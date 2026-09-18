# Decoder fuzzing

> **Two harnesses share this setup.** This directory holds the appkit JSON one
> (below). Phase 22 added a second, `companion-sync/fuzz/command_fuzz.cpp`, over the
> caregiver inbound command decoder — same switch (`-DECHO_BUILD_FUZZERS=ON`, declared
> at the root), same `standalone_main.cpp` replay driver, its own seed corpus in
> `companion-sync/fuzz/corpus/`. Targets: **`echo-command-fuzzer`** (Clang) and
> **`echo-command-fuzz-replay`** (any compiler). Everything in the sections below
> applies to it verbatim; substitute the target and corpus names.
>
> That decoder gets its own harness because it is the only one whose output *changes
> what the device does*: an accepted command is spoken aloud to the wearer or written
> to their store. Its harness therefore asserts more than "did not crash" — it asserts
> every field invariant of an *accepted* command, because a malformed command that is
> accepted is worse than one that crashes. The bytes it sees have already passed the
> secure channel's MAC, so it models an attacker who **holds the pairing key**: a
> compromised caregiver phone.

# JSON parser fuzzing

The appkit JSON reader (`echo::apps::net::Json::parse`) is the one component that
parses **untrusted network bytes** — Spotify / Gmail / Google API responses. It
already had one real crash bug (unbounded recursion on deeply-nested input, fixed
in Phase 6 with a depth cap), which is exactly the class of bug a fuzzer finds
automatically. This directory holds a small libFuzzer harness so the next such bug
is caught by a machine on every PR instead of by luck in manual review.

## Files

| File | Purpose |
|------|---------|
| `json_fuzz.cpp` | The harness — `LLVMFuzzerTestOneInput`, shared by both targets below. Feeds the input bytes to `Json::parse` and walks the result. |
| `standalone_main.cpp` | A libFuzzer-free `main()` so the harness builds with **any** compiler and *replays* corpus/crash files. No mutation, no coverage. |
| `corpus/` | Seed inputs (valid, representative JSON) to start the fuzzer from. |

Two CMake targets are produced when `-DECHO_BUILD_FUZZERS=ON`:

- **`echo-json-fuzzer`** — the coverage-guided libFuzzer engine. Requires Clang
  (only Clang ships `-fsanitize=fuzzer`). Built and run bounded in CI.
- **`echo-json-fuzz-replay`** — the standalone replay driver. Builds with the
  default g++/MSVC toolchain; use it to reproduce a crash or replay a corpus
  without Clang.

Both are **off by default** — fuzzing is an extra CI/dev tool, never a requirement
for a normal build (constraint #1: the stub build stays zero-dependency).

## Run it locally (longer than CI)

CI runs the fuzzer for a bounded ~2 minutes on every PR (fast feedback). To run a
real, longer session locally you need Clang:

```bash
# Configure a dedicated fuzz build, instrumenting the code under test for
# coverage + AddressSanitizer + UndefinedBehaviorSanitizer.
cmake -S . -B build-fuzz \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DECHO_BUILD_FUZZERS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -g -O1 -fno-omit-frame-pointer"

cmake --build build-fuzz --target echo-json-fuzzer

# Fuzz for as long as you like (here: 1 hour, 10s per-input timeout). New corpus
# entries accumulate in fuzz-corpus/; any crash is written as crash-<hash>.
mkdir -p fuzz-corpus
./build-fuzz/fuzz/echo-json-fuzzer fuzz-corpus apps/appkit/fuzz/corpus \
  -max_total_time=3600 -timeout=10 -rss_limit_mb=2048 -print_final_stats=1
```

A finding is written to `crash-<hash>` (and `-artifact_prefix=` can redirect it).
Reproduce and debug it with either target:

```bash
./build-fuzz/fuzz/echo-json-fuzzer crash-<hash>            # under ASan, with a stack
./build-analysis/fuzz/echo-json-fuzz-replay crash-<hash>   # any toolchain, no Clang needed
```

## Reproduce / replay without Clang

The standalone driver builds with the project's default toolchain:

```bash
cmake -S . -B build-analysis -DECHO_BUILD_FUZZERS=ON
cmake --build build-analysis --target echo-json-fuzz-replay
# Replay every seed (each should print "ok: ..."); or pass a crash file to reproduce.
./build-analysis/fuzz/echo-json-fuzz-replay apps/appkit/fuzz/corpus/*.json
```

## Scope

Only the JSON parser is fuzzed today — it is the highest-risk parser (untrusted
network input, prior crash bug). The other text/network parsers (URL encoding,
`readable.cpp` HTML extraction, base64) are candidates for their own harnesses as
future work; see the README "Code Quality" section.
