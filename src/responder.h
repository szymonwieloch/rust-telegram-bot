#ifndef RESPONDER_H
#define RESPONDER_H

#include <apr_pools.h>
#include <telebot.h>
#include "meteo.h"

/**
 * Initialise the response subsystem: creates the thread-safe queue and
 * starts the dedicated sender thread.
 *
 * @param pool    APR pool for the queue's synchronisation primitives.
 *                Must outlive responder_shutdown() (i.e. do NOT sub-pool
 *                it under a pool that is periodically cleared).
 * @param handle  telebot handler used to send messages.
 * @return        APR_SUCCESS on success.
 */
int responder_init(apr_pool_t *pool, telebot_handler_t handle);

/**
 * Shut down the response subsystem gracefully:
 * wake the sender thread, wait for it to exit, and free resources.
 */
void responder_shutdown(void);

/**
 * Callback suitable for meteo_get().
 *
 * `user_context` must be a heap-allocated `long long` containing the
 * Telegram chat ID.  Ownership is transferred — the callback frees it.
 * The weather result's strings are freed via meteo_free().
 */
void responder_weather_callback(void *user_context, CWeatherInfo wi);

#endif /* RESPONDER_H */
