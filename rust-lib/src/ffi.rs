//! C-compatible FFI bindings for the weather library.
//!
//! # C API
//!
//! ```c
//! typedef void (*meteo_callback_t)(void *user_context, CWeatherInfo result);
//!
//! MeteoContext *meteo_init(const char *geokey);
//! void meteo_shutdown(MeteoContext *ctx);
//! void meteo_get(const MeteoContext *ctx, const char *location,
//!                meteo_callback_t callback, void *user_context);
//! void meteo_free(CWeatherInfo *wi);
//! ```

use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

use crate::{MeteoContext, parsing::parse_location, Location};

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

/// Create a new library context with the given geocoding API key and a
/// freshly-started Tokio async runtime.
///
/// The returned opaque pointer must be passed to every subsequent
/// [`meteo_get`] call and finally freed with [`meteo_shutdown`].
///
/// Returns `NULL` if the runtime could not be created.
///
/// The key can be obtained for free from <https://geocode.maps.co/>.
///
/// `geokey` may be `NULL` or an empty string, which disables city-name
/// lookups (only coordinate-based queries will work).
///
/// # Safety
///
/// `geokey` must be a valid, null-terminated C string, or `NULL`.
#[no_mangle]
pub extern "C" fn meteo_init(geokey: *const c_char) -> *mut MeteoContext {
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

    match MeteoContext::new(key) {
        Ok(ctx) => Box::into_raw(Box::new(ctx)),
        Err(_) => ptr::null_mut(),
    }
}

/// Callback type: `void (*)(void *user_context, CWeatherInfo result)`.
pub type CWeatherCallback = extern "C" fn(*mut c_void, CWeatherInfo);

/// Asynchronously fetch current weather for a location string.
///
/// Returns immediately.  A coroutine (Tokio task) runs in the background;
/// `callback` is invoked with `user_context` and the result once the fetch
/// completes (or an immediate error is detected).
///
/// # Safety
///
/// `ctx` must be a valid pointer returned by [`meteo_init`] and not yet
/// passed to [`meteo_shutdown`].
/// `location` must be a valid, null-terminated C string.
/// `callback` must be a valid function pointer and must remain callable
/// until it is invoked.
#[no_mangle]
pub extern "C" fn meteo_get(
    ctx: *const MeteoContext,
    location: *const c_char,
    callback: CWeatherCallback,
    user_context: *mut c_void,
) {
    // ── Fast-path: null / invalid arguments → synchronous callback ──────
    if ctx.is_null() {
        callback(
            user_context,
            CWeatherInfo {
                message: into_c_str("Internal error: no context"),
                err: into_c_str("meteo context pointer is null; call meteo_init() first"),
            },
        );
        return;
    }
    if location.is_null() {
        callback(
            user_context,
            CWeatherInfo {
                message: into_c_str("Internal error: invalid location"),
                err: into_c_str("location pointer is null"),
            },
        );
        return;
    }

    let ctx = unsafe { &*ctx };

    // Parse the location string — we need to own a copy for the async task.
    let loc_str = match unsafe { CStr::from_ptr(location) }.to_str() {
        Ok(s) => s.to_string(),
        Err(e) => {
            callback(
                user_context,
                CWeatherInfo {
                    message: into_c_str("Invalid location format"),
                    err: into_c_str(&format!("invalid UTF-8 in location string: {}", e)),
                },
            );
            return;
        }
    };

    let mut loc = match parse_location(&loc_str) {
        Ok(l) => l,
        Err(e) => {
            callback(
                user_context,
                CWeatherInfo {
                    message: into_c_str("Invalid location format"),
                    err: into_c_str(&e),
                },
            );
            return;
        }
    };

    // If the user typed a city name, inject the API key from the context
    if let Location::City { ref mut api_key, .. } = &mut loc {
        if api_key.is_empty() {
            *api_key = ctx.api_key().to_string();
        }
    }

    // ── Spawn a Tokio task (coroutine) for the actual fetch ─────────────
    // Cast to usize so the raw pointer does not appear in the async block's
    // generator state (raw pointers are !Send, usize is Send).
    let user_ptr = user_context as usize;
    let _guard = ctx.runtime().enter();
    tokio::spawn(async move {
        let wi = match crate::get_weather(&loc).await {
            Ok(weather) => CWeatherInfo {
                message: into_c_str(&weather.to_string()),
                err: ptr::null(),
            },
            Err(e) => CWeatherInfo {
                message: into_c_str("Failed to fetch weather data"),
                err: into_c_str(&e.to_string()),
            },
        };
        callback(user_ptr as *mut c_void, wi);
    });
}

/// Shut down the Tokio runtime and free the context created by
/// [`meteo_init`].
///
/// After this call the `ctx` pointer is invalid and must not be used again.
///
/// # Safety
///
/// `ctx` must be a valid pointer returned by [`meteo_init`] and must not
/// have been passed to this function before.  `NULL` is safe (no-op).
#[no_mangle]
pub extern "C" fn meteo_shutdown(ctx: *mut MeteoContext) {
    if ctx.is_null() {
        return;
    }
    let ctx = unsafe { Box::from_raw(ctx) };
    ctx.shutdown();
    // Box is dropped here → MeteoContext memory is freed
}

/// Free a [`CWeatherInfo`] struct delivered to a [`CWeatherCallback`].
///
/// Must be called by the callback handler exactly once on the `result`
/// argument it receives.  Only the inner strings are freed; the struct
/// itself lives on the stack.
///
/// # Safety
///
/// `wi` must be a valid pointer to a `CWeatherInfo` that was delivered
/// to a callback and not yet freed.  After this call the pointer is
/// invalid for reading.
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
