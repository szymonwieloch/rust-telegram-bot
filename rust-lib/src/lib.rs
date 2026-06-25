//! A simple weather library that fetches current weather data from the
//! [Open-Meteo](https://open-meteo.com/) API.
//!
//! # Usage
//!
//! ```rust,no_run
//! use rust_telegram_bot::{get_current_weather, Location};
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

use open_meteo_api::{
    models::{CurrentWeather, OpenMeteoData},
    query::OpenMeteo,
};
use std::fmt;

/// Represents a location to query weather for.
#[derive(Debug, Clone)]
pub enum Location {
    /// Latitude and longitude coordinates.
    Coordinates { latitude: f32, longitude: f32 },
    /// A city/place name (requires a free geocoding API key from <https://geocode.maps.co/>).
    City {
        name: String,
        api_key: String,
    },
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

/// Errors that can occur when fetching weather data.
#[derive(Debug)]
pub enum WeatherError {
    /// The API did not return current weather data for the given location.
    NoCurrentWeather,
    /// An error from the underlying HTTP / API layer.
    ApiError(Box<dyn std::error::Error>),
    /// Failed to build the API query.
    QueryBuildError(Box<dyn std::error::Error>),
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
            Self::ApiError(e) => Some(e.as_ref()),
            Self::QueryBuildError(e) => Some(e.as_ref()),
            Self::NoCurrentWeather => None,
        }
    }
}

impl From<Box<dyn std::error::Error>> for WeatherError {
    fn from(e: Box<dyn std::error::Error>) -> Self {
        Self::ApiError(e)
    }
}

/// Fetch the current weather for a given [`Location`].
///
/// # Errors
///
/// Returns a [`WeatherError`] if the API call fails, the query cannot be built,
/// or no current weather data is available.
///
/// # Example
///
/// ```rust,no_run
/// use rust_telegram_bot::{get_current_weather, Location};
///
/// #[tokio::main]
/// async fn main() -> Result<(), Box<dyn std::error::Error>> {
///     let loc = Location::Coordinates {
///         latitude: 52.52,
///         longitude: 13.41,
///     };
///     let weather = get_current_weather(&loc).await?;
///     println!("{}", weather);
///     Ok(())
/// }
/// ```
pub async fn get_current_weather(location: &Location) -> Result<WeatherInfo, WeatherError> {
    let query = build_query(location)?;

    let data: OpenMeteoData = query.query().await.map_err(WeatherError::ApiError)?;

    WeatherInfo::from_open_meteo_data(&data).ok_or(WeatherError::NoCurrentWeather)
}

/// Build the `OpenMeteo` query builder from a [`Location`].
fn build_query(location: &Location) -> Result<OpenMeteo, WeatherError> {
    let builder = OpenMeteo::new();

    match location {
        Location::Coordinates { latitude, longitude } => builder
            .coordinates(*latitude, *longitude)
            .map_err(WeatherError::QueryBuildError),

        Location::City { name: _name, api_key: _api_key } => {
            // Note: `location()` is async because it geocodes the name first.
            // We handle that in a separate async function path.
            // For the City variant, use `get_current_weather` which calls
            // `build_query_city` internally.
            Err(WeatherError::QueryBuildError(
                "Use get_current_weather() for City locations (requires async geocoding)"
                    .to_string()
                    .into(),
            ))
        }
    }?
    .current_weather()
    .map_err(WeatherError::QueryBuildError)
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
            let data: OpenMeteoData = OpenMeteo::new()
                .location(name, api_key)
                .await
                .map_err(WeatherError::ApiError)?
                .current_weather()
                .map_err(WeatherError::QueryBuildError)?
                .query()
                .await
                .map_err(WeatherError::ApiError)?;

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
        assert_eq!(
            WeatherCondition::from_code(1.0),
            WeatherCondition::MainlyClear
        );
        assert_eq!(
            WeatherCondition::from_code(2.0),
            WeatherCondition::PartlyCloudy
        );
        assert_eq!(WeatherCondition::from_code(3.0), WeatherCondition::Overcast);
        assert_eq!(WeatherCondition::from_code(61.0), WeatherCondition::RainSlight);
        assert_eq!(
            WeatherCondition::from_code(95.0),
            WeatherCondition::Thunderstorm
        );
        assert!(matches!(
            WeatherCondition::from_code(999.0),
            WeatherCondition::Unknown(999.0)
        ));
    }

    #[test]
    fn test_weather_condition_display() {
        assert_eq!(WeatherCondition::ClearSky.to_string(), "Clear sky");
        assert_eq!(WeatherCondition::RainHeavy.to_string(), "Heavy rain");
        assert_eq!(
            WeatherCondition::Unknown(42.0).to_string(),
            "Unknown (code: 42)"
        );
    }
}
