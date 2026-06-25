cmake_minimum_required(VERSION 3.19)
project(telegram-weather-bot
    VERSION 0.1.0
    DESCRIPTION "Telegram bot that reports weather using rust-lib, telebot, and APR"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# ── Dependencies ─────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)

# APR (Apache Portable Runtime)
pkg_check_modules(APR REQUIRED apr-1)
pkg_check_modules(APRUTIL REQUIRED apr-util-1)

# telebot (Telegram Bot API library from GitHub)
include(FetchContent)
FetchContent_Declare(
    telebot
    GIT_REPOSITORY  https://github.com/smartnode/telebot.git
    GIT_TAG         v9.4
)
# Use EXCLUDE_FROM_ALL so telebot's test targets aren't built
FetchContent_GetProperties(telebot)
if(NOT telebot_POPULATED)
    FetchContent_Populate(telebot)
    add_subdirectory(${telebot_SOURCE_DIR} ${telebot_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# telebot compiles with -Werror; relax unused-parameter to keep it building
# with newer compilers that flag the library's own stubs
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(telebot_static PRIVATE -Wno-error=unused-parameter -Wno-error=sign-compare)
    target_compile_options(telebot PRIVATE -Wno-error=unused-parameter -Wno-error=sign-compare)
endif()

# ── Corrosion (CMake ↔ Cargo bridge) ────────────────────────────────────────

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY  https://github.com/corrosion-rs/corrosion.git
    GIT_TAG         v0.6.1
)
FetchContent_MakeAvailable(Corrosion)

# Import the Rust crate as a proper CMake target with full dependency tracking
corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_SOURCE_DIR}/rust-lib/Cargo.toml"
)

# ── Main executable ──────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME}
    src/main.c
    src/responder.c
    src/utils.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    ${APR_INCLUDE_DIRS}
    ${APRUTIL_INCLUDE_DIRS}
    "${telebot_SOURCE_DIR}/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${APR_LIBRARIES}
    ${APRUTIL_LIBRARIES}
    telebot
    meteo
    pthread dl m
)
# Tell CMake that the Rust static lib depends on OpenSSL.
# This ensures -lssl -lcrypto appear after libmeteo.a on the link line.
set_property(TARGET meteo APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES ssl crypto
)

# Pass rpath so telebot .so can be found at runtime
target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,-rpath,${telebot_BINARY_DIR}"
)

# ── Tests ────────────────────────────────────────────────────────────────────

enable_testing()

# Rust crate tests (run via cargo test)
add_test(
    NAME rust_tests
    COMMAND cargo test --manifest-path "${CMAKE_SOURCE_DIR}/rust-lib/Cargo.toml"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# C utility tests
add_executable(test_utils
    test/abts.c
    test/testutil.c
    test/test_utils.c
    src/utils.c
)

target_include_directories(test_utils PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/test"
    ${APR_INCLUDE_DIRS}
)

target_link_libraries(test_utils PRIVATE
    ${APR_LIBRARIES}
    pthread
)

add_test(NAME utils COMMAND test_utils)
