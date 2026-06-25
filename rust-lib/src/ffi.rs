//! C-compatible FFI bindings for the weather library.
//!
//! # C API
//!
//! ```c
//! struct WeatherInfo {
//!     const char *message;  // weather string on success, NULL on error
//!     const char *err;      // NULL on success, error description on failure
//! };
//!
//! WeatherInfo meteo_get(const char *location);
//! void meteo_free(WeatherInfo *wi);
//! ```

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use crate::{get_current_weather, Location};

/// C-compatible weather result.
///
/// On success, `message` points to a formatted weather string and `err` is null.
/// On failure, `message` is null and `err` points to an error description.
#[repr(C)]
pub struct CWeatherInfo {
    pub message: *const c_char,
    pub err: *const c_char,
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
            message: ptr::null(),
            err: into_c_str("location pointer is null"),
        };
    }

    // Parse the location string
    let loc_str = match unsafe { CStr::from_ptr(location) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            return CWeatherInfo {
                message: ptr::null(),
                err: into_c_str(&format!("invalid UTF-8 in location string: {}", e)),
            };
        }
    };

    // Parse "latitude,longitude" format
    let (lat, lon) = match parse_coordinates(loc_str) {
        Ok(coords) => coords,
        Err(e) => {
            return CWeatherInfo {
                message: ptr::null(),
                err: into_c_str(&e),
            };
        }
    };

    let location = Location::Coordinates {
        latitude: lat,
        longitude: lon,
    };

    // Create a Tokio runtime and block on the async call
    let rt = match tokio::runtime::Runtime::new() {
        Ok(rt) => rt,
        Err(e) => {
            return CWeatherInfo {
                message: ptr::null(),
                err: into_c_str(&format!("failed to create async runtime: {}", e)),
            };
        }
    };

    match rt.block_on(get_current_weather(&location)) {
        Ok(weather) => CWeatherInfo {
            message: into_c_str(&weather.to_string()),
            err: ptr::null(),
        },
        Err(e) => CWeatherInfo {
            message: ptr::null(),
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

/// Parse a "latitude,longitude" string into (f32, f32).
fn parse_coordinates(input: &str) -> Result<(f32, f32), String> {
    let parts: Vec<&str> = input.split(',').collect();
    if parts.len() != 2 {
        return Err(format!(
            "expected location in 'latitude,longitude' format, got: '{}'",
            input
        ));
    }

    let lat: f32 = parts[0]
        .trim()
        .parse()
        .map_err(|e| format!("invalid latitude '{}': {}", parts[0], e))?;

    let lon: f32 = parts[1]
        .trim()
        .parse()
        .map_err(|e| format!("invalid longitude '{}': {}", parts[1], e))?;

    if !(-90.0..=90.0).contains(&lat) {
        return Err(format!("latitude {} out of range [-90, 90]", lat));
    }
    if !(-180.0..=180.0).contains(&lon) {
        return Err(format!("longitude {} out of range [-180, 180]", lon));
    }

    Ok((lat, lon))
}

/// Convert a Rust string into an owned `*const c_char` suitable for FFI.
/// The caller is responsible for freeing the returned pointer with `CString::from_raw`.
fn into_c_str(s: &str) -> *const c_char {
    CString::new(s)
        .unwrap_or_else(|_| CString::new("string contains null byte").unwrap())
        .into_raw()
}
