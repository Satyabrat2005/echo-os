# echo-companion: BLE/WiFi sync to the caregiver app (alerts/status/firmware only).
add_library(echo-companion STATIC
    src/companion_sync.cpp
    # Phase 22: the caregiver boundary. transport.cpp is the ADR-9 seam (fake,
    # in-process loopback, documented BLE stub); secure_channel.cpp is the
    # encrypt-then-MAC framing; inbound.cpp is the hardened decoder for the
    # device's first-ever inbound path. All three are dependency-free: no libcurl,
    # no BLE stack, and no runtime network in CI.
    src/transport.cpp
    src/secure_channel.cpp
    src/inbound.cpp
)

target_include_directories(echo-companion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-companion
    PUBLIC  echo::common
    PRIVATE echo-project-options
)

add_library(echo::companion ALIAS echo-companion)

# --- Fuzzing the inbound decoder (Phase 22) ----------------------------------
# decode_command is the only decoder in the system whose output CHANGES DEVICE
# BEHAVIOUR — an accepted command is spoken to the wearer or written to their store —
# so it gets the same treatment the appkit JSON reader got in Phase 8, with the same
# switch (ECHO_BUILD_FUZZERS, declared at the root) and the same standalone replay
# driver. Reusing that driver rather than writing a second one is the point.
if(ECHO_BUILD_FUZZERS)
    set(ECHO_FUZZ_DRIVER ${CMAKE_SOURCE_DIR}/apps/appkit/fuzz/standalone_main.cpp)

    # 1. Replay driver — builds with ANY compiler (MSVC included), so the harness
    #    stays buildable on the dependency-free toolchain. Runs the harness over
    #    files named on argv: point it at the Phase 6 JSON corpus to seed, or at a
    #    crashing input to reproduce.
    add_executable(echo-command-fuzz-replay fuzz/command_fuzz.cpp ${ECHO_FUZZ_DRIVER})
    target_link_libraries(echo-command-fuzz-replay PRIVATE echo::companion echo-project-options)
    set_target_properties(echo-command-fuzz-replay PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)

    # 2. libFuzzer engine — coverage-guided; needs Clang's -fsanitize=fuzzer.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(echo-command-fuzzer fuzz/command_fuzz.cpp)
        target_link_libraries(echo-command-fuzzer PRIVATE echo::companion echo-project-options)
        target_compile_options(echo-command-fuzzer PRIVATE -g -O1 -fsanitize=fuzzer-no-link)
        target_link_options(echo-command-fuzzer PRIVATE -fsanitize=fuzzer)
        set_target_properties(echo-command-fuzzer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)
        message(STATUS "ECHO companion: command libFuzzer target ON (echo-command-fuzzer)")
    else()
        message(STATUS "ECHO companion: libFuzzer target skipped (needs Clang); "
                       "replay driver echo-command-fuzz-replay still built")
    endif()
endif()
