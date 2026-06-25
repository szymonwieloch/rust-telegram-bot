/**
 * telegram-weather-bot — A Telegram bot that reports current weather.
 *
 * Combines three libraries:
 *   - APR          (portable runtime: memory pools, threading)
 *   - telebot      (Telegram Bot API client)
 *   - rust-lib     (weather data via Open-Meteo, C FFI)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <apr_general.h>
#include <apr_pools.h>

#include <telebot.h>

#include "rust_telegram_bot.h"

/* ── Globals ──────────────────────────────────────────────────────────────── */

static apr_pool_t *g_pool = NULL;
static telebot_handler_t g_handle;
static volatile int g_running = 1;

/* ── Signal handler ───────────────────────────────────────────────────────── */

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── /start command ───────────────────────────────────────────────────────── */

static void handle_start(const telebot_message_t *msg) {
    const char *reply_text =
        "🌤️  Hello! Send me coordinates like:\n\n"
        "    /weather 51.5074,-0.1278\n\n"
        "and I'll tell you the current weather at that location.\n";

    telebot_send_message(g_handle, msg->chat->id, reply_text,
                         NULL, false, false, 0, NULL);
}

/* ── /weather command ─────────────────────────────────────────────────────── */

static void handle_weather(const telebot_message_t *msg, const char *arg) {
    if (!arg || strlen(arg) == 0) {
        telebot_send_message(g_handle, msg->chat->id,
                             "⚠️  Usage: /weather 51.5074,-0.1278",
                             NULL, false, false, 0, NULL);
        return;
    }

    /* Call rust-lib via its C FFI */
    CWeatherInfo wi = meteo_get(arg);

    const char *reply;
    if (wi.err) {
        /* Allocate a formatted error string from the APR pool */
        char *buf = apr_palloc(g_pool, strlen(wi.err) + 64);
        sprintf(buf, "❌  Error fetching weather:\n\n%s", wi.err);
        reply = buf;
    } else {
        reply = wi.message;
    }

    telebot_send_message(g_handle, msg->chat->id, reply,
                         NULL, false, false, 0, NULL);

    meteo_free(&wi);
}

/* ── Message dispatcher ───────────────────────────────────────────────────── */

static void on_message(const telebot_message_t *msg) {
    if (!msg || !msg->text) return;

    const char *text = msg->text;

    if (strcmp(text, "/start") == 0) {
        handle_start(msg);
    } else if (strncmp(text, "/weather ", 9) == 0) {
        handle_weather(msg, text + 9);
    } else if (strcmp(text, "/weather") == 0) {
        handle_weather(msg, NULL);
    }
}

/* ── Polling loop ─────────────────────────────────────────────────────────── */

static void polling_loop(void) {
    int offset = 0, limit = 10, timeout = 1;
    telebot_error_e ret;
    telebot_update_t *updates = NULL;
    int count = 0;

    /* Only poll for message-type updates */
    telebot_update_type_e allowed[] = { TELEBOT_UPDATE_TYPE_MESSAGE };

    printf("🤖  Bot started. Press Ctrl+C to stop.\n");

    while (g_running) {
        /* Clear the pool each iteration to avoid unbounded growth */
        apr_pool_clear(g_pool);

        updates = NULL;
        count = 0;

        ret = telebot_get_updates(g_handle, offset, limit, timeout,
                                  allowed, 1, &updates, &count);
        if (ret != TELEBOT_ERROR_NONE) {
            fprintf(stderr, "telebot_get_updates failed: %d\n", ret);
            apr_sleep(1000000); /* 1 second */
            continue;
        }

        for (int i = 0; i < count; i++) {
            telebot_update_t *upd = &updates[i];
            if (upd->update_type == TELEBOT_UPDATE_TYPE_MESSAGE) {
                on_message(&upd->message);
            }
            offset = upd->update_id + 1;
        }

        telebot_put_updates(updates, count);
    }
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <TELEGRAM_BOT_TOKEN>\n", argv[0]);
        return 1;
    }
    /* telebot_create expects char*, make a mutable copy */
    char *token = argv[1];

    /* ── Initialize APR ──────────────────────────────────────────────────── */
    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "Failed to initialize APR\n");
        return 1;
    }
    atexit(apr_terminate);

    if (apr_pool_create(&g_pool, NULL) != APR_SUCCESS) {
        fprintf(stderr, "Failed to create APR memory pool\n");
        return 1;
    }

    /* ── Initialize telebot ──────────────────────────────────────────────── */
    if (telebot_create(&g_handle, token) != TELEBOT_ERROR_NONE) {
        fprintf(stderr, "Failed to create telebot handler\n");
        return 1;
    }

    /* ── Setup signal handling ───────────────────────────────────────────── */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* ── Run the bot ─────────────────────────────────────────────────────── */
    polling_loop();

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    telebot_destroy(g_handle);
    apr_pool_destroy(g_pool);

    printf("👋  Bot stopped.\n");
    return 0;
}
