# ECHO OS — top-level build.
#
# The on-device software runtime for ECHO smart glasses. Runs on a stripped-down
# embedded Linux base and provides a custom runtime, voice-first UI shell, and
# local AI pipeline layered on top.
#
# Every module is a static library; boot/ produces the `echo-os` binary that
# wires them into the sub-120ms perception-to-response loop. Everything here is
# self-contained (no external dependencies) so the scaffold compiles cleanly with
# stub logic on any host toolchain, and cross-compiles to the target unchanged.
cmake_minimum_required(VERSION 3.16)

project(echo-os
    VERSION 0.1.0
    DESCRIPTION "On-device software runtime for ECHO smart glasses"
    # C is enabled for the single vendored C translation unit (SQLite amalgamation,
    # memory/vendor/sqlite3/sqlite3.c). All first-party code is C++.
    LANGUAGES CXX C
)

# --- Project-wide options carrier -------------------------------------------
# An INTERFACE target that centralizes the C++ standard and warning policy. Every
# module links it, so settings live in exactly one place.
add_library(echo-project-options INTERFACE)
target_compile_features(echo-project-options INTERFACE cxx_std_17)

if(MSVC)
    target_compile_options(echo-project-options INTERFACE /W4 /permissive-)
else()
    target_compile_options(echo-project-options INTERFACE
        -Wall -Wextra -Wpedantic)
endif()

# --- Code coverage (Phase 8) -------------------------------------------------
# Instrument every first-party target (they all link echo-project-options) with
# gcov/llvm-cov counters. OFF by default so normal and CI stub builds are
# unaffected — coverage is a dedicated CI job, never a local-dev requirement
# (constraint #1). See the CI "coverage" job and the README "Code Quality"
# section.
option(ECHO_COVERAGE "Instrument first-party targets for gcov/llvm-cov coverage" OFF)
if(ECHO_COVERAGE)
    if(MSVC)
        message(WARNING "ECHO_COVERAGE has no effect with MSVC; use a gcc/clang build")
    else()
        target_compile_options(echo-project-options INTERFACE --coverage -O0 -g)
        target_link_options(echo-project-options INTERFACE --coverage)
        message(STATUS "ECHO OS: coverage instrumentation ON (--coverage)")
    endif()
endif()

# Default to a release-ish build if the caller didn't choose (embedded target
# cares about size/latency).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Phase 3: real local-AI engine toggles ----------------------------------
# Defines the ECHO_WITH_* options and resolves each enabled engine into an
# echo::dep-* target. All default OFF, so the line below is a no-op for the
# dependency-free stub build; see cmake/echo_ai.cmake and README "Phase 3".
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(cmake/echo_ai.cmake)

# --- Modules (build order follows the dependency graph) ---------------------
add_subdirectory(common)          # shared vocabulary
add_subdirectory(sensor-pipeline) # real-time capture + lock-free buffering
add_subdirectory(memory)          # on-device person/reminder/event store (Phase 15)
add_subdirectory(perception)      # face/object/wake-word/ASR
add_subdirectory(cognitive-core)  # LLM orchestration + safe-mode + memory recall
add_subdirectory(voice-ui)        # TTS + audio-first shell
add_subdirectory(companion-sync)  # BLE/WiFi to caregiver app
add_subdirectory(power-mgmt)      # DVFS / duty-cycle scheduling
add_subdirectory(boot)            # init sequence + runtime + `echo-os` binary

# --- Tests ------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Apps layer (optional, on by default) -----------------------------------
# A voice-first application layer that runs in its own processes ON TOP of the
# core. It links the core's public interfaces (common, voice-ui) but never
# touches the safety loop's internals or its latency budget. Off-switch provided
# so the core can be built in isolation.
option(ECHO_BUILD_APPS "Build the voice-first apps layer (apps/)" ON)
if(ECHO_BUILD_APPS)
    set(ECHO_APPS_IN_TREE ON)
    add_subdirectory(apps)
endif()

message(STATUS "ECHO OS ${PROJECT_VERSION} configured (${CMAKE_BUILD_TYPE})")
