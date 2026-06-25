#ifndef METEO_H
#define METEO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Weather-info struct returned by meteo_get().
 *
 * Field semantics:
 * - `message`: Always set. Contains the formatted weather string on success,
 *   or a **user-friendly** error description on failure (suitable for
 *   displaying to an end user).
 * - `err`: Internal error details for logging. NULL on success; on failure,
 *   contains technical diagnostic information intended for the developer's
 *   logs, not for end users.
 *
 * Must be freed with meteo_free().
 */
typedef struct CWeatherInfo {
    const char *message;
    const char *err;
} CWeatherInfo;

/**
 * Fetch current weather for a location.
 *
 * @param location  A null-terminated C string in "latitude,longitude" format
 *                  (e.g. "51.5074,-0.1278" for London).
 * @return          A CWeatherInfo struct (see above).
 */
CWeatherInfo meteo_get(const char *location);

/**
 * Free a CWeatherInfo struct previously returned by meteo_get().
 *
 * @param wi  Pointer to the struct to free. May be NULL (no-op).
 */
void meteo_free(CWeatherInfo *wi);

#ifdef __cplusplus
}
#endif

#endif /* METEO_H */
