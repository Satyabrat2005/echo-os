// Fuzz harness for the appkit JSON reader (echo::apps::net::Json::parse).
//
// The JSON parser is the one appkit component that consumes *untrusted network
// bytes* — the Spotify / Gmail / Google API responses — and turns them into a
// structured value the backends read. It already had one real crash bug:
// unbounded recursion on deeply-nested input, a stack overflow fixed in Phase 6
// with a depth cap. Fuzzing exists so the *next* bug of that class is caught by a
// machine on every PR instead of by luck in a manual review.
//
// The entry point is libFuzzer's LLVMFuzzerTestOneInput: given an arbitrary byte
// buffer, parse it and touch the result. The only contract asserted is the one
// the module promises — parse() always terminates and returns a value; malformed
// input degrades to Null, it never throws, over-reads, or recurses without bound.
// Any input that trips ASan/UBSan (built into the CI fuzz job) or hangs is a
// finding. See the README "Code Quality" section and fuzz/README.md for how to run
// this locally for longer sessions.
#include "echo/apps/net/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // Build the input from the raw bytes, embedded NULs and all, and parse it.
    const std::string text(reinterpret_cast<const char*>(data), size);
    const echo::apps::net::Json j = echo::apps::net::Json::parse(text);

    // Walk the accessors so the fuzzer explores the built tree, not just the parse.
    // Every one of these is defined on every Json value and must be crash-free
    // regardless of what parse() produced.
    (void)j.type();
    if (j.is_object() || j.is_array()) {
        const std::size_t n = j.size();
        for (std::size_t i = 0; i < n; ++i) (void)j[i].type();  // array indexing path
    }
    (void)j.str_or("snippet");  // the common object-field extraction path
    return 0;
}
