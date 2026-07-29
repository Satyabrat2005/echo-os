# ECHO OS — appkit: the shared toolkit every real (non-mock) backend links.
#
# Contains the ONE network boundary (IHttpClient), a minimal JSON reader, URL
# encoding, credential/.env loading, the confirm-before-action gate, and headless
# readability extraction. Everything here except the libcurl transport is
# dependency-free and unit-tested, so the stub/CI build stays deterministic and
# credential-free.
#
# Network transport is opt-in:
#   -DECHO_WITH_NETWORK=ON   compile the libcurl-backed IHttpClient (needs libcurl)
#   (default OFF)            make_default_http_client() returns nullptr; real
#                            backends report Unavailable and degrade gracefully.
option(ECHO_WITH_NETWORK "Compile the real libcurl HTTP transport for app backends" OFF)

add_library(echo-appkit STATIC
    src/net/http.cpp
    src/net/url.cpp
    src/net/json.cpp
    src/net/base64.cpp
    src/config/credentials.cpp
    src/config/token_store.cpp
    src/confirm/confirmation.cpp
    src/text/readable.cpp
)
target_include_directories(echo-appkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(echo-appkit PRIVATE echo-project-options)
add_library(echo::appkit ALIAS echo-appkit)

if(ECHO_WITH_NETWORK)
    find_package(CURL REQUIRED)
    target_link_libraries(echo-appkit PRIVATE CURL::libcurl)
    target_compile_definitions(echo-appkit PUBLIC ECHO_WITH_NETWORK=1)
    message(STATUS "ECHO appkit: real network transport ON (libcurl)")
else()
    message(STATUS "ECHO appkit: network transport OFF (mock/stub build)")
endif()

# Dependency-free unit tests for the testable core (JSON, URL, credentials,
# confirmation gate, readability). No network — runs in CI.
add_executable(echo-appkit-tests tests/appkit_tests.cpp)
target_link_libraries(echo-appkit-tests PRIVATE echo::appkit echo-project-options)
add_test(NAME echo-appkit-tests COMMAND echo-appkit-tests)

# --- Fuzzing (Phase 8) -------------------------------------------------------
# A libFuzzer harness for Json::parse — the one parser that eats untrusted network
# bytes and already had a crash bug (the Phase 6 deep-nesting stack overflow). OFF
# by default: fuzzing is an extra CI/dev tool, never a requirement for a normal
# build, so the stub build stays zero-dependency (constraint #1). See fuzz/README.md.
option(ECHO_BUILD_FUZZERS "Build the JSON fuzz target(s) (see apps/appkit/fuzz/)" OFF)
if(ECHO_BUILD_FUZZERS)
    # 1. Standalone replay driver — builds with ANY compiler (g++/MSVC included).
    #    Runs the harness over files named on argv: reproduce a crash or replay a
    #    corpus without libFuzzer. This is what keeps the harness buildable on the
    #    dependency-free toolchain.
    add_executable(echo-json-fuzz-replay fuzz/json_fuzz.cpp fuzz/standalone_main.cpp)
    target_link_libraries(echo-json-fuzz-replay PRIVATE echo::appkit echo-project-options)
    set_target_properties(echo-json-fuzz-replay PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)

    # 2. libFuzzer engine — coverage-guided; needs Clang's -fsanitize=fuzzer. The
    #    code under test (echo-appkit) is instrumented via the build's global
    #    CMAKE_CXX_FLAGS (see fuzz/README.md and the CI fuzz-json job); the target
    #    itself only needs the libFuzzer main at link.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(echo-json-fuzzer fuzz/json_fuzz.cpp)
        target_link_libraries(echo-json-fuzzer PRIVATE echo::appkit echo-project-options)
        target_compile_options(echo-json-fuzzer PRIVATE -g -O1 -fsanitize=fuzzer-no-link)
        target_link_options(echo-json-fuzzer PRIVATE -fsanitize=fuzzer)
        set_target_properties(echo-json-fuzzer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)
        message(STATUS "ECHO appkit: JSON libFuzzer target ON (echo-json-fuzzer)")
    else()
        message(STATUS "ECHO appkit: libFuzzer target skipped (needs Clang); "
                       "replay driver echo-json-fuzz-replay still built")
    endif()
endif()
