/**
 * responder — Asynchronous response delivery for the weather bot.
 *
 * A thread-safe queue decouples weather-fetch completion (which happens
 * on arbitrary Tokio worker threads) from Telegram message delivery.
 *
 * Data flow:
 *   Rust async task → responder_weather_callback() → queue_push()
 *   sender_thread_func() → queue_pop() → telebot_send_message()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include <apr_thread_proc.h>

#include <telebot.h>

#include "meteo.h"
#include "responder.h"

/* ── Queue types ──────────────────────────────────────────────────────────── */

typedef struct response_node {
    long long               chat_id;
    char                   *text;
    struct response_node   *next;
} response_node_t;

typedef struct {
    response_node_t      *head;       /* pop from head */
    response_node_t      *tail;       /* push to tail  */
    apr_thread_mutex_t   *lock;
    apr_thread_cond_t    *cond;
    int                   shutdown;   /* set to 1 to wake the consumer */
} response_queue_t;

/* ── Global responder state ───────────────────────────────────────────────── */

static response_queue_t   g_queue;
static telebot_handler_t  g_handle;
static apr_thread_t      *g_thread = NULL;
static apr_pool_t        *g_pool = NULL;   /* owns mutex, cond, thread */

/* ── Queue operations ─────────────────────────────────────────────────────── */

static void queue_init(response_queue_t *q, apr_pool_t *pool) {
    q->head = NULL;
    q->tail = NULL;
    q->shutdown = 0;
    apr_thread_mutex_create(&q->lock, APR_THREAD_MUTEX_DEFAULT, pool);
    apr_thread_cond_create(&q->cond, pool);
}

static void queue_destroy(response_queue_t *q) {
    while (q->head) {
        response_node_t *n = q->head;
        q->head = n->next;
        free(n->text);
        free(n);
    }
    apr_thread_mutex_destroy(q->lock);
    apr_thread_cond_destroy(q->cond);
}

static void queue_push(response_queue_t *q, long long chat_id,
                       const char *text) {
    response_node_t *n = malloc(sizeof(*n));
    n->chat_id = chat_id;
    n->text    = strdup(text);
    n->next    = NULL;

    apr_thread_mutex_lock(q->lock);
    if (q->tail) {
        q->tail->next = n;
        q->tail = n;
    } else {
        q->head = q->tail = n;
    }
    apr_thread_cond_signal(q->cond);
    apr_thread_mutex_unlock(q->lock);
}

/** Returns 0 on success, -1 on shutdown. */
static int queue_pop(response_queue_t *q, response_node_t *out) {
    apr_thread_mutex_lock(q->lock);
    while (!q->head && !q->shutdown) {
        apr_thread_cond_wait(q->cond, q->lock);
    }
    if (q->shutdown && !q->head) {
        apr_thread_mutex_unlock(q->lock);
        return -1;
    }
    response_node_t *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    apr_thread_mutex_unlock(q->lock);

    out->chat_id = n->chat_id;
    out->text    = n->text;
    free(n);
    return 0;
}

static void queue_signal_shutdown(response_queue_t *q) {
    apr_thread_mutex_lock(q->lock);
    q->shutdown = 1;
    apr_thread_cond_signal(q->cond);
    apr_thread_mutex_unlock(q->lock);
}

/* ── Sender thread ────────────────────────────────────────────────────────── */

static void *APR_THREAD_FUNC sender_thread_func(apr_thread_t *thd, void *arg) {
    (void)thd;
    (void)arg;
    response_node_t resp;

    while (queue_pop(&g_queue, &resp) == 0) {
        telebot_send_message(g_handle, resp.chat_id, resp.text,
                             NULL, false, false, 0, NULL);
        free(resp.text);
    }

    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int responder_init(apr_pool_t *pool, telebot_handler_t handle) {
    g_pool   = pool;
    g_handle = handle;
    queue_init(&g_queue, pool);

    if (apr_thread_create(&g_thread, NULL, sender_thread_func,
                          NULL, pool) != APR_SUCCESS) {
        queue_destroy(&g_queue);
        return -1;
    }
    return 0;
}

void responder_shutdown(void) {
    queue_signal_shutdown(&g_queue);
    apr_thread_join(NULL, g_thread);
    queue_destroy(&g_queue);
    apr_pool_destroy(g_pool);
    g_pool = NULL;
}

void responder_weather_callback(void *user_context, CWeatherInfo wi) {
    long long chat_id = *(long long *)user_context;
    free(user_context);

    if (wi.err) {
        fprintf(stderr, "[weather-bot] internal error: %s\n", wi.err);
    }

    queue_push(&g_queue, chat_id, wi.message);
    meteo_free(&wi);
}
