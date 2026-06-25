//! Input parsing for location strings.
//!
//! Supports two formats:
//! - **City name**: any string consisting of alphabetic characters, spaces,
//!   hyphens, and apostrophes (Unicode letters, e.g. `"Kraków"`,
//!   `"New York"`, `"O'Reilly"`, `"Côte-d'Or"`).
//! - **GPS coordinates**: two comma-separated decimal numbers in the form
//!   `"latitude,longitude"`. Latitude must be in [-90, 90], longitude in
//!   [-180, 180]. Example: `"51.5074,-0.1278"`.
//!
//! Anything else is rejected as an invalid format.

use crate::Location;

/// Parse a location string into a [`Location`].
///
/// # Recognised formats
///
/// | Input pattern                          | Result                             |
/// |----------------------------------------|------------------------------------|
/// | Letters, spaces, `-`, `'` (e.g. `"O'Reilly"`) | `Location::City` (empty API key)   |
/// | `"lat,lon"` (e.g. `"51.5,-0.12"`)      | `Location::Coordinates`            |
/// | Anything else                          | `Err(“…”)`                         |
///
/// **Note:** The returned `Location::City` has an **empty** `api_key` field.
/// The caller is responsible for providing a geocoding API key before using
/// the city name with [`crate::get_weather`].
pub fn parse_location(input: &str) -> Result<Location, String> {
    let trimmed = input.trim();

    if trimmed.is_empty() {
        return Err("empty location string".to_string());
    }

    // Case 1: letters, spaces, hyphens, apostrophes → city name
    if is_alphabetic(trimmed) {
        return Ok(Location::City {
            name: trimmed.to_string(),
            api_key: String::new(),
        });
    }

    // Case 2: try parsing as "latitude,longitude"
    if let Ok(coords) = parse_coordinates(trimmed) {
        return Ok(Location::Coordinates {
            latitude: coords.0,
            longitude: coords.1,
        });
    }

    // Case 3: everything else is invalid
    Err(format!(
        "unrecognised location format: '{}'. Expected either a city name \
         (letters, spaces, hyphens, apostrophes) or coordinates in 'latitude,longitude' format",
        trimmed
    ))
}

/// Returns `true` when every character in `s` is a Unicode alphabetic letter,
/// space, hyphen (`-`), or apostrophe (`'`), and `s` contains at least one
/// letter.
fn is_alphabetic(s: &str) -> bool {
    s.chars().any(|c| c.is_alphabetic())
        && s.chars().all(|c| c.is_alphabetic() || c == ' ' || c == '-' || c == '\'')
}

/// Parse a `"latitude,longitude"` string into `(f32, f32)`.
///
/// Both values are validated against their respective geographic ranges:
/// latitude must be in [-90, 90], longitude in [-180, 180].
fn parse_coordinates(input: &str) -> Result<(f32, f32), String> {
    let parts: Vec<&str> = input.split(',').collect();

    if parts.len() != 2 {
        return Err(format!(
            "expected exactly two comma-separated values, got {} parts in '{}'",
            parts.len(),
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

#[cfg(test)]
mod tests {
    use super::*;

    // ── is_alphabetic ──────────────────────────────────────────────────

    #[test]
    fn alphabetic_simple() {
        assert!(is_alphabetic("London"));
        assert!(is_alphabetic("Tokyo"));
        assert!(is_alphabetic("a"));
        assert!(is_alphabetic("ABCdefGHI"));
    }

    #[test]
    fn alphabetic_rejects_empty() {
        assert!(!is_alphabetic(""));
    }

    #[test]
    fn alphabetic_rejects_numbers() {
        assert!(!is_alphabetic("London123"));
        assert!(!is_alphabetic("42"));
    }

    #[test]
    fn alphabetic_with_spaces() {
        assert!(is_alphabetic("New York"));
        assert!(is_alphabetic("Los Angeles"));
        assert!(is_alphabetic("Kuala Lumpur"));
        assert!(is_alphabetic("Sauðárkrókur"));
    }

    #[test]
    fn alphabetic_rejects_only_spaces() {
        assert!(!is_alphabetic("   "));
        assert!(!is_alphabetic(" "));
    }

    #[test]
    fn alphabetic_with_hyphens_and_apostrophes() {
        assert!(is_alphabetic("O'Reilly"));
        assert!(is_alphabetic("Côte-d'Or"));
        assert!(is_alphabetic("N'Djamena"));
        assert!(is_alphabetic("Wilkes-Barre"));
    }

    #[test]
    fn alphabetic_rejects_other_punctuation() {
        assert!(!is_alphabetic("City!"));
        assert!(!is_alphabetic("City?"));
        assert!(!is_alphabetic("City."));
        assert!(!is_alphabetic("12.3"));
    }

    #[test]
    fn alphabetic_with_accents() {
        assert!(is_alphabetic("Kraków"));
        assert!(is_alphabetic("München"));
        assert!(is_alphabetic("Wałbrzych"));
        assert!(is_alphabetic("Québec"));
    }

    #[test]
    fn alphabetic_cyrillic() {
        assert!(is_alphabetic("Москва"));
    }

    // ── parse_coordinates ──────────────────────────────────────────────

    #[test]
    fn coords_valid() {
        assert_eq!(parse_coordinates("51.5074,-0.1278").unwrap(), (51.5074, -0.1278));
        assert_eq!(parse_coordinates("0,0").unwrap(), (0.0, 0.0));
        assert_eq!(parse_coordinates("90,180").unwrap(), (90.0, 180.0));
        assert_eq!(parse_coordinates("-90,-180").unwrap(), (-90.0, -180.0));
    }

    #[test]
    fn coords_with_spaces() {
        assert_eq!(parse_coordinates(" 51.5 , -0.12 ").unwrap(), (51.5, -0.12));
    }

    #[test]
    fn coords_too_many_parts() {
        assert!(parse_coordinates("1,2,3").is_err());
    }

    #[test]
    fn coords_too_few_parts() {
        assert!(parse_coordinates("51.5074").is_err());
    }

    #[test]
    fn coords_lat_out_of_range() {
        assert!(parse_coordinates("91,0").is_err());
        assert!(parse_coordinates("-91,0").is_err());
    }

    #[test]
    fn coords_lon_out_of_range() {
        assert!(parse_coordinates("0,181").is_err());
        assert!(parse_coordinates("0,-181").is_err());
    }

    #[test]
    fn coords_non_numeric() {
        assert!(parse_coordinates("abc,def").is_err());
        assert!(parse_coordinates("12.3,xyz").is_err());
    }

    // ── parse_location ─────────────────────────────────────────────────

    #[test]
    fn parse_city_name() {
        let loc = parse_location("London").unwrap();
        match loc {
            Location::City { name, api_key } => {
                assert_eq!(name, "London");
                assert!(api_key.is_empty());
            }
            _ => panic!("expected City variant"),
        }
    }

    #[test]
    fn parse_city_name_accented() {
        let loc = parse_location("Kraków").unwrap();
        match loc {
            Location::City { name, .. } => assert_eq!(name, "Kraków"),
            _ => panic!("expected City variant"),
        }
    }

    #[test]
    fn parse_coordinates_via_parse_location() {
        let loc = parse_location("48.8566,2.3522").unwrap();
        match loc {
            Location::Coordinates { latitude, longitude } => {
                assert!((latitude - 48.8566).abs() < 0.001);
                assert!((longitude - 2.3522).abs() < 0.001);
            }
            _ => panic!("expected Coordinates variant"),
        }
    }

    #[test]
    fn parse_coordinates_with_negatives() {
        let loc = parse_location("-33.8688,151.2093").unwrap();
        match loc {
            Location::Coordinates { latitude, longitude } => {
                assert!((latitude - (-33.8688)).abs() < 0.001);
                assert!((longitude - 151.2093).abs() < 0.001);
            }
            _ => panic!("expected Coordinates variant"),
        }
    }

    #[test]
    fn parse_empty_string() {
        assert!(parse_location("").is_err());
        assert!(parse_location("   ").is_err());
    }

    #[test]
    fn parse_rejects_mixed() {
        // "London12" — contains digits, not alphabetic-only and not valid coords
        assert!(parse_location("London12").is_err());
        // "12.3London" — not valid coords, not alphabetic
        assert!(parse_location("12.3London").is_err());
    }

    #[test]
    fn parse_city_name_with_spaces() {
        let loc = parse_location("New York").unwrap();
        match loc {
            Location::City { name, .. } => assert_eq!(name, "New York"),
            _ => panic!("expected City variant"),
        }
    }

    #[test]
    fn parse_city_name_with_punctuation() {
        let loc = parse_location("O'Reilly").unwrap();
        match loc {
            Location::City { name, .. } => assert_eq!(name, "O'Reilly"),
            _ => panic!("expected City variant"),
        }
        let loc = parse_location("Côte-d'Or").unwrap();
        match loc {
            Location::City { name, .. } => assert_eq!(name, "Côte-d'Or"),
            _ => panic!("expected City variant"),
        }
    }

    #[test]
    fn parse_trimmed_input() {
        let loc = parse_location("  Tokyo  ").unwrap();
        match loc {
            Location::City { name, .. } => assert_eq!(name, "Tokyo"),
            _ => panic!("expected City"),
        }
    }
}
