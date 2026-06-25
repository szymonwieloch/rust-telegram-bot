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
 * Set the geocoding API key for city-name lookups.
 *
 * Must be called before any city-name-based weather queries.  The key can be
 * obtained for free from https://geocode.maps.co/.
 *
 * @param geokey  A null-terminated C string containing the API key.
 *                May be NULL or empty to disable city-name lookups.
 */
void meteo_init(const char *geokey);

/**
 * Fetch current weather for a location.
 *
 * @param location  A null-terminated C string in either format:
 *                  - "latitude,longitude" (e.g. "51.5074,-0.1278" for London)
 *                  - a city name (e.g. "London") — requires meteo_init() first
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
