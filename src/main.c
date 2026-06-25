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
#include <apr_getopt.h>
#include <apr_pools.h>
#include <apr_strings.h>

#include <telebot.h>

#include "meteo.h"
#include "responder.h"
#include "utils.h"

/* ── Globals ──────────────────────────────────────────────────────────────── */

static apr_pool_t *g_pool = NULL;
static telebot_handler_t g_handle;
static MeteoContext *g_meteo = NULL;
static volatile int g_running = 1;

/* ── Signal handler ───────────────────────────────────────────────────────── */

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── /start command ───────────────────────────────────────────────────────── */

static void handle_start(const telebot_message_t *msg) {
    const char *reply_text =
        "🌤️  Hello! Send me a location like:\n\n"
        "    51.5074,-0.1278\n"
        "    London\n"
        "    New York\n\n"
        "and I'll tell you the current weather there.\n";

    telebot_send_message(g_handle, msg->chat->id, reply_text,
                         NULL, false, false, 0, NULL);
}

/* ── Weather handler (non-blocking) ───────────────────────────────────────── */

static void handle_weather(const telebot_message_t *msg, const char *arg) {
    if (utils_is_blank(arg)) {
        telebot_send_message(g_handle, msg->chat->id,
                             "⚠️  Usage: send coordinates like 51.5074,-0.1278"
                             " or a city name like London",
                             NULL, false, false, 0, NULL);
        return;
    }

    /* Allocate chat_id on the heap so the callback can free it */
    long long *chat_id = malloc(sizeof(*chat_id));
    *chat_id = msg->chat->id;

    /* Fire-and-forget: meteo_get returns immediately; the callback
       will push the result into the queue when ready. */
    meteo_get(g_meteo, arg, responder_weather_callback, chat_id);
}

/* ── Message dispatcher ───────────────────────────────────────────────────── */

static void on_message(const telebot_message_t *msg) {
    if (!msg || !msg->text) return;

    const char *text = msg->text;

    if (strcmp(text, "/start") == 0) {
        handle_start(msg);
    } else {
        handle_weather(msg, text);
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
        /* OPERATION_FAILED means an empty result — no new messages. */
        if (ret == TELEBOT_ERROR_OPERATION_FAILED) {
            continue;
        }
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
    const char *token = NULL;
    const char *geokey = NULL;

    /* ── Initialize APR (needed early for getopt) ──────────────────────────── */
    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "Failed to initialize APR\n");
        return 1;
    }
    atexit(apr_terminate);

    if (apr_pool_create(&g_pool, NULL) != APR_SUCCESS) {
        fprintf(stderr, "Failed to create APR memory pool\n");
        return 1;
    }

    /* ── Parse command-line options with APR getopt ───────────────────────── */
    apr_getopt_t *opt;
    apr_status_t rv = apr_getopt_init(&opt, g_pool, argc,
                                      (const char * const *)argv);
    if (rv != APR_SUCCESS) {
        fprintf(stderr, "Failed to initialize option parser\n");
        apr_pool_destroy(g_pool);
        return 1;
    }

    const apr_getopt_option_t options[] = {
        { "token",  't', true,  "Telegram bot token from @BotFather (required)" },
        { "geokey", 'k', true,  "Geocoding API key from https://geocode.maps.co/"
                                " (optional; required for city-name lookups)" },
        { "help",   'h', false, "Show this help message" },
        { NULL, 0, false, NULL }
    };

    int optch;
    const char *optarg;
    while ((rv = apr_getopt_long(opt, options, &optch, &optarg)) == APR_SUCCESS) {
        switch (optch) {
        case 't':
            token = optarg;
            break;
        case 'k':
            geokey = optarg;
            break;
        case 'h':
            printf("Usage: %s --token <TOKEN> [--geokey <KEY>]\n", argv[0]);
            printf("\nOptions:\n");
            for (int i = 0; options[i].name != NULL; i++) {
                printf("  -%c, --%-8s %s\n",
                       options[i].optch, options[i].name,
                       options[i].description ? options[i].description : "");
            }
            apr_pool_destroy(g_pool);
            return 0;
        default:
            fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
            apr_pool_destroy(g_pool);
            return 1;
        }
    }

    if (rv != APR_EOF) {
        fprintf(stderr, "Option parsing error. Try '%s --help'.\n", argv[0]);
        apr_pool_destroy(g_pool);
        return 1;
    }

    if (!token) {
        fprintf(stderr, "Error: --token is required.\n");
        fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
        apr_pool_destroy(g_pool);
        return 1;
    }

    /* Make a mutable copy — telebot_create expects char* */
    char *token_mut = apr_pstrdup(g_pool, token);

    /* ── Initialize weather library (geocoding + async runtime) ──────────── */
    g_meteo = meteo_init(geokey);
    if (!g_meteo) {
        fprintf(stderr, "Failed to initialize weather library\n");
        apr_pool_destroy(g_pool);
        return 1;
    }

    /* ── Initialize telebot ──────────────────────────────────────────────── */
    if (telebot_create(&g_handle, token_mut) != TELEBOT_ERROR_NONE) {
        fprintf(stderr, "Failed to create telebot handler\n");
        apr_pool_destroy(g_pool);
        return 1;
    }

    /* ── Start the response subsystem (queue + sender thread) ───────────── */
    {
        apr_pool_t *resp_pool = NULL;
        apr_pool_create(&resp_pool, NULL);  /* standalone — survives g_pool clears */
        if (responder_init(resp_pool, g_handle) != 0) {
            fprintf(stderr, "Failed to start response subsystem\n");
            apr_pool_destroy(resp_pool);
            telebot_destroy(g_handle);
            meteo_shutdown(g_meteo);
            apr_pool_destroy(g_pool);
            return 1;
        }
    }

    /* ── Setup signal handling ───────────────────────────────────────────── */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* ── Run the bot ─────────────────────────────────────────────────────── */
    polling_loop();

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    responder_shutdown();

    telebot_destroy(g_handle);
    meteo_shutdown(g_meteo);
    apr_pool_destroy(g_pool);

    printf("👋  Bot stopped.\n");
    return 0;
}
