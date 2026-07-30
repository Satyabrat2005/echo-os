// ECHO OS tests — the shared, framework-free CHECK harness.
//
// Same spirit as test_main.cpp's inline macro, factored out so every Phase 10
// test target (sensor-pipeline, perception, voice-ui, companion-sync, e2e) shares
// one implementation. No external framework, runs under CTest: a failing CHECK
// prints file:line and the program returns non-zero.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace echo::test {

// C++17 inline variable: one definition even when included across TUs.
inline int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::echo::test::g_failures;                                        \
        }                                                                      \
    } while (0)

// Call once from main(); returns the process exit code.
inline int report(const char* suite) {
    if (g_failures == 0) {
        std::printf("[%s] all tests passed\n", suite);
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "[%s] %d check(s) failed\n", suite, g_failures);
    return EXIT_FAILURE;
}

}  // namespace echo::test
