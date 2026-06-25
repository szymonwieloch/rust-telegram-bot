#ifndef METEO_H
#define METEO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque weather-info struct returned by meteo_get().
 * On success, `message` points to a human-readable weather string and `err` is NULL.
 * On failure, `message` is NULL and `err` points to an error description.
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
