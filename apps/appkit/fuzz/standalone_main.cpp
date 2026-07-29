// Standalone driver for the JSON fuzz harness — a libFuzzer-free main() so the
// SAME LLVMFuzzerTestOneInput can be built with ANY compiler (the project's
// default g++/MSVC toolchain) and run over corpus or crash files. This is what
// keeps the fuzz harness buildable on the zero-dependency toolchain (constraint
// #1): coverage-guided fuzzing needs Clang, but *replaying* an input does not.
//
// Usage:  echo-json-fuzz-replay [file ...]
//   With file arguments, each file's bytes are fed once through the harness — the
//   way you reproduce a crash libFuzzer reported (`echo-json-fuzz-replay crash-abc`)
//   or replay a saved corpus. With no arguments it reads stdin as a single input.
//
// This is deliberately NOT a fuzzer: no mutation, no coverage feedback. Its job is
// buildable, dependency-free replay. The coverage-guided engine is the Clang
// libFuzzer target (echo-json-fuzzer); see fuzz/README.md.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {
int run_bytes(const std::string& bytes) {
    return LLVMFuzzerTestOneInput(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        // No files: treat stdin as a single input.
        const std::string data((std::istreambuf_iterator<char>(std::cin)),
                               std::istreambuf_iterator<char>());
        return run_bytes(data);
    }
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot open %s\n", argv[i]);
            return 1;
        }
        const std::string data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        run_bytes(data);
        std::fprintf(stderr, "ok: %s (%zu bytes)\n", argv[i], data.size());
    }
    return 0;
}
