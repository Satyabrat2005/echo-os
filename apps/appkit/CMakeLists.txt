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
