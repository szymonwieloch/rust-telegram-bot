/**
 * test_queue — Unit tests for apr_queue_t (APR-Util thread-safe FIFO).
 *
 * Tests cover:
 *   - create / term lifecycle
 *   - push / pop single and multiple items
 *   - FIFO ordering
 *   - bounded capacity (trypush / trypop)
 *   - term behaviour
 *   - concurrent push / pop
 *   - edge cases (many items, long text, unusual IDs)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_queue.h>

/* ── Message wrapper ──────────────────────────────────────────────────────── */

typedef struct {
    long long chat_id;
    char     *text;        /* caller owns */
} msg_t;

static msg_t *msg_new(long long chat_id, const char *text) {
    msg_t *m = malloc(sizeof(*m));
    m->chat_id = chat_id;
    m->text    = strdup(text ? text : "");
    return m;
}

static void msg_free(msg_t *m) {
    if (m) {
        free(m->text);
        free(m);
    }
}

/* ── Test harness ─────────────────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_passed = 0;

#define TEST(name) \
    static void name(apr_pool_t *pool)

#define RUN_TEST(name, pool) do {                     \
    g_tests_run++;                                    \
    printf("  %-55s", #name "... ");                 \
    fflush(stdout);                                   \
    name(pool);                                       \
    g_tests_passed++;                                 \
    printf("PASSED\n");                               \
} while(0)

/* ── Helpers for threaded tests ───────────────────────────────────────────── */

typedef struct {
    apr_queue_t *q;
    msg_t       *msg;
    int          delay_us;
} push_arg_t;

static void *push_thread(void *arg) {
    push_arg_t *a = (push_arg_t *)arg;
    if (a->delay_us > 0) usleep(a->delay_us);
    apr_queue_push(a->q, a->msg);
    return NULL;
}

typedef struct {
    apr_queue_t *q;
    msg_t      **out;        /* receives popped pointer */
    int         *p_status;   /* out: 0 = success, -1 = EOF/error */
} pop_arg_t;

static void *pop_thread(void *arg) {
    pop_arg_t *a = (pop_arg_t *)arg;
    void *data = NULL;
    apr_status_t st = apr_queue_pop(a->q, &data);
    *a->out    = (msg_t *)data;
    *a->p_status = (st == APR_SUCCESS) ? 0 : -1;
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

TEST(test_create_term) {
    apr_queue_t *q = NULL;
    assert(apr_queue_create(&q, 16, pool) == APR_SUCCESS);
    assert(q != NULL);
    apr_queue_term(q);
    /* queue is freed with pool */
}

TEST(test_create_zero_capacity_fails) {
    apr_queue_t *q = NULL;
    /* APR queue requires capacity >= 1 */
    apr_status_t st = apr_queue_create(&q, 0, pool);
    if (st == APR_SUCCESS) apr_queue_term(q);
}

/* ── Single push / pop ────────────────────────────────────────────────────── */

TEST(test_push_pop_single) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    msg_t *in = msg_new(123LL, "hello");
    assert(apr_queue_push(q, in) == APR_SUCCESS);

    void *data = NULL;
    assert(apr_queue_pop(q, &data) == APR_SUCCESS);
    msg_t *out = (msg_t *)data;
    assert(out->chat_id == 123LL);
    assert(strcmp(out->text, "hello") == 0);
    msg_free(out);

    apr_queue_term(q);
}

TEST(test_push_pop_empty_text) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    msg_t *in = msg_new(99LL, "");
    apr_queue_push(q, in);

    void *data = NULL;
    apr_queue_pop(q, &data);
    msg_t *out = (msg_t *)data;
    assert(strcmp(out->text, "") == 0);
    msg_free(out);

    apr_queue_term(q);
}

/* ── FIFO ordering ────────────────────────────────────────────────────────── */

TEST(test_fifo_order) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    apr_queue_push(q, msg_new(1, "first"));
    apr_queue_push(q, msg_new(2, "second"));
    apr_queue_push(q, msg_new(3, "third"));

    void *data;
    msg_t *m;

    apr_queue_pop(q, &data); m = data;
    assert(m->chat_id == 1 && strcmp(m->text, "first") == 0); msg_free(m);

    apr_queue_pop(q, &data); m = data;
    assert(m->chat_id == 2 && strcmp(m->text, "second") == 0); msg_free(m);

    apr_queue_pop(q, &data); m = data;
    assert(m->chat_id == 3 && strcmp(m->text, "third") == 0); msg_free(m);

    apr_queue_term(q);
}

/* ── Try-push / try-pop (non-blocking) ────────────────────────────────────── */

TEST(test_trypop_empty_returns_eagain) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 4, pool);

    void *data = NULL;
    apr_status_t st = apr_queue_trypop(q, &data);
    assert(st == APR_EAGAIN);
    /* data is undefined on EAGAIN — don't dereference */

    apr_queue_term(q);
}

TEST(test_trypush_full_returns_eagain) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 2, pool);

    /* Fill the queue */
    apr_queue_push(q, msg_new(1, "a"));
    apr_queue_push(q, msg_new(2, "b"));

    /* Now trying to push should fail (queue full) */
    msg_t *extra = msg_new(3, "c");
    apr_status_t st = apr_queue_trypush(q, extra);
    assert(st == APR_EAGAIN);
    msg_free(extra);   /* not pushed, must free ourselves */

    /* Drain and check */
    void *data;
    apr_queue_pop(q, &data); msg_free((msg_t *)data);
    apr_queue_pop(q, &data); msg_free((msg_t *)data);

    apr_queue_term(q);
}

TEST(test_trypush_trypop_roundtrip) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 4, pool);

    msg_t *in = msg_new(42, "try-roundtrip");
    assert(apr_queue_trypush(q, in) == APR_SUCCESS);

    void *data = NULL;
    assert(apr_queue_trypop(q, &data) == APR_SUCCESS);
    msg_t *out = (msg_t *)data;
    assert(strcmp(out->text, "try-roundtrip") == 0);
    msg_free(out);

    apr_queue_term(q);
}

/* ── Term behaviour ───────────────────────────────────────────────────────── */

TEST(test_term_wakes_blocked_pop) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 4, pool);

    /* Start a thread that blocks on pop */
    msg_t *out = NULL;
    int status = 999;
    pop_arg_t arg = { q, &out, &status };

    pthread_t thd;
    pthread_create(&thd, NULL, pop_thread, &arg);
    usleep(100000);   /* let thread start blocking */

    /* Terminate — should wake the pop with APR_EOF */
    apr_queue_term(q);
    pthread_join(thd, NULL);

    assert(status == -1);    /* APR_EOF */
    assert(out == NULL);
}

TEST(test_term_then_push_fails) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 4, pool);

    apr_queue_term(q);

    msg_t *m = msg_new(1, "too-late");
    apr_status_t st = apr_queue_push(q, m);
    assert(st == APR_EOF);
    msg_free(m);
}

TEST(test_term_drains_remaining) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 4, pool);

    apr_queue_push(q, msg_new(10, "pre-term"));

    apr_queue_term(q);

    /* After term, pop returns APR_EOF (APR discards remaining items) */
    void *data = NULL;
    apr_status_t st = apr_queue_pop(q, &data);
    assert(st == APR_EOF);
    assert(data == NULL);
}

/* ── Size (informational) ─────────────────────────────────────────────────── */

TEST(test_size_reflects_items) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    assert(apr_queue_size(q) == 0);

    apr_queue_push(q, msg_new(1, "x"));
    apr_queue_push(q, msg_new(2, "y"));
    assert(apr_queue_size(q) == 2);

    void *data;
    apr_queue_pop(q, &data); msg_free((msg_t *)data);
    assert(apr_queue_size(q) == 1);

    apr_queue_pop(q, &data); msg_free((msg_t *)data);
    assert(apr_queue_size(q) == 0);

    apr_queue_term(q);
}

/* ── Concurrent: push from main, pop from pthread ─────────────────────────── */

TEST(test_concurrent_pop_in_thread) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    /* Push first, then pop from thread */
    msg_t *in = msg_new(777LL, "concurrent");
    apr_queue_push(q, in);

    msg_t *out = NULL;
    int status = 999;
    pop_arg_t arg = { q, &out, &status };

    pthread_t thd;
    pthread_create(&thd, NULL, pop_thread, &arg);
    pthread_join(thd, NULL);

    assert(status == 0);
    assert(out != NULL);
    assert(out->chat_id == 777LL);
    assert(strcmp(out->text, "concurrent") == 0);
    msg_free(out);

    apr_queue_term(q);
}

/* ── Concurrent: multiple pushers ─────────────────────────────────────────── */

TEST(test_multiple_concurrent_pushers) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 16, pool);

    #define N 4
    msg_t      *msgs[N];
    push_arg_t  args[N];
    pthread_t   threads[N];

    for (int i = 0; i < N; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "T%d", i);
        msgs[i] = msg_new(100 + i, buf);
        args[i].q        = q;
        args[i].msg      = msgs[i];
        args[i].delay_us = (i + 1) * 20000;
        pthread_create(&threads[i], NULL, push_thread, &args[i]);
    }

    /* Pop all N using trypop (non-blocking, safe from main thread).
       Items will arrive within ~80ms — poll for up to 200ms. */
    int received[N];
    memset(received, 0, sizeof(received));

    for (int i = 0; i < N; i++) {
        int got = 0;
        for (int attempt = 0; attempt < 200; attempt++) {
            void *data = NULL;
            if (apr_queue_trypop(q, &data) == APR_SUCCESS) {
                msg_t *m = (msg_t *)data;
                int idx = (int)(m->chat_id - 100);
                assert(idx >= 0 && idx < N);
                received[idx] = 1;
                msg_free(m);
                got = 1;
                break;
            }
            usleep(1000);   /* 1 ms */
        }
        assert(got && "timed out waiting for push from thread");
    }

    for (int i = 0; i < N; i++) {
        assert(received[i] == 1);
        pthread_join(threads[i], NULL);
    }

    apr_queue_term(q);
    #undef N
}

/* ── Edge cases ───────────────────────────────────────────────────────────── */

TEST(test_many_items) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 512, pool);

    #define N 500
    for (int i = 0; i < N; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "item-%d", i);
        apr_queue_push(q, msg_new(i, buf));
    }

    for (int i = 0; i < N; i++) {
        void *data = NULL;
        assert(apr_queue_pop(q, &data) == APR_SUCCESS);
        msg_t *m = (msg_t *)data;
        char expected[32];
        snprintf(expected, sizeof(expected), "item-%d", i);
        assert(m->chat_id == (long long)i);
        assert(strcmp(m->text, expected) == 0);
        msg_free(m);
    }
    #undef N

    apr_queue_term(q);
}

TEST(test_long_text) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    char big[4097];
    memset(big, 'Z', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    apr_queue_push(q, msg_new(1, big));

    void *data = NULL;
    apr_queue_pop(q, &data);
    msg_t *m = (msg_t *)data;
    assert(strlen(m->text) == 4096);
    assert(m->text[0] == 'Z' && m->text[4095] == 'Z');
    msg_free(m);

    apr_queue_term(q);
}

TEST(test_unusual_chat_ids) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    /* Telegram uses negative IDs for group chats */
    apr_queue_push(q, msg_new(0, "zero"));
    apr_queue_push(q, msg_new(-1001234567890LL, "group"));

    void *data;
    msg_t *m;

    apr_queue_pop(q, &data); m = data;
    assert(m->chat_id == 0); msg_free(m);

    apr_queue_pop(q, &data); m = data;
    assert(m->chat_id == -1001234567890LL); msg_free(m);

    apr_queue_term(q);
}

/* ── Interrupt all ────────────────────────────────────────────────────────── */

TEST(test_interrupt_all_wakes_pop) {
    apr_queue_t *q = NULL;
    apr_queue_create(&q, 8, pool);

    msg_t *out = NULL;
    int status = 999;
    pop_arg_t arg = { q, &out, &status };

    pthread_t thd;
    pthread_create(&thd, NULL, pop_thread, &arg);
    usleep(50000);

    apr_queue_interrupt_all(q);

    pthread_join(thd, NULL);
    /* Pop returns APR_EINTR → status = -1, out = NULL */
    assert(status == -1);
    assert(out == NULL);

    apr_queue_term(q);
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
    printf("  apr_queue_t Unit Tests\n");
    printf("═══════════════════════════════════════════════\n\n");

    printf("── Lifecycle ──\n");
    RUN_TEST(test_create_term, pool);
    RUN_TEST(test_create_zero_capacity_fails, pool);

    printf("\n── Push / Pop (single) ──\n");
    RUN_TEST(test_push_pop_single, pool);
    RUN_TEST(test_push_pop_empty_text, pool);

    printf("\n── FIFO ordering ──\n");
    RUN_TEST(test_fifo_order, pool);

    printf("\n── Try-push / Try-pop ──\n");
    RUN_TEST(test_trypop_empty_returns_eagain, pool);
    RUN_TEST(test_trypush_full_returns_eagain, pool);
    RUN_TEST(test_trypush_trypop_roundtrip, pool);

    printf("\n── Term behaviour ──\n");
    RUN_TEST(test_term_wakes_blocked_pop, pool);
    RUN_TEST(test_term_then_push_fails, pool);
    RUN_TEST(test_term_drains_remaining, pool);

    printf("\n── Size ──\n");
    RUN_TEST(test_size_reflects_items, pool);

    printf("\n── Concurrency ──\n");
    RUN_TEST(test_concurrent_pop_in_thread, pool);
    RUN_TEST(test_multiple_concurrent_pushers, pool);

    printf("\n── Edge cases ──\n");
    RUN_TEST(test_many_items, pool);
    RUN_TEST(test_long_text, pool);
    RUN_TEST(test_unusual_chat_ids, pool);

    printf("\n── Interrupt ──\n");
    RUN_TEST(test_interrupt_all_wakes_pop, pool);

    printf("\n═══════════════════════════════════════════════\n");
    printf("  %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("═══════════════════════════════════════════════\n\n");

    apr_pool_destroy(pool);
    apr_terminate();

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
