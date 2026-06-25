# 🌤️ telegram-weather-bot

A **Telegram bot** that reports the current weather for any location — by
coordinates or city name. Built with **C** and **Rust**, bridged via FFI.

## Architecture

```
User → Telegram → telebot (C) → meteo (Rust/FFI) → Open-Meteo API
                                         ↘
                                     geocode.maps.co (city → coords)
```

| Layer | Language | Library |
|-------|----------|---------|
| Bot framework | C | [`telebot`][telebot] — Telegram Bot API client |
| Portable runtime | C | [APR][apr] / [APR-Util][apr-util] — memory pools, threading, queues |
| Weather fetching | Rust | [`open-meteo-api`][open-meteo] — free, no-key weather data |
| Geocoding | Rust | [`geocode.maps.co`][geocode] — city-name → coordinates |
| Build glue | CMake | [`Corrosion`][corrosion] — CMake ↔ Cargo bridge |

[telebot]: https://github.com/smartnode/telebot
[apr]: https://apr.apache.org/
[apr-util]: https://apr.apache.org/
[open-meteo]: https://open-meteo.com/
[geocode]: https://geocode.maps.co/
[corrosion]: https://github.com/corrosion-rs/corrosion

### Data flow (async)

Weather requests are **non-blocking**: the Rust library spawns a Tokio task for
each request, then invokes a C callback. A dedicated sender thread (APR queue)
serialises Telegram message delivery so the Tokio runtime never touches the
telebot socket.

```
User message → polling loop → meteo_get() [returns immediately]
                                  │
                           Tokio worker thread
                           (HTTP fetch + callback)
                                  │
                    responder_weather_callback()
                                  │
                           apr_queue_push()
                                  │
                        sender thread: apr_queue_pop()
                                  │
                        telebot_send_message() → User
```

## Features

- **Coordinates**: `51.5074,-0.1278` — direct Open-Meteo query.
- **City names**: `London`, `Tokyo`, `Warszawa` — geocoded via
  [geocode.maps.co][geocode] (free API key required).
- **Async I/O**: Tokio-backed Rust library; the C side never blocks on HTTP.
- **Graceful shutdown**: SIGINT/SIGTERM clean up the Tokio runtime, APR pool,
  and sender thread.
- **Tested**: unit tests for utility functions (APR-based test harness).

## Prerequisites

| Dependency | Purpose |
|-----------|---------|
| **CMake** ≥ 3.19 | Build system |
| **C compiler** (GCC/Clang, C11) | Compile the C part |
| **Rust toolchain** (stable) | Compile the Rust library |
| **APR + APR-Util** | Memory pools, queues, threads |
| **libcurl** | HTTP client (telebot dependency) |
| **json-c** | JSON parser (telebot dependency) |
| **OpenSSL** | HTTPS in telebot and the Rust library |
| **pkg-config** | Locate system dependencies |

### Install on Debian/Ubuntu

```bash
sudo apt install cmake gcc libapr1-dev libaprutil1-dev libcurl4-openssl-dev libjson-c-dev libssl-dev pkg-config
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Install on Fedora

```bash
sudo dnf install cmake gcc apr-devel apr-util-devel libcurl-devel json-c-devel openssl-devel pkg-config
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

## Build

```bash
cmake -B build
cmake --build build -j$(nproc)
```

The binary is at `build/telegram-weather-bot`.

## Run

```bash
./build/telegram-weather-bot \
    --token  "YOUR_TELEGRAM_BOT_TOKEN" \
    --geokey "YOUR_GEOCODE_MAPS_CO_KEY"   # optional
```

| Option | Required | Description |
|--------|----------|-------------|
| `-t`, `--token` | **yes** | Bot token from [@BotFather](https://t.me/BotFather) |
| `-k`, `--geokey` | no | Free API key from [geocode.maps.co](https://geocode.maps.co/). Without it, only coordinate lookups work. |
| `-h`, `--help` | — | Show usage. |

### Example

```
./build/telegram-weather-bot -t 123456:ABC-DEF1234gh -k 6a3d58af9ddc...
```

Then in Telegram, message your bot:

```
/start                → welcome message
51.5074,-0.1278       → weather in London (coordinates)
Tokyo                 → weather in Tokyo (city name, needs geokey)
```

## Running tests

```bash
cmake -B build
cmake --build build -j$(nproc) --target test_utils
cd build && ctest --output-on-failure
```

## Project structure

```
.
├── CMakeLists.txt          # Top-level build (CMake + Corrosion)
├── run.sh                  # Convenience launcher
├── src/
│   ├── main.c              # Entry point, CLI, polling loop
│   ├── responder.c/h       # Async response queue + sender thread
│   └── utils.c/h           # String helpers
├── include/
│   └── meteo.h             # Rust-FFI header (generated manually)
├── rust-lib/
│   ├── Cargo.toml          # Rust crate (staticlib + rlib)
│   └── src/
│       ├── lib.rs          # Weather logic, Location enum, formatting
│       ├── ffi.rs          # #[no_mangle] C bindings
│       └── parsing.rs      # Coordinate / city-name parsing
└── test/
    ├── abts.c/h            # APR-based test harness (from httpd)
    ├── testutil.c/h        # Test utilities
    └── test_utils.c        # Unit tests for utils.c
```

## License

[MIT](LICENSE) © 2026 Szymon Wieloch