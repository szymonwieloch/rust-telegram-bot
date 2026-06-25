//! C-compatible FFI bindings for the weather library.
//!
//! # C API
//!
//! ```c
//! struct WeatherInfo {
//!     const char *message;  // user-facing message (weather string on success,
//!                           // user-friendly error description on failure)
//!     const char *err;      // NULL on success, internal error details on failure
//!                           // (intended for logging, not shown to end users)
//! };
//!
//! void meteo_init(const char *geokey);
//! WeatherInfo meteo_get(const char *location);
//! void meteo_free(WeatherInfo *wi);
//! ```

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use crate::{get_weather, init_geocoding_key, parsing::parse_location};

/// C-compatible weather result.
///
/// # Field semantics
///
/// - `message`: Always set. On success, contains the formatted weather string.
///   On failure, contains a **user-friendly** error description suitable for
///   displaying to an end user.
/// - `err`: Internal error details for logging. `NULL` on success; on failure,
///   contains technical diagnostic information (e.g. the underlying API error,
///   parse failure details) intended for the developer's logs.
#[repr(C)]
pub struct CWeatherInfo {
    pub message: *const c_char,
    pub err: *const c_char,
}

/// Set the geocoding API key for city-name lookups.
///
/// Must be called before any city-name-based weather queries.  The key can be
/// obtained for free from <https://geocode.maps.co/>.
///
/// `geokey` may be `NULL` or an empty string, which disables city-name
/// lookups (only coordinate-based queries will work).
///
/// # Safety
///
/// `geokey` must be a valid, null-terminated C string, or `NULL`.
#[no_mangle]
pub extern "C" fn meteo_init(geokey: *const c_char) {
    let key = if geokey.is_null() {
        String::new()
    } else {
        match unsafe { CStr::from_ptr(geokey) }.to_str() {
            Ok(s) => s.to_string(),
            Err(_) => {
                // Non-UTF-8 key → treat as if none was provided.
                String::new()
            }
        }
    };

    init_geocoding_key(key);
}

/// Fetch current weather for a location string.
///
/// The `location` parameter must be a null-terminated C string in the format
/// `"latitude,longitude"` (e.g. `"51.5074,-0.1278"` for London).
///
/// Returns a [`CWeatherInfo`] struct. The caller **must** free it with
/// [`meteo_free`] to avoid memory leaks.
///
/// # Safety
///
/// `location` must be a valid, null-terminated C string.
#[no_mangle]
pub extern "C" fn meteo_get(location: *const c_char) -> CWeatherInfo {
    // Guard against null pointer
    if location.is_null() {
        return CWeatherInfo {
            message: into_c_str("Internal error: invalid location"),
            err: into_c_str("location pointer is null"),
        };
    }

    // Parse the location string
    let loc_str = match unsafe { CStr::from_ptr(location) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            return CWeatherInfo {
                message: into_c_str("Invalid location format"),
                err: into_c_str(&format!("invalid UTF-8 in location string: {}", e)),
            };
        }
    };

    let location = match parse_location(loc_str) {
        Ok(loc) => loc,
        Err(e) => {
            return CWeatherInfo {
                message: into_c_str("Invalid location format"),
                err: into_c_str(&e),
            };
        }
    };

    // Create a Tokio runtime and block on the async call
    let rt = match tokio::runtime::Runtime::new() {
        Ok(rt) => rt,
        Err(e) => {
            return CWeatherInfo {
                message: into_c_str("Internal error"),
                err: into_c_str(&format!("failed to create async runtime: {}", e)),
            };
        }
    };

    match rt.block_on(get_weather(&location)) {
        Ok(weather) => CWeatherInfo {
            message: into_c_str(&weather.to_string()),
            err: ptr::null(),
        },
        Err(e) => CWeatherInfo {
            message: into_c_str("Failed to fetch weather data"),
            err: into_c_str(&e.to_string()),
        },
    }
}

/// Free a [`CWeatherInfo`] struct previously returned by [`meteo_get`].
///
/// # Safety
///
/// `wi` must be a valid pointer returned by [`meteo_get`], and must not have
/// been freed already. After this call the pointer is invalid.
#[no_mangle]
pub extern "C" fn meteo_free(wi: *mut CWeatherInfo) {
    if wi.is_null() {
        return;
    }

    unsafe {
        let wi = &mut *wi;

        // Free the C strings if they were allocated
        if !wi.message.is_null() {
            let _ = CString::from_raw(wi.message as *mut c_char);
        }
        if !wi.err.is_null() {
            let _ = CString::from_raw(wi.err as *mut c_char);
        }

        // Convert back to Box to free the struct itself
        // Note: we can't drop through Box here since the struct was
        // returned by value. The caller allocates it on their stack.
        // We only need to free the inner strings — the struct itself
        // is managed by the caller.
    }
}

/// Convert a Rust string into an owned `*const c_char` suitable for FFI.
/// The caller is responsible for freeing the returned pointer with `CString::from_raw`.
fn into_c_str(s: &str) -> *const c_char {
    CString::new(s)
        .unwrap_or_else(|_| CString::new("string contains null byte").unwrap())
        .into_raw()
}
