#ifndef METEO_H
#define METEO_H

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque library context (created by meteo_init, destroyed by meteo_shutdown). */
typedef struct MeteoContext MeteoContext;

/**
 * Weather-info struct delivered to the callback.
 *
 * Field semantics:
 * - `message`: Always set. Contains the formatted weather string on success,
 *   or a **user-friendly** error description on failure (suitable for
 *   displaying to an end user).
 * - `err`: Internal error details for logging. NULL on success; on failure,
 *   contains technical diagnostic information intended for the developer's
 *   logs, not for end users.
 *
 * The callback **must** call meteo_free() on this struct to release the
 * strings it owns.
 */
typedef struct CWeatherInfo {
    const char *message;
    const char *err;
} CWeatherInfo;

/**
 * Callback type for asynchronous weather requests.
 *
 * Called exactly once per meteo_get() call when the result is ready (or an
 * immediate error has occurred).  The callback receives the opaque
 * `user_context` pointer that was passed to meteo_get() together with the
 * weather result.
 *
 * @param user_context  Opaque pointer passed through from meteo_get().
 * @param result        Weather result (or error description).  The callback
 *                      MUST call meteo_free(&result) to release owned strings.
 */
typedef void (*meteo_callback_t)(void *user_context, struct CWeatherInfo result);

/**
 * Create a new library context with the given geocoding API key and a
 * freshly-started Tokio async runtime.
 *
 * The returned opaque pointer must be passed to every subsequent meteo_get()
 * call and finally freed with meteo_shutdown().
 *
 * The key can be obtained for free from https://geocode.maps.co/.
 *
 * @param geokey  A null-terminated C string containing the API key.
 *                May be NULL or empty to disable city-name lookups.
 * @return        Opaque context pointer, or NULL if initialisation failed.
 */
MeteoContext *meteo_init(const char *geokey);

/**
 * Shut down the Tokio runtime and free the context created by meteo_init().
 *
 * After this call the pointer is invalid and must not be used again.
 *
 * @param ctx  Context pointer from meteo_init().  NULL is safe (no-op).
 */
void meteo_shutdown(MeteoContext *ctx);

/**
 * Asynchronously fetch current weather for a location.
 *
 * The call returns immediately.  A coroutine runs in the background to
 * perform the fetch; once the result is ready (or an error is encountered)
 * `callback` is invoked with `user_context` and the `CWeatherInfo` result.
 *
 * The callback may be invoked synchronously from within meteo_get() for
 * immediate / trivial errors (e.g. NULL arguments).  In the normal case
 * the callback runs on a Tokio worker thread after the HTTP round-trip
 * completes.
 *
 * @param ctx           Context pointer from meteo_init().
 * @param location      A null-terminated C string in either format:
 *                      - "latitude,longitude" (e.g. "51.5074,-0.1278")
 *                      - a city name (e.g. "London") — requires a geocoding
 *                        key in the context
 * @param callback      Function to call with the result.
 * @param user_context  Opaque pointer forwarded to `callback`.  May be NULL.
 */
void meteo_get(const MeteoContext *ctx, const char *location,
               meteo_callback_t callback, void *user_context);

/**
 * Free a CWeatherInfo struct — must be called by the meteo_callback_t
 * handler on the `result` argument it receives.
 *
 * @param wi  Pointer to the struct to free.  May be NULL (no-op).
 */
void meteo_free(CWeatherInfo *wi);

#ifdef __cplusplus
}
#endif

#endif /* METEO_H */
