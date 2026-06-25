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

#include <apr_thread_proc.h>

#include <telebot.h>

#include "meteo.h"
#include "queue.h"
#include "responder.h"

/* ── Global responder state ───────────────────────────────────────────────── */

static response_queue_t   g_queue;
static telebot_handler_t  g_handle;
static apr_thread_t      *g_thread = NULL;
static apr_pool_t        *g_pool = NULL;   /* owns mutex, cond, thread */

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
