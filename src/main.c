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
#include <apr_signal.h>

#include <telebot.h>

#include "meteo.h"
#include "responder.h"
#include "utils.h"

/* ── Globals ──────────────────────────────────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;

/* ── Signal handler ───────────────────────────────────────────────────────── */

static void on_signal(int sig)
{
    (void) sig;
    g_running = 0;
}

/* ── /start command ───────────────────────────────────────────────────────── */

static void handle_start(telebot_handler_t handle, const telebot_message_t *msg)
{
    const char *reply_text = "🌤️  Hello! Send me a location like:\n\n"
                             "    51.5074,-0.1278\n"
                             "    London\n"
                             "    New York\n\n"
                             "and I'll tell you the current weather there.\n";

    telebot_error_e ret =
        telebot_send_message(handle, msg->chat->id, reply_text, NULL, false, false, 0, NULL);
    if (ret != TELEBOT_ERROR_NONE) {
        fprintf(stderr, "[weather-bot] /start reply failed: %d\n", ret);
    }
}

/* ── Weather handler (non-blocking) ───────────────────────────────────────── */

static void handle_weather(telebot_handler_t handle, const MeteoContext *meteo,
                           const telebot_message_t *msg, const char *arg)
{
    if (utils_is_blank(arg)) {
        telebot_error_e ret = telebot_send_message(handle, msg->chat->id,
                                                   "⚠️  Usage: send coordinates like 51.5074,-0.1278"
                                                   " or a city name like London",
                                                   NULL, false, false, 0, NULL);
        if (ret != TELEBOT_ERROR_NONE) {
            fprintf(stderr, "[weather-bot] usage reply failed: %d\n", ret);
        }
        return;
    }

    /* Allocate chat_id on the heap so the callback can free it */
    long long *chat_id = malloc(sizeof(*chat_id));
    *chat_id = msg->chat->id;

    /* Fire-and-forget: meteo_get returns immediately; the callback
       will push the result into the queue when ready. */
    meteo_get(meteo, arg, responder_weather_callback, chat_id);
}

/* ── Message dispatcher ───────────────────────────────────────────────────── */

static void on_message(telebot_handler_t handle, const MeteoContext *meteo,
                       const telebot_message_t *msg)
{
    if (!msg || !msg->text)
        return;

    const char *text = msg->text;

    if (strcmp(text, "/start") == 0) {
        handle_start(handle, msg);
    } else {
        handle_weather(handle, meteo, msg, text);
    }
}

/* ── Polling loop ─────────────────────────────────────────────────────────── */

static void polling_loop(apr_pool_t *pool, telebot_handler_t handle, const MeteoContext *meteo)
{
    int offset = 0, limit = 10, timeout = 1;
    telebot_error_e ret;
    telebot_update_t *updates = NULL;
    int count;

    /* Only poll for message-type updates */
    telebot_update_type_e allowed[] = {TELEBOT_UPDATE_TYPE_MESSAGE};

    printf("🤖  Bot started. Press Ctrl+C to stop.\n");

    while (g_running) {
        /* Clear the pool each iteration to avoid unbounded growth */
        apr_pool_clear(pool);

        updates = NULL;
        count = 0;

        ret = telebot_get_updates(handle, offset, limit, timeout, allowed, 1, &updates, &count);
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
                on_message(handle, meteo, &upd->message);
            }
            offset = upd->update_id + 1;
        }

        telebot_put_updates(updates, count);
    }
}

/* ── Argument parsing ──────────────────────────────────────────────────────── */

/**
 * Parse command-line options.
 *
 * @param pool   APR memory pool
 * @param argc   argument count (from main)
 * @param argv   argument vector (from main)
 * @param token  [out] set to the --token value, or NULL
 * @param geokey [out] set to the --geokey value, or NULL
 * @return       0 on success, 1 on error, 2 if --help was shown
 */
static int parse_args(apr_pool_t *pool, int argc, char **argv, const char **token,
                      const char **geokey)
{
    *token = NULL;
    *geokey = NULL;

    apr_getopt_t *opt;
    apr_status_t rv = apr_getopt_init(&opt, pool, argc, (const char *const *) argv);
    if (rv != APR_SUCCESS) {
        fprintf(stderr, "Failed to initialize option parser\n");
        return 1;
    }

    const apr_getopt_option_t options[] = {
        {"token", 't', true, "Telegram bot token from @BotFather (required)"},
        {"geokey", 'k', true,
         "Geocoding API key from https://geocode.maps.co/"
         " (optional; required for city-name lookups)"},
        {"help", 'h', false, "Show this help message"},
        {NULL, 0, false, NULL}};

    int optch;
    const char *optarg;
    while ((rv = apr_getopt_long(opt, options, &optch, &optarg)) == APR_SUCCESS) {
        switch (optch) {
        case 't':
            *token = optarg;
            break;
        case 'k':
            *geokey = optarg;
            break;
        case 'h':
            printf("Usage: %s --token <TOKEN> [--geokey <KEY>]\n", argv[0]);
            printf("\nOptions:\n");
            for (int i = 0; options[i].name != NULL; i++) {
                printf("  -%c, --%-8s %s\n", options[i].optch, options[i].name,
                       options[i].description ? options[i].description : "");
            }
            return 2; /* caller should exit with 0 */
        default:
            fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
            return 1;
        }
    }

    if (rv != APR_EOF) {
        fprintf(stderr, "Option parsing error. Try '%s --help'.\n", argv[0]);
        return 1;
    }

    if (!*token) {
        fprintf(stderr, "Error: --token is required.\n");
        fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
        return 1;
    }

    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *token = NULL;
    const char *geokey = NULL;

    apr_pool_t *pool = NULL;
    telebot_handler_t handle;
    MeteoContext *meteo = NULL;

    /* ── Initialize APR (needed early for getopt) ──────────────────────────── */
    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "Failed to initialize APR\n");
        return 1;
    }
    atexit(apr_terminate);

    if (apr_pool_create(&pool, NULL) != APR_SUCCESS) {
        fprintf(stderr, "Failed to create APR memory pool\n");
        return 1;
    }

    /* ── Parse command-line options ──────────────────────────────────────── */
    {
        int pr = parse_args(pool, argc, argv, &token, &geokey);
        if (pr != 0) {
            apr_pool_destroy(pool);
            return (pr == 2) ? 0 : 1;
        }
    }

    /* ── Copy token for telebot_create (library copies it internally via
       strdup, so we can free our copy immediately after the call). ────── */
    char *token_mut = strdup(token);
    if (!token_mut) {
        fprintf(stderr, "Failed to allocate token copy\n");
        apr_pool_destroy(pool);
        return 1;
    }

    /* ── Initialize weather library (geocoding + async runtime) ──────────── */
    meteo = meteo_init(geokey);
    if (!meteo) {
        fprintf(stderr, "Failed to initialize weather library\n");
        free(token_mut);
        apr_pool_destroy(pool);
        return 1;
    }

    /* ── Initialize telebot ──────────────────────────────────────────────── */
    if (telebot_create(&handle, token_mut) != TELEBOT_ERROR_NONE) {
        fprintf(stderr, "Failed to create telebot handler\n");
        free(token_mut);
        apr_pool_destroy(pool);
        return 1;
    }
    free(token_mut); /* library copied it — our copy no longer needed */

    /* ── Start the response subsystem (queue + sender thread) ───────────── */
    {
        apr_pool_t *resp_pool = NULL;
        apr_pool_create(&resp_pool, NULL); /* standalone — survives pool clears */
        if (responder_init(resp_pool, handle) != 0) {
            fprintf(stderr, "Failed to start response subsystem\n");
            apr_pool_destroy(resp_pool);
            telebot_destroy(handle);
            meteo_shutdown(meteo);
            apr_pool_destroy(pool);
            return 1;
        }
    }

    /* ── Setup signal handling (APR wrapper for portability) ─────────────── */
    apr_signal(SIGINT, on_signal);
    apr_signal(SIGTERM, on_signal);

    /* ── Run the bot ─────────────────────────────────────────────────────── */
    polling_loop(pool, handle, meteo);

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    /* Order matters: shutdown Tokio first so no more callbacks can fire,
       then drain the response queue and join the sender thread. */
    meteo_shutdown(meteo);

    responder_shutdown();

    telebot_destroy(handle);
    apr_pool_destroy(pool);

    printf("👋  Bot stopped.\n");
    return 0;
}
