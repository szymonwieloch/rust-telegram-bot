/**
 * responder — Asynchronous response delivery for the weather bot.
 *
 * Uses apr_queue_t (APR-Util) to decouple weather-fetch completion
 * (on arbitrary Tokio worker threads) from Telegram message delivery
 * (on a dedicated sender thread).
 *
 * Data flow:
 *   Rust async task → responder_weather_callback() → apr_queue_push()
 *   sender_thread_func() → apr_queue_pop() → telebot_send_message()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_queue.h>
#include <apr_thread_proc.h>

#include <telebot.h>

#include "meteo.h"
#include "responder.h"

/* ── Message wrapper (stored as void* in apr_queue_t) ─────────────────────── */

typedef struct {
    long long chat_id;
    char     *text;        /* caller owns, must free after popping */
} response_msg_t;

/* ── Global responder state ───────────────────────────────────────────────── */

static apr_queue_t        *g_queue = NULL;
static telebot_handler_t   g_handle;
static apr_thread_t       *g_thread = NULL;
static apr_pool_t         *g_pool = NULL;   /* owns queue, thread */

/* ── Sender thread ────────────────────────────────────────────────────────── */

static void *APR_THREAD_FUNC sender_thread_func(apr_thread_t *thd, void *arg) {
    (void)thd;
    (void)arg;

    while (1) {
        void *data = NULL;
        apr_status_t st = apr_queue_pop(g_queue, &data);
        if (st == APR_EOF) {
            break;
        }
        if (st == APR_EINTR) {
            continue;          /* spurious wakeup — retry */
        }
        /* st == APR_SUCCESS */
        response_msg_t *msg = (response_msg_t *)data;
        telebot_send_message(g_handle, msg->chat_id, msg->text,
                             NULL, false, false, 0, NULL);
        free(msg->text);
        free(msg);
    }

    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int responder_init(apr_pool_t *pool, telebot_handler_t handle) {
    g_pool   = pool;
    g_handle = handle;

    if (apr_queue_create(&g_queue, 256, pool) != APR_SUCCESS) {
        return -1;
    }

    if (apr_thread_create(&g_thread, NULL, sender_thread_func,
                          NULL, pool) != APR_SUCCESS) {
        return -1;
    }
    return 0;
}

void responder_shutdown(void) {
    apr_queue_term(g_queue);
    apr_thread_join(NULL, g_thread);

    /* Queue is freed with the pool — just clear the pointer */
    apr_pool_destroy(g_pool);
    g_pool  = NULL;
    g_queue = NULL;
}

void responder_weather_callback(void *user_context, CWeatherInfo wi) {
    long long chat_id = *(long long *)user_context;
    free(user_context);

    if (wi.err) {
        fprintf(stderr, "[weather-bot] internal error: %s\n", wi.err);
    }

    response_msg_t *msg = malloc(sizeof(*msg));
    msg->chat_id = chat_id;
    msg->text    = strdup(wi.message);
    apr_queue_push(g_queue, msg);

    meteo_free(&wi);
}
