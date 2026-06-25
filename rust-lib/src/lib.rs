//! A simple weather library that fetches current weather data from the
//! [Open-Meteo](https://open-meteo.com/) API.
//!
//! # Usage
//!
//! ```rust,no_run
//! use meteo::{get_current_weather, Location};
//!
//! #[tokio::main]
//! async fn main() -> Result<(), Box<dyn std::error::Error>> {
//!     let location = Location::Coordinates {
//!         latitude: 51.5074,
//!         longitude: -0.1278,
//!     };
//!     let weather = get_current_weather(&location).await?;
//!     println!("{}", weather);
//!     Ok(())
//! }
//! ```

pub mod ffi;
pub mod parsing;

use open_meteo_api::{
    models::{CurrentWeather, OpenMeteoData},
    query::OpenMeteo,
};
use std::fmt;
use std::future::Future;

/// Opaque context holding the geocoding API key and Tokio async runtime.
///
/// Created via [`MeteoContext::new`] and passed to all FFI functions.
/// The C side treats this as an opaque pointer.
pub struct MeteoContext {
    geocoding_api_key: String,
    runtime: tokio::runtime::Runtime,
}

impl MeteoContext {
    /// Create a new context with the given geocoding API key.
    ///
    /// `geokey` may be empty to disable city-name lookups.
    ///
    /// # Errors
    ///
    /// Returns an error message if the Tokio runtime could not be created.
    pub fn new(geokey: String) -> Result<Self, String> {
        let runtime = tokio::runtime::Runtime::new()
            .map_err(|e| format!("failed to create tokio runtime: {}", e))?;
        Ok(Self { geocoding_api_key: geokey, runtime })
    }

    /// The stored geocoding API key (may be empty).
    pub fn api_key(&self) -> &str {
        &self.geocoding_api_key
    }

    /// Borrow the underlying Tokio runtime (for `enter()` / spawning).
    pub fn runtime(&self) -> &tokio::runtime::Runtime {
        &self.runtime
    }

    /// Execute an async future on this context's runtime, blocking the
    /// current thread until completion.
    pub fn block_on<F: Future>(&self, future: F) -> F::Output {
        self.runtime.block_on(future)
    }

    /// Shut down the Tokio runtime and consume the context.
    pub fn shutdown(self) {
        self.runtime.shutdown_background();
    }
}

/// Represents a location to query weather for.
#[derive(Debug, Clone)]
pub enum Location {
    /// Latitude and longitude coordinates.
    Coordinates { latitude: f32, longitude: f32 },
    /// A city/place name (requires a free geocoding API key from <https://geocode.maps.co/>).
    City { name: String, api_key: String },
}

/// A human-readable description of a WMO weather code.
#[derive(Debug, Clone, PartialEq)]
pub enum WeatherCondition {
    ClearSky,
    MainlyClear,
    PartlyCloudy,
    Overcast,
    Fog,
    DrizzleLight,
    DrizzleModerate,
    DrizzleDense,
    RainSlight,
    RainModerate,
    RainHeavy,
    SnowSlight,
    SnowModerate,
    SnowHeavy,
    RainShowersSlight,
    RainShowersModerate,
    RainShowersViolent,
    SnowShowersSlight,
    SnowShowersHeavy,
    Thunderstorm,
    ThunderstormWithHail,
    Unknown(f32),
}

impl WeatherCondition {
    /// Convert a WMO weather code (f32) into a `WeatherCondition`.
    pub fn from_code(code: f32) -> Self {
        match code as i32 {
            0 => Self::ClearSky,
            1 => Self::MainlyClear,
            2 => Self::PartlyCloudy,
            3 => Self::Overcast,
            45 => Self::Fog,
            48 => Self::Fog,
            51 => Self::DrizzleLight,
            53 => Self::DrizzleModerate,
            55 => Self::DrizzleDense,
            61 => Self::RainSlight,
            63 => Self::RainModerate,
            65 => Self::RainHeavy,
            71 => Self::SnowSlight,
            73 => Self::SnowModerate,
            75 => Self::SnowHeavy,
            80 => Self::RainShowersSlight,
            81 => Self::RainShowersModerate,
            82 => Self::RainShowersViolent,
            85 => Self::SnowShowersSlight,
            86 => Self::SnowShowersHeavy,
            95 => Self::Thunderstorm,
            96 => Self::ThunderstormWithHail,
            99 => Self::ThunderstormWithHail,
            _ => Self::Unknown(code),
        }
    }
}

impl fmt::Display for WeatherCondition {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ClearSky => write!(f, "Clear sky"),
            Self::MainlyClear => write!(f, "Mainly clear"),
            Self::PartlyCloudy => write!(f, "Partly cloudy"),
            Self::Overcast => write!(f, "Overcast"),
            Self::Fog => write!(f, "Fog"),
            Self::DrizzleLight => write!(f, "Light drizzle"),
            Self::DrizzleModerate => write!(f, "Moderate drizzle"),
            Self::DrizzleDense => write!(f, "Dense drizzle"),
            Self::RainSlight => write!(f, "Slight rain"),
            Self::RainModerate => write!(f, "Moderate rain"),
            Self::RainHeavy => write!(f, "Heavy rain"),
            Self::SnowSlight => write!(f, "Slight snow"),
            Self::SnowModerate => write!(f, "Moderate snow"),
            Self::SnowHeavy => write!(f, "Heavy snow"),
            Self::RainShowersSlight => write!(f, "Slight rain showers"),
            Self::RainShowersModerate => write!(f, "Moderate rain showers"),
            Self::RainShowersViolent => write!(f, "Violent rain showers"),
            Self::SnowShowersSlight => write!(f, "Slight snow showers"),
            Self::SnowShowersHeavy => write!(f, "Heavy snow showers"),
            Self::Thunderstorm => write!(f, "Thunderstorm"),
            Self::ThunderstormWithHail => write!(f, "Thunderstorm with hail"),
            Self::Unknown(code) => write!(f, "Unknown (code: {})", code),
        }
    }
}

/// Processed, easy-to-use current weather information.
#[derive(Debug, Clone)]
pub struct WeatherInfo {
    /// Temperature in °C.
    pub temperature: f32,
    /// Wind speed in km/h.
    pub wind_speed: f32,
    /// Wind direction in degrees (0–360).
    pub wind_direction: f32,
    /// Human-readable weather condition.
    pub condition: WeatherCondition,
    /// Raw WMO weather code.
    pub weather_code: f32,
    /// Whether it is currently daytime.
    pub is_day: bool,
    /// Observation time (ISO 8601).
    pub time: String,
    /// Latitude of the location.
    pub latitude: f32,
    /// Longitude of the location.
    pub longitude: f32,
    /// Timezone name (e.g. "Europe/London").
    pub timezone: String,
}

impl WeatherInfo {
    /// Build a `WeatherInfo` from the raw API response.
    fn from_open_meteo_data(data: &OpenMeteoData) -> Option<Self> {
        let cw: &CurrentWeather = data.current_weather.as_ref()?;

        Some(Self {
            temperature: cw.temperature,
            wind_speed: cw.windspeed,
            wind_direction: cw.winddirection,
            condition: WeatherCondition::from_code(cw.weathercode),
            weather_code: cw.weathercode,
            is_day: cw.is_day > 0.5,
            time: cw.time.clone(),
            latitude: data.latitude,
            longitude: data.longitude,
            timezone: data.timezone.clone(),
        })
    }
}

impl fmt::Display for WeatherInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let day_night = if self.is_day { "☀️ Day" } else { "🌙 Night" };
        write!(
            f,
            "📍 ({:.4}, {:.4}) — {}\n\
             🌡️  {:.1} °C\n\
             💨 {:.1} km/h from {:.0}°\n\
             🌤️  {}\n\
             🕐 {} | {}",
            self.latitude,
            self.longitude,
            self.timezone,
            self.temperature,
            self.wind_speed,
            self.wind_direction,
            self.condition,
            self.time,
            day_night,
        )
    }
}

/// Private, always-`Send + Sync` error type for string messages.
#[derive(Debug)]
struct StrError(String);

impl fmt::Display for StrError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for StrError {}

/// A `Send + Sync` wrapper for boxed errors originating from async I/O.
///
/// Unlike a plain `Box<dyn Error>` this is guaranteed to be `Send + Sync`
/// at the type level, so [`WeatherError`] can cross `.await` points and be
/// spawned into Tokio tasks without `unsafe`.
#[derive(Debug)]
pub struct BoxedError(Box<dyn std::error::Error + Send + Sync>);

impl BoxedError {
    /// Construct a `BoxedError` from a plain string message (always `Send + Sync`).
    pub(crate) fn msg(s: impl Into<String>) -> Self {
        Self(Box::new(StrError(s.into())))
    }

    /// Convert a `Box<dyn Error>` into a `BoxedError`.
    ///
    /// # Safety
    ///
    /// The caller must ensure the concrete error type inside the box is
    /// `Send + Sync`.  Every error source used in this crate (`reqwest`,
    /// `serde_json`, `open-meteo-api`) satisfies this, so all internal
    /// call-sites are sound.  This method is `pub(crate)` to prevent
    /// external callers from accidentally violating the invariant.
    pub(crate) unsafe fn from_box_unchecked(e: Box<dyn std::error::Error>) -> Self {
        // SAFETY: `Box<dyn Error>` and `Box<dyn Error + Send + Sync>` share
        // the same layout (data ptr + vtable ptr).  The caller guarantees the
        // concrete error type satisfies `Send + Sync`, so the vtable for the
        // widened trait object is compatible at runtime.
        Self(std::mem::transmute::<
            Box<dyn std::error::Error>,
            Box<dyn std::error::Error + Send + Sync>,
        >(e))
    }
}

impl fmt::Display for BoxedError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl std::error::Error for BoxedError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        self.0.source()
    }
}

/// Errors that can occur when fetching weather data.
#[derive(Debug)]
pub enum WeatherError {
    /// The API did not return current weather data for the given location.
    NoCurrentWeather,
    /// An error from the underlying HTTP / API layer.
    ApiError(BoxedError),
    /// Failed to build the API query.
    QueryBuildError(BoxedError),
}

impl fmt::Display for WeatherError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NoCurrentWeather => {
                write!(f, "No current weather data available for this location")
            }
            Self::ApiError(e) => write!(f, "API error: {}", e),
            Self::QueryBuildError(e) => write!(f, "Query build error: {}", e),
        }
    }
}

impl std::error::Error for WeatherError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::ApiError(e) => Some(e),
            Self::QueryBuildError(e) => Some(e),
            Self::NoCurrentWeather => None,
        }
    }
}

impl From<Box<dyn std::error::Error + Send + Sync>> for WeatherError {
    fn from(e: Box<dyn std::error::Error + Send + Sync>) -> Self {
        Self::ApiError(BoxedError(e))
    }
}

/// Fetch the current weather for a given [`Location`].
///
/// Only handles [`Location::Coordinates`]; for city names use [`get_weather`].
///
/// # Errors
///
/// Returns a [`WeatherError`] if the API call fails, the query cannot be built,
/// or no current weather data is available.
pub async fn get_current_weather(location: &Location) -> Result<WeatherInfo, WeatherError> {
    let (lat, lon) = match location {
        Location::Coordinates { latitude, longitude } => (*latitude, *longitude),
        Location::City { .. } => {
            return Err(WeatherError::QueryBuildError(BoxedError::msg(
                "get_current_weather does not support city names; use get_weather()",
            )));
        }
    };

    let query = OpenMeteo::new()
        .coordinates(lat, lon)
        .map_err(|e| WeatherError::QueryBuildError(unsafe { BoxedError::from_box_unchecked(e) }))?
        .current_weather()
        .map_err(|e| WeatherError::QueryBuildError(unsafe { BoxedError::from_box_unchecked(e) }))?;

    let data: OpenMeteoData = query
        .query()
        .await
        .map_err(|e| WeatherError::ApiError(unsafe { BoxedError::from_box_unchecked(e) }))?;

    WeatherInfo::from_open_meteo_data(&data).ok_or(WeatherError::NoCurrentWeather)
}

/// Fetch the current weather for a given [`Location`], including city-name-based
/// lookups (which require an async geocoding step).
///
/// This is the recommended entry point. It handles both coordinate-based and
/// city-name-based locations.
///
/// # Errors
///
/// Returns a [`WeatherError`] if the API call or geocoding fails.
pub async fn get_weather(location: &Location) -> Result<WeatherInfo, WeatherError> {
    match location {
        Location::Coordinates { .. } => get_current_weather(location).await,
        Location::City { name, api_key } => {
            if api_key.is_empty() {
                return Err(WeatherError::QueryBuildError(BoxedError::msg(
                    "City name lookups require a geocoding API key. \
                     Call meteo_init() first or use coordinates.",
                )));
            }

            let data: OpenMeteoData = OpenMeteo::new()
                .location(name, api_key)
                .await
                .map_err(|e| WeatherError::ApiError(unsafe { BoxedError::from_box_unchecked(e) }))?
                .current_weather()
                .map_err(|e| {
                    WeatherError::QueryBuildError(unsafe { BoxedError::from_box_unchecked(e) })
                })?
                .query()
                .await
                .map_err(|e| {
                    WeatherError::ApiError(unsafe { BoxedError::from_box_unchecked(e) })
                })?;

            WeatherInfo::from_open_meteo_data(&data).ok_or(WeatherError::NoCurrentWeather)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_weather_condition_from_code() {
        assert_eq!(WeatherCondition::from_code(0.0), WeatherCondition::ClearSky);
        assert_eq!(WeatherCondition::from_code(1.0), WeatherCondition::MainlyClear);
        assert_eq!(WeatherCondition::from_code(2.0), WeatherCondition::PartlyCloudy);
        assert_eq!(WeatherCondition::from_code(3.0), WeatherCondition::Overcast);
        assert_eq!(WeatherCondition::from_code(61.0), WeatherCondition::RainSlight);
        assert_eq!(WeatherCondition::from_code(95.0), WeatherCondition::Thunderstorm);
        assert!(matches!(WeatherCondition::from_code(999.0), WeatherCondition::Unknown(999.0)));
    }

    #[test]
    fn test_weather_condition_display() {
        assert_eq!(WeatherCondition::ClearSky.to_string(), "Clear sky");
        assert_eq!(WeatherCondition::RainHeavy.to_string(), "Heavy rain");
        assert_eq!(WeatherCondition::Unknown(42.0).to_string(), "Unknown (code: 42)");
    }
}
