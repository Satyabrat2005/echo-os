# ECHO OS — State of the Project

*One honest page: what's built, what's verified, what's still pending real-world
validation, how well it's tested, and what quality gates are in place. This is
the document to read first — a YC partner or a new engineer should be able to
trust every line of it. It is an internal reference, not marketing copy.*

Last updated: Phase 9 (documentation pass). Source of truth for every claim is
the repo at this commit; nothing here is aspirational unless explicitly labelled.

## In one paragraph

ECHO OS is the on-device software runtime for smart glasses worn by people with
memory loss. It is **not a from-scratch kernel** — it runs on a stripped-down
embedded Linux base and layers a custom runtime, a voice-first UI shell, and a
fully local AI pipeline on top. The architecture is real and complete: a
sub-120 ms perception → cognitive → voice core loop with a confidence-gated
safe-mode fallback, a hard-isolated apps layer in separate processes, and a
build that compiles dependency-free on any host with deterministic stubs. What is
**not yet done** is running the real AI models and real third-party APIs on real
hardware with real users — the seams for all of it exist and compile, but the
live bring-up (Phases 4–6, Part B) has not happened. The honest one-liner: **the
skeleton and the contracts are solid and tested; the flesh — real models, real
credentials, real hardware, measured latency — is wired up but unproven.**

## What's built and verified ✅

| Area | Status | Evidence |
|------|--------|----------|
| Module boundaries & public interfaces | **Built** | `common`, `sensor-pipeline`, `perception`, `cognitive-core`, `voice-ui`, `companion-sync`, `power-mgmt`, `boot` all compile as static libs |
| Lock-free SPSC ring buffer (sensor handoff) | **Built & tested** | [`ring_buffer.hpp`](../sensor-pipeline/include/echo/sensor/ring_buffer.hpp); fixture frames traverse the real queue in `echo-sensor` |
| 120 ms latency budget as an enforced contract | **Built & tested** | encoded in [`latency.hpp`](../common/include/echo/latency.hpp), asserted by unit test |
| Safe-mode gate (principle #5) | **Built & tested** | [`cognitive_core.cpp`](../cognitive-core/src/cognitive_core.cpp), threshold 0.72; degrades on failure |
| Privacy-by-shape companion transport (principle #4) | **Built & tested** | [`companion_sync.hpp`](../companion-sync/include/echo/companion/companion_sync.hpp) — "no API accepts a `SensorFrame`" is now a compile-time assertion in `echo-companion` |
| End-to-end core turn (sensor→perception→cognitive→voice) | **Built & tested** | fixture audio → safe-mode fallback spoken, asserted in `echo-e2e-fixture` |
| Apps layer: 8 apps as real supervised processes | **Built & tested** | IPC round-trip + crash containment in `echo-apps-smoke` |
| Hard core/apps isolation in the build graph | **Built** | `echo::app-sdk` links no core module (CMake-enforced) |
| HUD: exactly three overlay primitives | **Built** | [`hud.hpp`](../apps/hud-compositor/include/echo/apps/hud/hud.hpp) |
| Confirm-before-send gate (state-changing actions) | **Built & tested** | [`confirmation.cpp`](../apps/appkit/src/confirm/confirmation.cpp), 100% covered |
| NLU mail/browser routing fix (Phase 6) | **Built & tested** | `echo-nlu-routing` regression suite |
| Real AI adapters: whisper / OpenCV / Piper | **Run against real models in CI** (Phase 11) | `tests/real_*_test.cpp` in the `real-engines` job — real weights, on the fixtures (not field-tested) |
| Real AI adapters: llama.cpp / Porcupine | **Compiles** behind `ECHO_WITH_*`, not run | Phase 3; account-gated (Porcupine) / too large for CI (llama) — real-hardware-run items |
| Real API backends (Spotify/Gmail/Search/YouTube) | **Compiles & links** behind `ECHO_WITH_NETWORK` | Phase 5/6; libcurl 8.21.0; **no live API call has run** |
| JSON parser hardening (depth cap) + Gmail header-injection fix | **Built & tested** | Phase 6 fixes, both with regression tests + a fuzz harness |

## What's pending real-world validation ⏳

These are the **open gaps from Phases 4, 5, and 6 (Part B)**. They are not bugs —
they are the parts that inherently require hardware, accounts, or people, and so
could not run in a headless sandbox. Do not claim any of them as done until the
evidence exists.

| # | Gap | Phase | What it needs |
|---|-----|-------|---------------|
| 1 | Real quantized LLM/ASR/TTS/vision run end-to-end | 4 | models installed on the laptop, `-DECHO_REAL_AI=ON` build, real mic/speaker/webcam |
| 2 | **Measured** per-stage latency (≥20 real turns) | 4 | the numbers in the README latency table are still blank placeholders — **not** filled with fake data |
| 3 | Gap analysis: real end-to-end ms vs. the 120 ms target | 4 | expected to **exceed** 120 ms on a laptop CPU; record it honestly, don't tune the budget to fit |
| 4 | A non-founder tester uses it with no instructions | 4 | the "Known Issues" table is intentionally empty until this happens |
| 5 | Live credentialed Spotify/Gmail/Search/YouTube calls | 5 | real developer-app credentials + the network build; only mock traffic has run |
| 6 | One-time OAuth authorize helper (`scripts/authorize.*`) | 5 | currently a documented manual browser-paste step |
| 7 | On-glasses sensor DMA frontends + BLE/WiFi companion transport | hardware | the real embedded target; today these are stubs |
| 8 | Porcupine AccessKey + custom "Hey ECHO" `.ppn` | 4 | account-gated (Picovoice console); user-provided, never committed |

The README's Phase 4/5 tables are the canonical place these numbers get filled in
**during** the real bring-up, and are deliberately left as `_—_` placeholders
rather than fabricated.

## Test coverage by module

Measured with `gcov`/`gcovr` on the default **stub-build** test suite, excluding
test and fuzz-harness code. After **Phase 10** this suite is **nine** CTest
suites: the four earlier ones (`echo-smoke`, `echo-appkit-tests`,
`echo-apps-smoke`, `echo-nlu-routing`) plus the fixture-driven core-pipeline
harness `echo-sensor`, `echo-perception`, `echo-voice`, `echo-companion`, and the
end-to-end `echo-e2e-fixture`. Overall: **≈44 % lines** (1161 / 2614), 50 %
functions, 24 % branches — up from ≈40 % before Phase 10, with the gain
concentrated exactly where Phase 9 flagged the weakness: the core loop.

| Module | Line coverage | Was (Phase 9) | Read this as |
|--------|--------------:|--------------:|--------------|
| `companion-sync` | **96 %** | 0 % | ⬆ real alert/status/firmware construction + lifecycle exercised; the privacy guarantee (no `SensorFrame` path) is now a **compile-time** assertion, not a comment |
| `voice-ui` | **91 %** | 0 % | ⬆ real `to_utterance()` tone mapping + stub shell fully covered; the Piper **synthesis** path is now run for real in the Phase 11 `real-engines` job (separate from this gcov run); only the SDL **playback** backend (needs hardware) is uncovered |
| `cognitive-core` | **84 %** | 67 % | ⬆ both gate branches now driven end-to-end (low-confidence *and* confident-but-no-LLM); the real `llm.cpp` adapter is still compiled out of the stub build |
| `perception` | **69 %** | 7 % | ⬆ routing/framing/fusion + input guards covered. The real **ASR (whisper)** and **vision (OpenCV)** engines are now run against real weights in the Phase 11 `real-engines` job (separate from this stub gcov run, so they still read 0 here); the **wake-word (Porcupine)** model path and the wake-gated **endpoint→transcribe** branch stay genuinely uncovered — an explicitly asserted gap, not padding |
| `sensor-pipeline` | **42 %** | 0 % | ⬆ the real `SensorPipeline` + SPSC queue at **85 %**; the two 0 % files are the scaffold no-op stub factories (deliberately bypassed) and the SDL/OpenCV real-capture path (compiled out) |
| `apps/appkit` | **76 %** | 70 % | confirm gate (100 %), JSON, URL, readability; base64 / token-store / http transport still near 0 % |
| `apps/app-framework` | **74 %** | 71 % | supervisor / router / IPC / NLU — still the best-tested app-layer code |
| `apps/*` (8 apps) | ~22 % | ~30 % | mock app logic covered; the **real** network backends (spotify/gmail/youtube/search) near 0 % |
| `common` | 20 % | 20 % | latency + log partly covered; config/types/result thin |
| `power-mgmt` | **0 %** | 0 % | not on the tested path |
| `hud-compositor` | **0 %** | 0 % | the sole visual surface (lives under `apps/hud`) |
| `boot` (runtime wiring) | **0 %** | 0 % | integration wiring, exercised only by running the binary |

> Numbers are re-measured on the Phase-10 coverage build (`-DECHO_COVERAGE=ON`,
> Debug) over all nine suites; the few points of movement in the `apps/*` /
> appkit rows are measurement/build variance, not new app-layer tests — Phase 10
> touched only the core loop.

**The headline the coverage numbers tell:** Phase 9's inversion — newest code
best-tested, core safety loop least-tested — is now **substantially corrected**.
The perception→cognitive→voice core loop and the privacy-critical companion path
went from 0–7 % to 42–96 %, and the single most load-bearing behavior (an unsure
turn falling to the calm safe-mode fallback) is now asserted end-to-end by
`echo-e2e-fixture`.

**Phase 11 narrowed the "real engines untested" gap — the part of it that never
actually needed the hardware.** Three of the real engines are free, account-free,
and small enough for CI, so they are now run against real weights in the
`real-engines` CI job (see below), not just stubbed:

- **ASR — real whisper.cpp** (`tiny.en`) transcribes the fixtures: silence →
  near-empty, garble → no crash, and a clear (Piper-synthesized) phrase → a
  non-empty transcript with the expected words. — `tests/real_asr_test.cpp`
- **Vision — real OpenCV YuNet + SFace** detect and recognize real public-domain
  face photos: enrolled → detected **and** matched, a different person → detected
  but **not** matched, a non-face frame → no detection. — `tests/real_vision_test.cpp`
- **TTS — real Piper** synthesizes both the neutral and reassuring voice-ui tones
  into valid, non-silent audio, with the reassuring tone measurably slower (the
  tone setting changes the real waveform, not just a label). — `tests/real_tts_test.cpp`

What that **does not** claim, and STATE.md will not let it imply: real-world
robustness. Those fixtures are clean and synthetic (a TTS phrase, well-lit frontal
portraits) — the tests prove the engines *work at all* on good input, not that
`tiny.en` or SFace hold up under field noise, poor light, motion, or a real human
speaking naturally. That still needs the on-hardware run.

What remains genuinely untested, and needs the libraries/credentials/hardware:

- **Porcupine wake-word** — needs an account-gated Picovoice access key; stays a
  real-hardware-run item.
- **llama.cpp / full LLM reasoning** — multi-GB model + CI runtime cost make it
  impractical in a workflow; stays a real-hardware-run item.
- **Live behaviour** — real mic/webcam capture, latency under load, and a human
  speaking/moving naturally (vs. a clean recorded fixture), plus the SDL audio
  **playback** path and the on-hardware sensor DMA/real-capture code.

The tests mark each remaining gap explicitly rather than reporting high coverage
on code they never run.

> Coverage is **not gated on a threshold** yet (a deliberate Phase 8 choice — an
> arbitrary floor on a codebase that is still intentionally stubbed at its core
> would reward testing stub code). The number is made visible and trending first;
> a floor comes once the real engines land and the meaningful denominator settles.

## Quality gates in place (CI)

Every push and PR to `master` runs [`.github/workflows/ci.yml`](../.github/workflows/ci.yml):

| Gate | What it enforces |
|------|------------------|
| **Stub build + full ctest** | dependency-free build, all four suites — the always-green safety net |
| **Network build** | `-DECHO_WITH_NETWORK=ON` compiles & links libcurl; appkit/apps tests pass on a clean machine (no live API calls) |
| **Secret scan** | `check_secrets.sh` blocks a committed API key, OAuth secret, private key, token cache, or `.env` |
| **clang-tidy** | `bugprone-*` / `cert-*` / `clang-analyzer-*` over every first-party TU; findings are hard errors |
| **cppcheck** | second independent analyzer; `warning`/`performance`/`portability` fail the job |
| **Fuzz (JSON)** | bounded libFuzzer under ASan/UBSan on the one parser that eats untrusted bytes |
| **Coverage** | gcov + gcovr; % printed to the run summary, XML to Codecov |
| **Real engines** (Phase 11) | `-DECHO_WITH_WHISPER/OPENCV/PIPER=ON`; downloads + **SHA-256-verifies** the free models and runs `tests/real_*` against real weights on the fixtures. Runs in parallel; models + whisper build cached |

Two one-time repo-admin steps remain outside code: **branch protection** (require
these checks before merge) and **enabling Codecov** (the badge reads `unknown`
until then; the percentage is visible in every run regardless).

The `real-engines` job (Phase 11) now runs whisper.cpp, OpenCV, and Piper against
real models in CI — they are free, need no account, and are small enough to cache.
What stays deliberately **excluded**: the full `ECHO_REAL_AI=ON` matrix, because
its remaining two engines don't belong in a workflow — **Porcupine** (account-gated
access key) and **llama.cpp** (multi-GB model + runtime cost). Every model the real
job does download is checksum-verified before use (running unverified third-party
model binaries in CI is a supply-chain risk, not a hypothetical one).

## Known issues / inconsistencies noted during this doc pass

Per Phase 9 constraint #1 (documentation only, no source changes), anything
surfaced while writing these docs is recorded here rather than fixed:

- *None found in this pass.* The repo's existing honest style holds up — the
  README's Phase 4/5 placeholder tables, the "compiles but not run live" caveats,
  and the code comments all match what the tree actually does. If a future doc
  pass finds a real discrepancy, list it here with a file/line pointer.

## Bottom line for a new reader

- **Trust the contracts.** Interfaces, isolation boundary, safe-mode gate, latency
  budget, secret hygiene, and the mock/real seams are real and mostly tested.
- **Distrust any "it works on hardware" claim** — that step (real models, real
  APIs, real users, measured latency) has not happened yet, and the docs say so
  everywhere it matters.
- **The next engineer's first job** is the real bring-up in [RUNBOOK.md](RUNBOOK.md)
  and closing the core-loop test gap above.
