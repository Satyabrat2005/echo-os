# Phase 4: real-world bring-up and validation

Makes Phase 3 **real** on the laptop: installs the actual dependencies, builds
with every `ECHO_WITH_*` flag ON, fixes what real library versions surface, tunes
against real behavior, and records genuinely measured latency. No new features.

> ⚠️ **Before opening this PR, fill the three `TODO` blocks below** with the real
> results from the hardware run (Steps 3–5). They are intentionally blank here —
> the numbers and known issues are only real once the demo has actually run on the
> laptop with a mic, speaker, webcam, and an outside tester.

## Code-side bring-up (done, verified in the stub build)

- **llama.cpp adapter hardened against version drift.** The one call that moves
  between llama.cpp revisions — the KV-cache clear — now routes through
  `echo_llama_kv_clear()` and defaults to the current memory API, with a
  build-time override `-DECHO_LLAMA_KV_CLEAR=self|cache` for older checkouts
  (`cognitive-core/src/llm.cpp`, `cmake/echo_ai.cmake`). No safe-mode behavior
  changed.
- **Route-tag parsing extracted + unit-tested.** `parse_route_tag()` moved into
  its own always-compiled unit (`cognitive-core/.../route_tag.{hpp,cpp}`) and now
  tolerates the messy output real models produce (`[route:  Mail ]` → `mail`). New
  tests in `tests/test_main.cpp` cover the manual-flow utterances and edge cases;
  they run in the zero-dependency stub build and pass under `ctest`.
- **Dependency bring-up scripts.** `scripts/setup_deps.ps1` (+ `.sh`) build
  whisper.cpp/llama.cpp from source, download the freely-available models, and
  record exact versions to `models/INSTALLED_VERSIONS.md`; `scripts/build_real.ps1`
  configures all flags ON and builds. Account-gated pieces (Porcupine key + custom
  "Hey ECHO" wake word) are called out as manual steps, not faked.
- **Honest latency tooling.** `scripts/analyze_latency.ps1` (+ `.sh`) turns
  `latency_log.csv` into the README's min/avg/max table and gap-analysis line, and
  warns when fewer than 20 turns were logged.
- **README Phase 4 section** with the ordered checklist, an installed-versions
  table, the restructured latency table, and an honest **Known Issues** section.

## Constraints held (unchanged)

- Safe-mode gate, lock-free ring buffer, latency-budget model, and apps isolation
  untouched. The stub build stays zero-dependency and green; both smoke tests pass.

## TODO — real hardware validation (fill before opening the PR)

**1. Build with everything ON.** Installed versions:
<!-- TODO: paste the models/INSTALLED_VERSIONS.md table; note the KV-clear variant that compiled. -->

**2. Measured latency** (≥20 real turns, from `scripts/analyze_latency.ps1`):
<!-- TODO: paste the min/avg/max table + gap-analysis line. Record the real number even if it exceeds 120 ms. -->

**3. First outside-user test + Known Issues:**
<!-- TODO: who tested (non-founder), top confusions, and the Known Issues list from the README. -->
