# Phase 8: static analysis, fuzzing, and coverage

Phase 6's two real bugs — the JSON parser's unbounded recursion (a stack overflow on
hostile input) and the Gmail header-injection hole — were both found by careful manual
review. That doesn't scale and won't happen with the same rigor every time. Phase 7
gave the project CI; this phase gives that CI **teeth against exactly this class of
bug**: two static analyzers, a JSON fuzz target, and coverage reporting, all wired
into `.github/workflows/ci.yml` as additive jobs.

Unlike Phase 7, this PR *does* touch product code — because running the analyzers
surfaced real findings, and the Phase 6 discipline is to **fix them here with the
change, not defer them**.

## What's in this PR

- **clang-tidy** (`.clang-tidy` + `scripts/run_clang_tidy.sh`, new CI job) — a curated
  `bugprone-*` / `cert-*` / `clang-analyzer-*` ruleset (the family that flags the
  untrusted-data-into-protocol-string pattern behind the header-injection bug) over
  every first-party translation unit. Findings are hard errors. Each disabled check
  carries a one-line reason in `.clang-tidy` — genuine noise is tuned out and
  documented; nothing with a real defect behind it is suppressed.
- **cppcheck** (new CI job) — a second, independently-implemented analyzer over the
  compile database. Different engines catch different things, so it runs *alongside*
  clang-tidy. `warning`/`performance`/`portability` findings fail the job.
- **JSON fuzz target** (`apps/appkit/fuzz/`, new bounded CI job) — a coverage-guided
  libFuzzer harness for `Json::parse` (the one parser that eats untrusted network
  bytes and already had a crash bug), run ~2 min under ASan/UBSan and seeded from a
  checked-in corpus. Ships with a compiler-agnostic standalone replay driver so a
  crash can be reproduced without Clang, and `fuzz/README.md` documents longer local
  sessions. Off by default (`-DECHO_BUILD_FUZZERS=ON`).
- **Coverage** (`-DECHO_COVERAGE=ON`, new CI job) — gcov + gcovr; the % lands in the
  Actions run summary (visible + trending on its own), the HTML/XML report is an
  artifact, and the XML feeds a Codecov badge. **Not** gated on a threshold yet.
- **README** — a coverage badge beside the CI badge, a new **Code Quality** section
  (what's checked, findings fixed, and what's deliberately not done yet), and the new
  checks added to the branch-protection list.

## Findings fixed this phase (with the fix, not filed for later)

- **cppcheck → latent null-deref** in `apps/demo/echo_demo.cpp`: `mic->start()` was
  called unguarded, while the cleanup path and the "microphone unavailable" log both
  already treated `mic` as possibly-null. Today's factories never return null so it
  can't crash *yet*, but a real mic backend that fails to open the device would. Now
  guarded — it degrades to the existing "unavailable" message instead of crashing.
- **clang-tidy → 15 findings, all fixed:**
  - `VoiceCommand::slot` — a `std::move` the compiler couldn't honor (the ternary's
    other branch is a `const&`, forcing a copy anyway) plus a per-call copy of the
    by-value default → now a `const&` parameter. Behavior identical, covered by the
    existing apps tests.
  - `url_encode` — the hot-path `snprintf` replaced with direct hex writing: faster,
    and no ignored return value to reason about. Covered by the existing
    `test_url_encode` (`%2B` / `%26` / `%3D` cases).
  - `sanitize_header_value` (the Phase 6 header-injection fix) moved into an anonymous
    namespace.
  - Twelve `cert-err33-c` sites — all C-stdlib format/log calls (`snprintf` into a
    fixed buffer, `fprintf`/`fflush`, `signal`) whose return is conventionally and
    safely ignored — made explicit with `(void)` casts, so the check stays live to
    catch a *future* ignored `fopen`/`malloc` instead of being disabled wholesale.
  - Surfaced only by the CI run (Linux/libstdc++ models `std::optional` where a
    Windows/MSVC-header run can't): a real **unchecked-optional-access** in the
    supervisor's exit-code logging → idiomatic `.value_or(-1)`; a `size_t`→`double`
    narrowing in the latency printout → explicit `static_cast`. `Result::value()`
    keeps a documented `NOLINT` — it is the deliberate unchecked accessor (precondition
    `is_ok()`, like `std::optional::operator*`).
- **cppcheck → redundant `.c_str()`** passed to `string_view`-taking loggers
  (`stlcstrParam`, a needless `strlen`), fixed at all call sites by passing the
  `std::string` directly.
- **Fuzzing → no new crash.** That's the expected, good result: the one known parser
  crash was already fixed in Phase 6 with the depth cap; 30k mutated inputs locally and
  the bounded ASan/UBSan run in CI found nothing new. The harness's value is ongoing
  regression protection.

## Constraints held

- **Zero-dependency local build intact** — every new flag (`ECHO_BUILD_FUZZERS`,
  `ECHO_COVERAGE`) defaults off; static analysis and fuzzing are CI-only jobs, never a
  requirement for a normal or stub build.
- **CI runtime bounded** — the fuzz job is time-limited to ~2 min; the coverage/fuzz
  builds are ccache-cached. Jobs run in parallel with the existing three.
- **Honest suppressions** — disabled checks are documented with reasons; the Codecov
  badge reads `unknown` until the repo is enabled on Codecov (a one-time settings
  step, noted in the README), while the coverage % is visible in every run regardless.

## What's deliberately not done yet

- **Fuzzing is scoped to JSON** (highest risk, prior crash bug). URL/`readable.cpp`/
  base64 are noted as future harnesses following the same pattern.
- **No coverage threshold** — the number is made visible and trending first; an
  arbitrary floor on a codebase this size (much of it still intentionally stubbed)
  would be premature.
- **The real local-AI matrix stays excluded**, as in Phase 7 — the analyzers run over
  the dependency-free first-party sources, not the account-gated multi-GB build.

## Verification done here

- clang-tidy run locally (v22) over all 60 first-party TUs and again in CI
  (Linux/libstdc++). The CI toolchain models `std::optional` where the local
  Windows/MSVC-header run can't, so it caught the optional-access + narrowing findings
  the first push missed; those are fixed and the job is green.
- cppcheck run locally (v2.17, `--check-level=exhaustive`) and in CI. The CI version
  additionally flagged the `stlcstrParam` `.c_str()` calls; fixed, job green.
- Fuzz harness builds and runs; the standalone replay driver passes the seed corpus and
  30k mutated inputs with no crash (local runs are MinGW/no-ASan — the ASan/UBSan run is
  CI's job).
- Coverage: instrumented build compiles, full `ctest` passes, gcovr reports
  **~39.6% lines / 38.9% functions** on the current suite.
- Full `ctest` suite green in both the stub and coverage builds (`echo-smoke`,
  `echo-appkit-tests`, `echo-apps-smoke`, `echo-nlu-routing`).
- The CI jobs themselves are confirmed green only once this branch is pushed and Actions
  fires — verified via `gh run watch` after push.
