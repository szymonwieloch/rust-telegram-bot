cmake_minimum_required(VERSION 3.16)
project(telegram-weather-bot
    VERSION 0.1.0
    DESCRIPTION "Telegram bot that reports weather using rust-lib, telebot, and APR"
    LANGUAGES C
)

# Allow FetchContent_Populate for add_subdirectory(EXCLUDE_FROM_ALL) pattern
cmake_policy(SET CMP0169 OLD)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# ── Dependencies ─────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)

# APR (Apache Portable Runtime)
pkg_check_modules(APR REQUIRED apr-1)

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

# ── rust-lib (custom cargo build) ────────────────────────────────────────────

set(RUST_LIB_DIR  "${CMAKE_SOURCE_DIR}/rust-lib")
set(RUST_LIB_A    "${RUST_LIB_DIR}/target/debug/libmeteo.a")

add_custom_command(
    OUTPUT  "${RUST_LIB_A}"
    COMMAND cargo build
    WORKING_DIRECTORY "${RUST_LIB_DIR}"
    COMMENT "Building rust-lib with Cargo..."
)
add_custom_target(rust_lib_target DEPENDS "${RUST_LIB_A}")

# ── Main executable ──────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME}
    src/main.c
)

# Make sure rust-lib is built before linking
add_dependencies(${PROJECT_NAME} rust_lib_target)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    ${APR_INCLUDE_DIRS}
    "${telebot_SOURCE_DIR}/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${APR_LIBRARIES}
    telebot
    "${RUST_LIB_A}"
    # Transitive deps of the Rust static library
    ssl crypto pthread dl m
)

# Pass rpath so telebot .so can be found at runtime
target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,-rpath,${telebot_BINARY_DIR}"
)
