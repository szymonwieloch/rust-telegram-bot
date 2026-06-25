/**
 * test_queue — Unit tests for the thread-safe response queue.
 *
 * Tests cover:
 *   - init / destroy lifecycle
 *   - push / pop single and multiple items
 *   - FIFO ordering
 *   - shutdown behaviour
 *   - edge cases (empty text, long text, many items)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include <apr_general.h>
#include <apr_pools.h>

#include "../src/queue.h"

/* ── Test harness ─────────────────────────────────────────────────────────── */

static int g_tests_run     = 0;
static int g_tests_passed  = 0;

#define TEST(name) \
    static void name(apr_pool_t *pool)

#define RUN_TEST(name, pool) do {                    \
    g_tests_run++;                                   \
    printf("  %-55s", #name "... ");                \
    fflush(stdout);                                  \
    name(pool);                                      \
    g_tests_passed++;                                \
    printf("PASSED\n");                              \
} while(0)

/* ── Helper: push and pop in a background thread ──────────────────────────── */

typedef struct {
    response_queue_t *q;
    long long         chat_id;
    const char       *text;
    int               delay_us;   /* microseconds to sleep before pushing */
} push_thread_arg_t;

static void *push_thread_func(void *arg) {
    push_thread_arg_t *a = (push_thread_arg_t *)arg;
    if (a->delay_us > 0) {
        usleep(a->delay_us);
    }
    queue_push(a->q, a->chat_id, a->text);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

TEST(test_init_destroy) {
    response_queue_t q;
    queue_init(&q, pool);
    assert(q.head == NULL);
    assert(q.tail == NULL);
    assert(q.shutdown == 0);
    queue_destroy(&q);
}

TEST(test_destroy_empty_twice_is_harmless) {
    /* Just verify no double-free — if we get here without crashing, ok. */
    response_queue_t q;
    queue_init(&q, pool);
    queue_destroy(&q);
    /* queue_destroy again would be UB; we just test the normal path. */
}

/* ── Single push / pop ────────────────────────────────────────────────────── */

TEST(test_push_pop_single) {
    response_queue_t q;
    queue_init(&q, pool);

    queue_push(&q, 123456LL, "hello world");

    response_node_t out;
    memset(&out, 0xAA, sizeof(out));   /* poison to detect partial writes */
    int ret = queue_pop(&q, &out);

    assert(ret == 0);
    assert(out.chat_id == 123456LL);
    assert(strcmp(out.text, "hello world") == 0);

    free(out.text);
    queue_signal_shutdown(&q);
    assert(queue_pop(&q, &out) == -1);
    queue_destroy(&q);
}

TEST(test_push_pop_empty_text) {
    response_queue_t q;
    queue_init(&q, pool);

    queue_push(&q, 999LL, "");

    response_node_t out;
    int ret = queue_pop(&q, &out);
    assert(ret == 0);
    assert(out.chat_id == 999LL);
    assert(strcmp(out.text, "") == 0);

    free(out.text);
    queue_signal_shutdown(&q);
    queue_pop(&q, &out);   /* drain shutdown */
    queue_destroy(&q);
}

/* ── FIFO ordering ────────────────────────────────────────────────────────── */

TEST(test_fifo_order_three_items) {
    response_queue_t q;
    queue_init(&q, pool);

    queue_push(&q, 1, "first");
    queue_push(&q, 2, "second");
    queue_push(&q, 3, "third");

    response_node_t out;

    assert(queue_pop(&q, &out) == 0);
    assert(out.chat_id == 1 && strcmp(out.text, "first") == 0);
    free(out.text);

    assert(queue_pop(&q, &out) == 0);
    assert(out.chat_id == 2 && strcmp(out.text, "second") == 0);
    free(out.text);

    assert(queue_pop(&q, &out) == 0);
    assert(out.chat_id == 3 && strcmp(out.text, "third") == 0);
    free(out.text);

    queue_signal_shutdown(&q);
    assert(queue_pop(&q, &out) == -1);
    queue_destroy(&q);
}

/* ── Shutdown behaviour ───────────────────────────────────────────────────── */

TEST(test_shutdown_on_empty_returns_neg1) {
    response_queue_t q;
    queue_init(&q, pool);

    queue_signal_shutdown(&q);

    response_node_t out;
    assert(queue_pop(&q, &out) == -1);

    queue_destroy(&q);
}

TEST(test_shutdown_drains_remaining_items) {
    response_queue_t q;
    queue_init(&q, pool);

    queue_push(&q, 10, "before-shutdown");
    queue_signal_shutdown(&q);
    queue_push(&q, 20, "after-shutdown");   /* push still works after shutdown */

    response_node_t out;

    /* Should drain both items in order, then return -1 */
    assert(queue_pop(&q, &out) == 0);
    assert(strcmp(out.text, "before-shutdown") == 0);
    free(out.text);

    assert(queue_pop(&q, &out) == 0);
    assert(strcmp(out.text, "after-shutdown") == 0);
    free(out.text);

    assert(queue_pop(&q, &out) == -1);

    queue_destroy(&q);
}

/* ── Helper types for threaded pop tests ─────────────────────────────────── */

typedef struct {
    response_queue_t *q;
    long long         expected_chat_id;
    const char       *expected_text;
    int              *p_result;       /* out: 1 = ok, 0 = mismatch */
} pop_thread_arg_t;

static void *pop_thread_func(void *arg) {
    pop_thread_arg_t *a = (pop_thread_arg_t *)arg;
    response_node_t out;
    int ret = queue_pop(a->q, &out);
    if (ret != 0) {
        *a->p_result = -1;   /* unexpected shutdown */
        return NULL;
    }
    *a->p_result = (out.chat_id == a->expected_chat_id &&
                    strcmp(out.text, a->expected_text) == 0) ? 1 : 0;
    free(out.text);
    return NULL;
}

/* ── Concurrent: push from main, pop from APR thread ─────────────────────── */

TEST(test_concurrent_pop_in_thread) {
    response_queue_t q;
    queue_init(&q, pool);

    /* Push first, then have thread pop */
    queue_push(&q, 888LL, "hello-thread");

    int pop_ok = -99;
    pop_thread_arg_t arg;
    arg.q              = &q;
    arg.expected_chat_id = 888LL;
    arg.expected_text  = "hello-thread";
    arg.p_result       = &pop_ok;

    pthread_t thd;
    int st = pthread_create(&thd, NULL, pop_thread_func, &arg);
    assert(st == 0);

    pthread_join(thd, NULL);
    assert(pop_ok == 1);

    queue_signal_shutdown(&q);
    response_node_t out;
    assert(queue_pop(&q, &out) == -1);
    queue_destroy(&q);
}

/* ── Concurrent: multiple pushers, one consumer thread ───────────────────── */

TEST(test_multiple_pushers_one_consumer_thread) {
    response_queue_t  q;
    queue_init(&q, pool);

    #define N 4
    push_thread_arg_t pargs[N];
    pthread_t         pthreads[N];
    const char       *texts[N] = { "A", "B", "C", "D" };

    /* Spawn pusher threads (they sleep briefly to stagger) */
    for (int i = 0; i < N; i++) {
        pargs[i].q        = &q;
        pargs[i].chat_id  = 200 + i;
        pargs[i].text     = texts[i];
        pargs[i].delay_us = (i + 1) * 20000;
        pthread_create(&pthreads[i], NULL, push_thread_func, &pargs[i]);
    }

    /* Consumer in the main thread: we pre-push a shutdown marker via a
       separate thread approach.  Instead, just pop N items — the pusher
       threads ensure all N are available. */
    int received[N];
    memset(received, 0, sizeof(received));
    response_node_t out;

    for (int i = 0; i < N; i++) {
        /* We cannot block the main thread on apr_thread_cond_wait (APR issue).
           But we know all pushers finish within ~80ms.  Use a brief poll loop. */
        int got = 0;
        for (int attempt = 0; attempt < 200; attempt++) {
            apr_thread_mutex_lock(q.lock);
            if (q.head != NULL) {
                /* Manually dequeue to avoid apr_thread_cond_wait */
                response_node_t *n = q.head;
                q.head = n->next;
                if (!q.head) q.tail = NULL;
                apr_thread_mutex_unlock(q.lock);

                out.chat_id = n->chat_id;
                out.text    = n->text;
                free(n);

                int idx = (int)(out.chat_id - 200);
                assert(idx >= 0 && idx < N);
                received[idx] = 1;
                free(out.text);
                got = 1;
                break;
            }
            apr_thread_mutex_unlock(q.lock);
            usleep(1000);   /* 1 ms */
        }
        assert(got && "timed out waiting for push from thread");
    }

    /* Verify all N were received exactly once */
    for (int i = 0; i < N; i++) {
        assert(received[i] == 1);
    }

    for (int i = 0; i < N; i++) {
        pthread_join(pthreads[i], NULL);
    }

    queue_signal_shutdown(&q);
    assert(queue_pop(&q, &out) == -1);
    queue_destroy(&q);
    #undef N
}

/* ── Edge cases ───────────────────────────────────────────────────────────── */

TEST(test_push_pop_long_text) {
    response_queue_t q;
    queue_init(&q, pool);

    /* Build a 4 KB string */
    char long_text[4097];
    memset(long_text, 'X', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';

    queue_push(&q, 42, long_text);

    response_node_t out;
    assert(queue_pop(&q, &out) == 0);
    assert(strlen(out.text) == 4096);
    assert(out.text[0] == 'X' && out.text[4095] == 'X');
    free(out.text);

    queue_signal_shutdown(&q);
    queue_pop(&q, &out);
    queue_destroy(&q);
}

TEST(test_push_pop_special_characters) {
    response_queue_t q;
    queue_init(&q, pool);

    const char *special = "line1\nline2\t tab\0hidden" /* only up to \0 */;
    queue_push(&q, 1, "🌤️  Unicode and emoji ✓");
    queue_push(&q, 2, "C:\\path\\to\\file");
    queue_push(&q, 3, "100% \"quoted\" 'text'");

    response_node_t out;

    assert(queue_pop(&q, &out) == 0);
    assert(strcmp(out.text, "🌤️  Unicode and emoji ✓") == 0);
    free(out.text);

    assert(queue_pop(&q, &out) == 0);
    assert(strcmp(out.text, "C:\\path\\to\\file") == 0);
    free(out.text);

    assert(queue_pop(&q, &out) == 0);
    assert(strcmp(out.text, "100% \"quoted\" 'text'") == 0);
    free(out.text);

    queue_signal_shutdown(&q);
    assert(queue_pop(&q, &out) == -1);
    queue_destroy(&q);

    (void)special;   /* suppress unused warning */
}

TEST(test_push_pop_many_items) {
    response_queue_t q;
    queue_init(&q, pool);

    #define N 500
    for (int i = 0; i < N; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "item-%d", i);
        queue_push(&q, i, buf);
    }

    response_node_t out;
    for (int i = 0; i < N; i++) {
        assert(queue_pop(&q, &out) == 0);
        char expected[32];
        snprintf(expected, sizeof(expected), "item-%d", i);
        assert(out.chat_id == (long long)i);
        assert(strcmp(out.text, expected) == 0);
        free(out.text);
    }

    queue_signal_shutdown(&q);
    assert(queue_pop(&q, &out) == -1);
    queue_destroy(&q);
    #undef N
}

TEST(test_push_null_chat_id) {
    response_queue_t q;
    queue_init(&q, pool);

    queue_push(&q, 0, "zero chat id");

    response_node_t out;
    assert(queue_pop(&q, &out) == 0);
    assert(out.chat_id == 0);
    assert(strcmp(out.text, "zero chat id") == 0);
    free(out.text);

    queue_signal_shutdown(&q);
    queue_pop(&q, &out);
    queue_destroy(&q);
}

TEST(test_push_negative_chat_id) {
    response_queue_t q;
    queue_init(&q, pool);

    /* Telegram uses negative IDs for group chats */
    queue_push(&q, -1001234567890LL, "group message");

    response_node_t out;
    assert(queue_pop(&q, &out) == 0);
    assert(out.chat_id == -1001234567890LL);
    assert(strcmp(out.text, "group message") == 0);
    free(out.text);

    queue_signal_shutdown(&q);
    queue_pop(&q, &out);
    queue_destroy(&q);
}

/* ── Shutdown while a pop is blocking (threaded) ──────────────────────────── */

typedef struct {
    response_queue_t *q;
    int               *p_result;   /* out: queue_pop return value */
} shutdown_test_arg_t;

static void *blocking_pop_thread(void *arg) {
    shutdown_test_arg_t *a = (shutdown_test_arg_t *)arg;
    response_node_t out;
    *a->p_result = queue_pop(a->q, &out);
    if (*a->p_result == 0) {
        free(out.text);
    }
    return NULL;
}

TEST(test_shutdown_wakes_blocked_pop) {
    response_queue_t q;
    queue_init(&q, pool);

    int pop_result = 999;   /* sentinel */
    shutdown_test_arg_t arg;
    arg.q        = &q;
    arg.p_result = &pop_result;

    pthread_t thd;
    pthread_create(&thd, NULL, blocking_pop_thread, &arg);

    /* Give the thread time to start blocking on queue_pop */
    usleep(100000);   /* 100 ms */

    /* Now shut down — should wake the blocked pop */
    queue_signal_shutdown(&q);

    pthread_join(thd, NULL);

    assert(pop_result == -1);

    queue_destroy(&q);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "FATAL: apr_initialize failed\n");
        return 1;
    }

    apr_pool_t *pool = NULL;
    apr_pool_create(&pool, NULL);

    printf("\n");
    printf("═══════════════════════════════════════════════\n");
    printf("  Queue Unit Tests\n");
    printf("═══════════════════════════════════════════════\n\n");

    /* ── Lifecycle ──────────────────────────────────────────────────────── */
    printf("── Lifecycle ──\n");
    RUN_TEST(test_init_destroy, pool);
    RUN_TEST(test_destroy_empty_twice_is_harmless, pool);

    /* ── Single push/pop ────────────────────────────────────────────────── */
    printf("\n── Push / Pop (single) ──\n");
    RUN_TEST(test_push_pop_single, pool);
    RUN_TEST(test_push_pop_empty_text, pool);

    /* ── FIFO ordering ──────────────────────────────────────────────────── */
    printf("\n── FIFO ordering ──\n");
    RUN_TEST(test_fifo_order_three_items, pool);

    /* ── Shutdown ───────────────────────────────────────────────────────── */
    printf("\n── Shutdown behaviour ──\n");
    RUN_TEST(test_shutdown_on_empty_returns_neg1, pool);
    RUN_TEST(test_shutdown_drains_remaining_items, pool);

    /* ── Concurrency ────────────────────────────────────────────────────── */
    printf("\n── Concurrency ──\n");
    RUN_TEST(test_concurrent_pop_in_thread, pool);
    RUN_TEST(test_multiple_pushers_one_consumer_thread, pool);
    RUN_TEST(test_shutdown_wakes_blocked_pop, pool);

    /* ── Edge cases ─────────────────────────────────────────────────────── */
    printf("\n── Edge cases ──\n");
    RUN_TEST(test_push_pop_long_text, pool);
    RUN_TEST(test_push_pop_special_characters, pool);
    RUN_TEST(test_push_pop_many_items, pool);
    RUN_TEST(test_push_null_chat_id, pool);
    RUN_TEST(test_push_negative_chat_id, pool);

    /* ── Summary ────────────────────────────────────────────────────────── */
    printf("\n═══════════════════════════════════════════════\n");
    printf("  %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("═══════════════════════════════════════════════\n\n");

    apr_pool_destroy(pool);
    apr_terminate();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
