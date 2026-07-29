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
| Lock-free SPSC ring buffer (sensor handoff) | **Built** | [`ring_buffer.hpp`](../sensor-pipeline/include/echo/sensor/ring_buffer.hpp) |
| 120 ms latency budget as an enforced contract | **Built & tested** | encoded in [`latency.hpp`](../common/include/echo/latency.hpp), asserted by unit test |
| Safe-mode gate (principle #5) | **Built & tested** | [`cognitive_core.cpp`](../cognitive-core/src/cognitive_core.cpp), threshold 0.72; degrades on failure |
| Privacy-by-shape companion transport (principle #4) | **Built** | [`companion_sync.hpp`](../companion-sync/include/echo/companion/companion_sync.hpp) — no API accepts a `SensorFrame` |
| Apps layer: 8 apps as real supervised processes | **Built & tested** | IPC round-trip + crash containment in `echo-apps-smoke` |
| Hard core/apps isolation in the build graph | **Built** | `echo::app-sdk` links no core module (CMake-enforced) |
| HUD: exactly three overlay primitives | **Built** | [`hud.hpp`](../apps/hud-compositor/include/echo/apps/hud/hud.hpp) |
| Confirm-before-send gate (state-changing actions) | **Built & tested** | [`confirmation.cpp`](../apps/appkit/src/confirm/confirmation.cpp), 100% covered |
| NLU mail/browser routing fix (Phase 6) | **Built & tested** | `echo-nlu-routing` regression suite |
| Real AI adapters (whisper/llama/Piper/Porcupine/OpenCV) | **Compiles** behind `ECHO_WITH_*` | Phase 3; **never run against real models in-sandbox** — see pending |
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

Measured with `gcov`/`gcovr` on the default **stub-build** test suite (the four
CTest suites: `echo-smoke`, `echo-appkit-tests`, `echo-apps-smoke`,
`echo-nlu-routing`), excluding test and fuzz-harness code. Overall: **≈40 %
lines** (1058 / 2675), 39 % functions, 22 % branches.

| Module | Line coverage | Read this as |
|--------|--------------:|--------------|
| `cognitive-core` | **67 %** | safe-mode **gate logic** well-covered; the real `llm.cpp` adapter is compiled out of the stub build, so its live path is unexercised |
| `apps/app-framework` | **71 %** | supervisor / router / IPC / NLU — the newest, best-tested code |
| `apps/appkit` | **70 %** | confirm gate (100 %), JSON, URL, readability well-covered; base64 / token-store / http transport at 0 % |
| `apps/*` (8 apps) | ~30 % | mock app logic covered; the **real** network backends (spotify/gmail/youtube/search) near 0 % |
| `common` | 20 % | latency + log partly covered; config/types/result thin |
| `perception` | **7 %** | ⚠️ core product, barely exercised — only the stub engine ticks |
| `sensor-pipeline` | **0 %** | ⚠️ core product — lock-free buffer has correctness weight, near-zero test coverage |
| `voice-ui` | **0 %** | ⚠️ core product |
| `companion-sync` | **0 %** | ⚠️ the privacy-critical off-device path |
| `power-mgmt` | **0 %** | not on the tested path |
| `hud-compositor` | **0 %** | the sole visual surface |
| `boot` (runtime wiring) | **0 %** | integration wiring, exercised only by running the binary |

**The headline the coverage numbers tell:** the *newest* code (the apps
framework and appkit, Phases 5–8) is the *best*-tested, while the **core
safety-critical product loop — perception, sensor-pipeline, voice-ui, and the
privacy-critical companion-sync path — is the least-tested.** `cognitive-core` is
a partial exception: its gate *decision logic* is well-covered, but its real LLM
adapter is not. This inversion is the single most important thing for a reviewer
to understand: the parts most load-bearing for user safety have the thinnest
automated safety net today. Closing it is the natural next testing investment,
and it depends on the same real-hardware bring-up as the gaps above.

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

Two one-time repo-admin steps remain outside code: **branch protection** (require
these checks before merge) and **enabling Codecov** (the badge reads `unknown`
until then; the percentage is visible in every run regardless). The real-AI
matrix (`ECHO_REAL_AI=ON`) is deliberately **excluded** from CI — multi-GB model
downloads and an account-gated Porcupine key don't belong in a workflow.

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
