# ECHO OS — State of the Project

*One honest page: what's built, what's verified, what's still pending real-world
validation, how well it's tested, and what quality gates are in place. This is
the document to read first — a YC partner or a new engineer should be able to
trust every line of it. It is an internal reference, not marketing copy.*

Last updated: Phase 20 (longevity & resource-leak soak — a simulated-time soak that drives the
real runtime + real memory engine through 7 compressed days and puts real numbers on
reminder-scheduler drift, event-log retention, RSS, fd count, and repeated watchdog recovery;
it found and fixed two genuine longevity bugs along the way. See the dedicated section below).
Source of truth for every claim is the repo at this commit; nothing here is aspirational unless
explicitly labelled.

## In one paragraph

ECHO OS is the on-device software runtime for smart glasses worn by people with
memory loss. It is **not a from-scratch kernel** — it runs on a stripped-down
embedded Linux base and layers a custom runtime, a voice-first UI shell, and a
fully local AI pipeline on top. The architecture is real and complete: a
sub-120 ms perception → cognitive → voice core loop with a confidence-gated
safe-mode fallback, a hard-isolated apps layer in separate processes, and a
build that compiles dependency-free on any host with deterministic stubs. What is
**not yet done** is running the whole stack on real hardware with a real user:
every real AI engine (wake-word, ASR, vision, TTS, LLM) is now run against real
models in CI on fixtures/synthetic speech (Phases 11–13), but real third-party APIs
and the live mic/webcam/human/latency bring-up (Phases 4–6, Part B) have not
happened. The honest one-liner: **the skeleton, the contracts, and every real-model
integration are solid and CI-tested; what's left is the flesh that can't be
simulated — real hardware, a real human in a real room, real credentials, and
measured latency.**

**New in Phase 15 — the product finally *remembers*.** Every phase before this
built real but *generic* voice-assistant plumbing; nothing on-device persisted who
a face belongs to or what the wearer needs reminding of. Phase 15 adds the `memory/`
module: a local, private SQLite store of **people** (name + relationship the wearer
stated, keyed by the SFace embedding), **reminders** (with recurrence + acknowledged
state), and a narrow **event log** — wired into the loop so "who is this?" now
produces *"That's Priya, your daughter. You last saw her two days ago,"* recalled
from the real record, and a `[route:memory]` question ("did I take my medication
today?") is answered from the log rather than guessed. It persists across restarts,
never phones home, and its raw content **provably cannot reach `companion-sync`**
(compile-time proof, extending the Phase-10 `SensorFrame` guarantee). What it does
**not** yet claim is multi-day real-world memory accuracy, note summarization/pruning
so the store stays bounded, or any use by a real wearer — see the Phase 15 section
below.

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
| Real AI adapter: llama.cpp reasoning | **Run against a real (tiny) model in CI** (Phase 12) | `tests/real_llm_test.cpp` in the `real-llm` job — real llama.cpp + Qwen2.5-0.5B-Instruct; route-tag parsing + safe-mode gate verified. **Production reasoning quality (larger model) still unverified** |
| Real AI adapter: openWakeWord wake-word | **Run against real models in CI** (Phase 13) | `tests/real_wakeword_test.cpp` in the `real-engines` job — real 3-stage ONNX pipeline on ONNX Runtime; wake phrase fires, silence/garble/ordinary speech do not (account-free, on fixtures — not field-tested) |
| Real AI adapter: Porcupine wake-word | **Compiles** behind `ECHO_WITH_PORCUPINE`, not run | Phase 3; account-gated (Picovoice key) — kept as a higher-accuracy **production option**, superseded in CI by openWakeWord |
| Real API backends (Spotify/Gmail/Search/YouTube) | **Compiles & links** behind `ECHO_WITH_NETWORK` | Phase 5/6; libcurl 8.21.0; **no live API call has run** |
| JSON parser hardening (depth cap) + Gmail header-injection fix | **Built & tested** | Phase 6 fixes, both with regression tests + a fuzz harness |
| **Memory & recall engine** (people/reminders/events, SQLite-persisted) | **Built & tested** (Phase 15) | [`memory_engine.hpp`](../memory/include/echo/memory/memory_engine.hpp); `echo-memory` unit suite (CRUD, recurrence, no-auto-create rule, RAG medication query, persistence-across-reopen) + `echo-e2e-fixture` face-recall turn — all in the dependency-free stub build |
| **Face-based recall wired into the loop** (perception→cognitive→voice) | **Built & tested** (Phase 15) | a fixture embedding + a prior naming utterance → an enriched "That's Priya…" turn, end to end in `echo-e2e-fixture` |
| **Memory store cannot reach `companion-sync`** (compile-time) | **Built & tested** (Phase 15) | `static_assert`s in `echo-memory` extend the Phase-10 `SensorFrame` proof to embeddings, person records, and events |
| **Fault containment + graceful degradation** (per-engine guard, defined degraded modes, hung-call watchdog) | **Built & tested** (Phase 17) | [`engine_guard.hpp`](../boot/include/echo/boot/engine_guard.hpp) + `boot/src/runtime.cpp`; `echo-fault-injection` injects throw/error/hang at every boundary and asserts no crash, the defined degradation, recovery, and reboot escalation — see the Phase 17 ledger below for the precise scope |
| **Audio robustness under noise** (measured ASR/wake-word degradation vs SNR, single-mic pre-processing, noise-driven safe-mode confidence) | **Measured against synthetic noise in CI; real room unvalidated** (Phase 19) | [`audio_dsp.hpp`](../common/include/echo/audio_dsp.hpp) + `tests/audio_robustness_test.cpp` (`real-engines` job): three checksummed noise beds mixed into the clean clips at clean/+10/0/−5 dB, real whisper + openWakeWord measured at each; a high-pass+gate+AGC pre-processor evaluated; low SNR folded into the transcript confidence so noisy audio rides the **existing** 0.72 safe-mode gate. **Synthetic noise on clean clips — not a real mic in a real room (see the Phase 19 section + gap #13)** |
| **Power/thermal POLICY** (battery vision duty-cycling, thermal inference throttle, low-battery reminder priority) | **Policy built & tested; hardware unvalidated** (Phase 18) | [`power_source.hpp`](../power-mgmt/include/echo/power/power_source.hpp) + [`power_policy.hpp`](../power-mgmt/include/echo/power/power_policy.hpp) wired through `boot/src/runtime.cpp`; `echo-power-mgmt` drives fake power/thermal sources across the threshold bands and asserts the duty-cycle/throttle decisions, the EngineDegraded alert reuse, a reminder still delivered at critical battery, and that a dropped frame is never fabricated. **The real battery/thermal SENSOR is a documented stub — see the Phase 18 section and gap #12** |
| **Orchestration longevity** (no leak/drift over a compressed multi-day run) | **Simulated-time soak green in CI; engine-library longevity still unproven** (Phase 20) | `tests/soak_test.cpp` drives the REAL runtime + REAL memory engine through **10,080 ticks = 7 simulated days** (injected virtual clock, no real sleep): reminder-scheduler drift, event-log retention boundedness, RSS plateau, fd stability, and repeated watchdog recovery — see the Phase 20 section for the measured numbers. It also **found and fixed two real longevity bugs** (recurring-reminder suppression; every-tick flash rewrite). **Proves the orchestration doesn't leak/drift; does NOT prove whisper/llama/OpenCV/ONNX are leak-free over real days — see gap #14** |

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
| 8 | Custom-trained "Hey ECHO" wake word (either backend) | 13 | CI verifies wake-word with openWakeWord's **pretrained** "hey jarvis"; a bespoke "Hey ECHO" model is a *training* undertaking (openWakeWord: synthesize+train; or Porcupine: account-gated `.ppn`) — a documented follow-up, not a bug |
| 9 | **Multi-day, real-world memory accuracy** | 15 | recall is verified against *fixture* embeddings on clean input; whether SFace re-identifies the same person across days, lighting, and aging — and how often it false-matches a stranger — needs the real vision engine + a real wearer over time |
| ~~10~~ | ~~**Note summarization / pruning (bounded growth)**~~ | ~~15~~ | **Closed in Phase 16.** The event log is now bounded by count **and** age, enforced on the existing scheduler tick; per-person notes are bounded by a count cap with oldest-first eviction (not aged out — notes are the long-term value). LLM summarization was considered and **deferred** (Phase 15's documented tiny-model fragility); cap-and-evict shipped instead. Tested in `echo-memory`. |
| 11 | **A real wearer uses recall in daily life** | 15 | no person with memory loss has named someone to ECHO and been reminded of them later; like every other engine, "works on clean fixtures" ≠ "helps a real user" |
| 12 | **Real battery/thermal sensor + real device power/thermal behaviour** | 18 | the power/thermal POLICY (duty-cycle, throttle, reminder priority) is CI-tested against simulated readings, but there is no real fuel gauge or skin-adjacent thermal sensor to read — the backend is a documented stub. Whether these throttle levels keep a temple-worn device comfortable, and whether the low-battery reminder pass fires with enough power left to matter, needs the real hardware. **This is a permanent-until-hardware gap, not a temporary one closeable by more code** (see the Phase 18 section) |
| 13 | **A real mic in a real room** | 19 | Phase 19 measures ASR/wake-word degradation against *synthetic* noise beds mixed into *clean* clips at defined SNRs — real, honest, and CI-provable, but still not a real microphone's frequency response, echo off real walls, or a real human speaking while moving. This **narrows** the live-hardware risk (gap #4) with measured evidence; it does not eliminate it. **Permanent-until-hardware**, like gap #12 (see the Phase 19 section) |
| 14 | **Real third-party engine-library uptime over real days** | 20 | Phase 20's soak proves the ORCHESTRATION layer (runtime loop, scheduler, retention, watchdog, memory store) doesn't leak or drift over 7 *simulated* days with *fake* engines. It does **not** instrument the real engine libraries — whisper.cpp, llama.cpp, OpenCV, ONNX Runtime — so a slow leak *inside* one of those over genuine multi-day wall-clock uptime would not be caught here. Needs the real engines running on real hardware for real days (a superset of gaps #1/#4). **Permanent-until-hardware**, like gaps #12/#13 (see the Phase 20 section) |

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
| `cognitive-core` | **84 %** | 67 % | ⬆ both gate branches now driven end-to-end (low-confidence *and* confident-but-no-LLM); the real `llm.cpp` adapter is compiled out of *this* stub gcov run, but is now exercised for real in the Phase 12 `real-llm` job (route-tag parsing + safe-mode gate against a real tiny model) |
| `perception` | **69 %** | 7 % | ⬆ routing/framing/fusion + input guards covered. The real **ASR (whisper)**, **vision (OpenCV)**, and now **wake-word (openWakeWord)** engines are run against real weights in the `real-engines` job (Phases 11 + 13; separate from this stub gcov run, so they still read 0 here); the wake-gated **endpoint→transcribe** branch stays genuinely uncovered — an explicitly asserted gap, not padding |
| `sensor-pipeline` | **42 %** | 0 % | ⬆ the real `SensorPipeline` + SPSC queue at **85 %**; the two 0 % files are the scaffold no-op stub factories (deliberately bypassed) and the SDL/OpenCV real-capture path (compiled out) |
| `apps/appkit` | **76 %** | 70 % | confirm gate (100 %), JSON, URL, readability; base64 / token-store / http transport still near 0 % |
| `apps/app-framework` | **74 %** | 71 % | supervisor / router / IPC / NLU — still the best-tested app-layer code |
| `apps/*` (8 apps) | ~22 % | ~30 % | mock app logic covered; the **real** network backends (spotify/gmail/youtube/search) near 0 % |
| `common` | 20 % | 20 % | latency + log partly covered; config/types/result thin |
| `power-mgmt` | **↑ (Phase 18)** | 0 % | the pure duty-cycle/throttle policy + the source boundary are now driven directly by `echo-power-mgmt` and exercised through the real runtime; the legacy DVFS `power_manager.cpp` actor and the documented real-backend stub stay uncovered (the stub has no behaviour to assert) |
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

**Phase 12 narrowed the LLM part of that gap too — revisiting a call Phase 11
made.** Phase 11 listed llama.cpp as impractical for CI, but that judgement was
written for a full-size 3B–4B model. Phase 12 uses a genuinely *tiny* instruct
model (Qwen2.5-0.5B-Instruct, Q5_K_M, ~498 MiB — pinned + SHA-256-verified in
[`MANIFEST.md`](../MANIFEST.md)) that runs one short greedy CPU inference in
seconds, so the **real llama.cpp reasoning path now runs in CI** (the `real-llm`
job), not just compiled-out:

- **Route-tag parsing on real output** — the tiny model, given ECHO's real system
  prompt, emits `[route:media]` for "play some jazz music", and the Phase-6-hardened
  parser extracts `media` from the model's actual output format (not an idealized
  stub). — `tests/real_llm_test.cpp`
- **Safe-mode gate with a real LLM wired in** — a low-confidence observation carrying
  that *same* clear command still lands in safe mode: the gate keys on perception
  confidence and short-circuits before the model, so the real LLM is never consulted.
  A **real finding, not routed around** (constraint #4): unlike the stub — which
  returns empty text and so trips the LLM-failure fallback — a real model produces
  confident text for *every* prompt, so the safe-mode guarantee here rests on the
  perception-confidence gate, exactly as the code intends.
- **Sanity generation** — a simple question returns non-empty, non-crashing text.

What this **does not** claim: production reasoning quality. A 0.5B model exists here
only to exercise the real *integration* (load, generate, KV-clear), the parser, and
the gate. The larger model used in the real deployment is **not** validated by this
test — that remains an on-hardware item.

**Phase 13 closed the last real-engine gap — wake-word — the same account-free
way.** Phase 11 left wake-word out only because the Porcupine backend needs an
account-gated Picovoice key. Phase 13 adds **openWakeWord** alongside Porcupine
(behind the same `wake_word.hpp` interface, default backend, Porcupine kept as a
production option): its pretrained three-stage ONNX pipeline runs on ONNX Runtime,
all Apache-2.0 / MIT and anonymously fetchable, so it runs in the `real-engines` CI
job against real models — `tests/real_wakeword_test.cpp`:

- **Fires on the wake phrase** — a Piper-synthesized "hey jarvis" (an *in-distribution*
  positive: openWakeWord itself trains on Piper TTS) clears the detection threshold.
- **Does not false-accept** — ordinary non-wake speech, silence, and loud garble all
  stay below threshold. The test prints every observed score and asserts on the
  model's *actual* probabilistic behavior, not a manufactured perfect separation.

What this **does not** claim: a custom "Hey ECHO" model (it ships the pretrained
"hey jarvis" — training a bespoke word is a documented follow-up, gap #8), nor field
robustness (clean synthetic clips, not a human in a noisy room).

**With Phase 13, every real engine — wake-word, ASR, vision, TTS, LLM — is now
verified against real models in CI on synthetic/fixture input.** What remains is a
single, *permanent* gap that cannot be simulated and is not claimed as covered:

- **Live-hardware behaviour with a real human** — real mic/webcam capture, a person
  speaking and moving naturally in a real room with real background noise, latency
  under load, the SDL audio **playback** path, and the on-hardware sensor DMA /
  real-capture code. This is inherently un-simulatable and stays honestly open.
- (Two narrower, non-"engine" items also remain by nature: **production-grade LLM
  answer quality** — the deployment-size model, vs. the 0.5B CI stand-in — and a
  **custom-trained "Hey ECHO"** word; both are documented above, not engine-integration
  gaps.)

The tests mark each remaining gap explicitly rather than reporting high coverage
on code they never run.

> Coverage is **not gated on a threshold** yet (a deliberate Phase 8 choice — an
> arbitrary floor on a codebase that is still intentionally stubbed at its core
> would reward testing stub code). The number is made visible and trending first;
> a floor comes once the real engines land and the meaningful denominator settles.

## Phase 15 — the memory & recall engine (the product's actual core)

This is the first phase since the scaffold that adds a **genuinely new capability**
rather than making an existing one real. The `memory/` module is a local, private,
on-device store — real **SQLite** (vendored amalgamation, v3.46.1, compiled in-tree;
see [ADR-13](DECISIONS.md)), persisted to `echo_memory.db`, surviving restarts.

**What is now true, and CI-verified in the dependency-free stub build:**

- **People, created only when named.** The wearer saying *"this is my daughter
  Priya"* creates a person record (name + relationship + the SFace embedding). An
  unknown face with **no** naming utterance creates **zero** records — recognizing a
  stranger never invents an identity. Both are asserted (`test_unknown_face_creates_no_person`,
  and end-to-end in `echo-e2e-fixture`).
- **Enriched recall in the loop.** `cognitive-core` matches a face embedding against
  the store and, on a confident match, returns *"That's Priya, your daughter. You
  last saw her two days ago"* — a fact from the record, returned **before** the
  safe-mode gate (recall is retrieval, not an LLM guess). Wired through the real
  perception→cognitive→voice path; the enriched turn is exercised in `echo-e2e-fixture`.
- **Reminders on the existing tick.** `boot/`'s runtime checks due reminders on the
  **same core tick** (no second timer loop) and speaks them through `voice-ui`.
  Recurrence (once/daily/weekly) advances on acknowledge; verified in `echo-memory`.
- **Retrieval-augmented answers.** A `[route:memory]` tag (parsed by Phase 6's
  unchanged route-tag parser) routes "did I take my medication today?" to the engine,
  which answers **from the event log** ("Yes — you took your … at 9:07am" / "Not yet
  today …") or returns `""` so the caller falls back rather than fabricate. Verified
  against the real log in `echo-memory`.
- **Persistence across restart** is proven: a record written, the engine closed, a
  *fresh* engine opened on the same file, and the person (embedding included) recalled
  (`test_persistence_across_reopen`).
- **Privacy, proven not asserted.** The store's raw content — embeddings, person
  records, events — has **no path to `companion-sync`**: compile-time `static_assert`s
  in `echo-memory` extend the Phase-10 `SensorFrame` proof, and were confirmed to
  *bite* (a deliberately-wrong "leak exists" assertion fails to compile). No new
  network path is introduced (constraint #2).

**What this phase honestly does NOT achieve** (also in the gap table, rows 9–11):

- **Encryption at rest.** At rest the store is **file-permission restricted**
  (best-effort owner-only), **not encrypted**. On POSIX that's chmod 0600; on Windows
  `std::filesystem::permissions` maps loosely (ACLs still apply). True encryption is
  **SQLCipher**, a deliberate follow-up ([ADR-13](DECISIONS.md)). **→ Closed in Phase 16**
  (AES-256-CTR over the serialized image; SQLCipher itself was ruled out on this
  toolchain — see the Phase 16 section and [ADR-14](DECISIONS.md)).
- **Real-world recall accuracy.** Tests use *fixture* embeddings (orthogonal for
  strangers, identical for the same person) — the clean-input analogue of the other
  engines' fixtures. Whether SFace re-identifies a real person across days/lighting,
  and its false-match rate, needs the real vision engine + a real wearer.
- **Bounded growth.** Notes and the event log only accumulate today; summarization/
  pruning so the store doesn't grow forever is designed-for (the event scope is kept
  narrow — recognized-person and reminder events, never raw conversation transcripts)
  but not implemented. **→ Closed in Phase 16** (event log capped by count + age on the
  existing tick; notes cap-and-evict; summarization deferred — [ADR-15](DECISIONS.md)).
- **A real wearer.** Same permanent, un-simulatable gap as every engine: no person
  with memory loss has actually used this yet.

**MinGW toolchain reality (constraint #4):** SQLite 3.46.1 compiled **cleanly** on
this MinGW-w64 ucrt g++ 15.1 laptop (~10 s, zero warnings, links and runs first
try) — the opposite of the ORT/OpenCV native-dep friction in Phase 14b. There was no
build gap to work around, so none was invented. (One project-level change: C was
enabled as a language for that single vendored TU.)

## Phase 16 — hardening the memory store (encryption at rest + retention)

Phase 15 shipped the memory engine and, in the section above, named two gaps it left
open on purpose: the store was **file-permission restricted, not encrypted**, and it
had **no retention policy** (unbounded growth). Phase 16 closes both — and, per the
same honest-framing discipline, says exactly what the encryption does and doesn't buy.

**Encryption at rest — what shipped, and why not SQLCipher.** SQLCipher was tested
for real first (the Phase-14b/15 "smoke-test before committing hours" discipline).
The finding is concrete, not hand-waved: SQLCipher is **not** a self-contained
amalgamation like SQLite — it must be generated from a source tree **and** linked
against a crypto backend (OpenSSL `libcrypto`), which is **absent from this MinGW
toolchain** (verified: no `openssl/*` headers, no `libcrypto` — a bare `-lcrypto`
compile fails). Pulling OpenSSL in would break the dependency-free, vendorable stub
build (constraint #3) — the very native-dep friction Phase 14b documented. So, per
constraint #4, we shipped the **documented fallback**: the working database is held
in an **in-memory** SQLite connection (loaded via `sqlite3_deserialize`), and its
serialized image is written to disk as **AES-256-CTR ciphertext**. The AES-256 is a
small, dependency-free, in-tree implementation **proven against the published FIPS-197
and NIST SP 800-38A known-answer vectors** in `echo-memory` (nothing trusted on faith).
Consequences, stated plainly (see [ADR-14](DECISIONS.md)):
- **What it buys:** the on-disk file is ciphertext — a copied/backed-up/imaged `.db`
  is unreadable without the key. A plain unkeyed `sqlite3_open` of the raw file sees
  garbage, and the stored names do not appear in it (both **asserted** in
  `test_encryption_at_rest`). The plaintext SQLite image **never touches disk** — it
  lives only in process RAM.
- **What it does NOT buy:** it is **not** authenticated (no MAC; a wrong key / corruption
  is caught only by a post-decrypt SQLite-magic sanity check, and the engine fails
  **closed**), and it is **not** hardware-backed — an attacker with live read access to
  both the `.db` and the key file can decrypt. Durability is at **checkpoint** (close +
  the retention tick), not per-transaction, and the whole DB is resident in RAM — both
  acceptable for a wearable's small store, and the reason Part 2 bounds its growth.

**Key management — said accurately.** The AES key is a random **per-device key**,
generated once at first boot into a local key file with owner-only permissions — the
same "least-bad local option" already used for OAuth secrets in appkit's
`.echo-tokens/`. It is honestly a protected local key file, **not** a secure-enclave
key; binding it to a TPM or a paired phone is the follow-up ([ADR-14](DECISIONS.md)).

**Migration — a Phase-15 wearer's data is not discarded.** On open, an existing
**unencrypted** Phase-15 `memory.db` (detected by the `SQLite format 3` magic) is
migrated in place to the encrypted format, logged loudly. Proven with a real plaintext
fixture DB checked into `tests/fixtures/` (`test_migration_from_plaintext`): the
person/relation/notes and the reminder survive, and the file is ciphertext afterward.

**Retention — bounded, on the existing tick.** The append-only event log is now capped
by **count and age** (defaults 2000 rows / 90 days, env-overridable), enforced on the
**same scheduler tick** that delivers reminders (no third timer — constraint held).
Pruning touches only the events table, so a retained acknowledgment's reminder state is
untouched — asserted in `test_event_log_retention_bounded` (oldest evicted, newest kept,
the acknowledged reminder still closed). Person **notes** are deliberately **not** aged
out; instead a per-person segment cap evicts oldest-first (asserted in
`test_notes_growth_bounded`), applied both on the tick and immediately on each
`add_note`. LLM summarization was considered and **deferred** to avoid forcing a fragile
tiny-model dependency (Phase 15's documented lesson) — cap-and-evict is the honest,
robust first pass ([ADR-15](DECISIONS.md)).

**Constraints held.** The Phase-15 compile-time privacy proof still bites (re-verified:
inverting a `static_assert` to claim a leak *exists* fails to compile); the
person-record-only-on-naming rule and the reminder-delivery path are unchanged; and the
stub build stays **dependency-free** — the AES and key code are plain in-tree C++, no
new external dependency. All 10 stub-build suites remain green.

## Phase 17 — fault tolerance & graceful degradation

Sixteen phases built the pipeline; none had ever asked what happens when a piece of it
**breaks** mid-turn. That matters more here than in most software: the wearer is, by
design, someone who may not notice or be able to reset a malfunctioning device on their
own face. Phase 17 makes every engine boundary containable and gives each failure a
**deliberate** degraded behaviour — and, in keeping with this file's discipline, says
exactly which failure classes are now handled and which still bring the process down.

**What shipped, and CI-verified in the dependency-free stub build:**

- **Containment at every engine boundary.** wake-word/ASR/vision, LLM, TTS, and memory
  calls run through an [`EngineGuard`](../boot/include/echo/boot/engine_guard.hpp) that
  turns a thrown exception into a `fail(HardwareError)` `Result` and bounds a **hung**
  call (runs it on a worker, abandons it past a per-engine budget → `fail(Timeout)`).
  A `try/catch` backstop wraps the whole tick. Built on the existing `Result`/`Status`
  vocabulary, not a second error channel ([ADR-16](DECISIONS.md)).
- **A defined degraded mode per engine**, decided not accidental: vision failure →
  **voice-only, never a fabricated face match** (constraint #2); ASR failure → a calm
  spoken retry rather than silence; LLM not-responding (throw/**timeout**) → a known-good
  engine-fault line **kept distinct from the low-confidence safe-mode gate**
  (constraint #1 — the gate is an `Ok` response, this is a failed *call*); memory write
  failure → the reminder is **still delivered this session** and not dropped, logged, and
  not repeated every tick.
- **A watchdog on the existing core tick** (no fourth timer — constraint #4) that recovers
  transient faults by construction, attempts in-process re-init of a hung engine, and
  escalates to a `reboot_required()` request when in-process recovery is exhausted.
- **Fault-injection tests** (`tests/fault_injection_test.cpp`, suite `echo-fault-injection`)
  drive the **real runtime** with fault-injecting fake engines and assert, at every
  boundary and for throw/error/hang: the process doesn't crash, the defined degraded
  behaviour happens, a transient clears on the next turn, and a persistent hang escalates
  to reboot. **11 stub-build suites now green** (was 10).

**The precise ledger — what is fault-tolerant, what is not.** This is a safety claim, so
it is stated per class, not summarized optimistically:

| Failure class | Handled how | Crashes the process? | Needs physical reboot? |
|---|---|---|---|
| Engine **throws** an exception (any of vision/ASR/LLM/TTS/memory) | Contained → `fail(HardwareError)`; defined degraded mode; next call recovers | **No** | No |
| Engine returns a **non-Ok `Status`/`Result`** | Same containment/degraded path as a throw | **No** | No |
| Engine **hangs** (never returns), recoverable by re-init | Detected via timeout; worker abandoned; pipeline continues degraded; re-init restores it | **No** | No |
| Engine **hangs** and re-init also hangs (wedged thread / driver / held lock) | Detected; degraded fallbacks continue; after `max_recoveries` → `reboot_required()` | No (loop keeps running) | **Yes** — supervised restart is the honest fallback |
| Unhandled failure **outside** the guarded boundaries handled as a C++ exception | Caught by the per-tick `try/catch` backstop | **No** | No |
| **Segfault / memory corruption / UB / `noexcept`-violation `std::terminate` / OOM** | **Not containable** by `try/catch` | **Yes** | **Yes** — needs a supervising init system or hardware watchdog |

**Honest limitations (not worked around, documented):**

- **A wedged thread leaks and can block shutdown.** C++ can't safely kill a
  non-returning call, and a `std::async` future's destructor joins — so a truly wedged
  engine leaks one worker until process restart and may stall process exit. This is why
  the reboot path exists rather than a pretence of always-recover.
- **The hang bound costs hot-path overhead** — a worker dispatch + a copy of each call's
  argument. Acceptable for the scaffold's small per-tick call count, heavier than the
  "zero jank" ideal; a production build would use a persistent per-engine worker or a
  hardware watchdog timer ([ADR-16](DECISIONS.md)).
- **The watchdog's "recovery" is optimistic for responsive-but-broken engines.** An
  engine that re-initializes cleanly but keeps failing on every call is served its
  degraded fallback **indefinitely** rather than rebooting the whole device (a
  responsive engine isn't "wedged") — correct, but it means not every persistent fault
  ends in a reboot, only genuine hangs.
- **Same permanent gap as every phase:** this is verified with injected faults on fakes,
  not a real engine crashing on real hardware on a real wearer's face. "Contains injected
  faults in the stub build" ≠ "keeps a real device usable through a real subsystem failure
  in the field."

## Phase 18 — power & thermal management

`power-mgmt` had been a top-level module directory since the very first scaffold, named
alongside boot, perception, cognitive-core, and the rest — but across seventeen phases it
was never given real logic (a placeholder DVFS actor and a hardcoded `evaluate()` call were
all it had). That matters more here than in most devices: this is worn all day by someone who
may not notice a dead battery, an uncomfortably hot temple, or a device silently throttling
itself into uselessness by mid-afternoon. Phase 17 made ECHO OS resilient to *software*
failures; Phase 18 makes it resilient to the *physical* reality of limited power and real heat
— and, per this file's discipline, is precise about what is policy-verified versus still
hardware-unvalidated.

**What shipped, and CI-verified in the dependency-free stub build:**

- **A real sensing boundary + a deterministic fake.** `IPowerSource` (battery percent +
  charging) and `IThermalSource` (a coarse `ThermalState` nominal/warm/hot, plus an optional
  numeric SoC temp as telemetry) mirror the `IHttpClient`/engine-interface discipline: a real
  backend hook and a fully deterministic fake (`fake_power_source.hpp`). The thermal signal is
  a discrete STATE by choice ([ADR-17](DECISIONS.md)): a temple-worn device's realistic input is
  a few thermal-zone trip points, not a calibrated skin temperature, and the policy needs a
  throttle *level*, not a setpoint.
- **Battery-driven vision duty-cycling.** Continuous camera face-detection is the expensive,
  always-on cost, so the vision sampling cadence drops as charge falls: **full-rate above 40 %,
  reduced (1-in-4) 15–40 %, vision-off / voice-only below 15 %**. A dropped frame is exactly
  that — DROPPED. Recognition simply happens less often; nothing is ever fabricated to hide the
  lower rate (the same non-negotiable "never guess" rule as Phase 15's naming-gate and Phase
  17's degraded modes). Microphone frames are never duty-cycled, so a vision-off device stays
  fully responsive to speech.
- **A thermal-aware inference throttle, wired INTO the Phase-17 watchdog (not beside it).** A
  warm/hot temple relaxes the cognitive hang budget (×1.5 / ×2.0) so a legitimately-slower
  throttled turn is not abandoned as if it were hung — the throttle scales the *existing* guard
  budget up rather than adding a parallel timer. Thermal pressure also forces at-least-reduced
  vision even on a full battery.
- **Low-battery / high-thermal ride the SAME caregiver channel.** Both surface through the
  existing `AlertKind::EngineDegraded` alert path (edge-triggered, so a standing condition alerts
  once), NOT a second device-health mechanism — the brief's constraint, honoured. The specific
  condition is in the alert note.
- **A low-battery critical-reminder pass.** When the battery crosses a critical floor, a
  best-effort final delivery of due reminders fires before a possible shutdown, and reminder
  delivery is never duty-cycled or throttled away — a low battery must not be what silences a
  medication reminder. (Honest scope below.)
- **A source read-fault is contained and never fabricates.** A throwing gauge read neither
  crashes the loop nor invents a charge level: the runtime holds the last-known-good decision
  (default: healthy, so a transient glitch doesn't needlessly cripple vision) and raises one
  sensing heads-up.
- **Tests** (`tests/power_mgmt_test.cpp`, suite `echo-power-mgmt`) assert all of the above in two
  layers: the pure policy against scripted readings, and the same behaviour through the REAL
  runtime driven by fake sources over the defined threshold transitions. The Phase-17
  fault-injection fakes were extracted into a shared `tests/fake_engines.hpp` and reused rather
  than duplicated. **12 stub-build suites now green** (was 11); fault-injection stays green on
  the shared fakes.

**What this phase honestly does NOT achieve — a NEW, permanent-until-hardware gap (#12):** This
closes a **design and policy** gap, *not* a hardware-validation one, and the two must not be
conflated (the mistake avoided since Phase 9).

- **No real sensor exists to read.** There is no fuel gauge or skin-adjacent thermal sensor on a
  dev laptop or on the not-yet-existing glasses, so the real backend is a **documented stub**:
  the *shape* of the sysfs / fuel-gauge read is captured (this is exactly where a
  `/sys/class/power_supply/*/capacity` or a `/sys/class/thermal/thermal_zone*/temp` read goes),
  the *values* are simulated. The policy is real and tested; the sensor behind it is not.
- **The critical-reminder pass is doubly speculative.** It fires on a low-charge THRESHOLD, not
  a real "about to die" signal (predicting imminent shutdown needs hardware), and there is no
  guaranteed post-alert power budget. The policy — deliver-before-loss, never silence a reminder
  for power — is testable; the shutdown prediction it rests on is not, the same way the live-mic
  gap has stood open since Phase 9.
- **Real thermal behaviour is unproven.** Whether these throttle levels actually keep a
  temple-worn device comfortable, and how fast real skin-adjacent hardware heats, is not
  something a stub build can claim. Only the policy LOGIC is verified.

## Phase 19 — audio robustness under noise (a measurement, not a claim)

Every ASR and wake-word test since Phase 10 fed the real engines **clean** audio: a
Piper-synthesized phrase over synthetic silence. That proved the engines *work at
all*, but never asked what a real room does to them — background hum, a door
clattering, a second person talking. That gap is very likely the biggest reason the
still-pending **live human test** (gap #4, open since Phase 9) could disappoint. Phase
19 derisks it with real, CI-provable engineering instead of finding out the hard way —
and, unlike every prior phase, its headline deliverable is partly a **measurement**:
the numbers below are what the real engines actually did, not a target they were tuned
to hit.

**What shipped, and CI-verified in the `real-engines` job:**

- **Noise-augmented fixtures at defined SNR levels.** Three synthetic **noise beds** —
  `hum` (steady low-frequency), `transient` (bursty clatter), `babble` (a competing
  talker) — are generated by [`make_noise_fixtures.py`](../tests/fixtures/make_noise_fixtures.py)
  (integer-only, so byte-identical everywhere) and **checksum-verified** against
  [`MANIFEST.md`](../MANIFEST.md), then mixed into the clean speech/wake clips at
  **clean / +10 dB / 0 dB / −5 dB** (`20·log10(rms_speech/rms_noise)`, whole-clip RMS).
- **Measured real degradation, honestly.** `tests/audio_robustness_test.cpp` runs the
  real **whisper.cpp** (`tiny.en`) and real **openWakeWord** engines against every
  bed × SNR combination and records their *actual* behaviour (below).
- **A basic single-mic pre-processing stage** in the capture path
  ([`AudioPreprocessor`](../common/include/echo/audio_dsp.hpp): first-order high-pass +
  a gentle noise-floor gate + AGC), wired into the SDL mic source
  (`sensor-pipeline/src/real_sources.cpp`) and evaluated against the same noisy clips.
  **Single-mic by design** — the capture path is one mono I2S/SDL microphone (confirmed
  against the real capture code, not assumed), so there is deliberately **no
  beamforming**; if the glasses ever ship a mic array, that is where it would go.
- **A noise-driven confidence signal into the EXISTING safe-mode gate.** The engine
  estimates the utterance SNR and folds a `noise_confidence(snr)` into the transcript
  confidence (weakest-link). Heavily-degraded audio therefore drops below the
  unchanged **0.72** cognitive-core gate and routes through the **same** Phase-17
  "ASR degraded → ask again calmly" fallback — **no parallel mechanism** (constraint #3).

### Measured accuracy vs. SNR (real engines, this commit)

Numbers from the real `tiny.en` + openWakeWord engines on this dev laptop (the
`real-engines` CI job reproduces them). `hit` = an expected content word is present;
`conf` = whisper's mean token probability; `SNR est` = the engine's own SNR estimate
of the mixed clip; `→pp` = after the pre-processor. Wake `peak` = openWakeWord's peak
score (fires at ≥ 0.50).

| Bed | SNR | ASR `hit` | ASR `conf` | SNR est → pp | Safe-mode gate | Wake peak → pp |
|-----|-----|:---------:|:----------:|:------------:|:--------------:|:--------------:|
| — | clean | ✅ | 0.88 | 44.5 → 50.6 | pass | 0.998 → 0.998 |
| hum | +10 dB | ✅ | 0.88 | 14.5 → 22.3 | pass | 0.998 → 0.998 |
| hum | 0 dB | ✅ | 0.87 | 4.5 → 12.9 | **ask again** | 0.998 → 0.998 |
| hum | −5 dB | ✅ | 0.89 | −0.3 → 9.3 | **ask again** | 0.998 → 0.997 |
| transient | +10 dB | ✅ | 0.89 | 38.9 → 45.1 | pass | 0.998 → 0.998 |
| transient | 0 dB | ✅ | 0.90 | 31.1 → 37.0 | pass | 0.999 → 0.999 |
| transient | −5 dB | ✅ | 0.89 | 27.2 → 33.1 | pass | 0.999 → 0.999 |
| babble | +10 dB | ✅ | 0.87 | 18.5 → 25.3 | pass | 0.997 → 0.997 |
| babble | 0 dB | ✅ | 0.87 | 9.9 → 16.8 | **ask again** | 0.995 → 0.993 |
| babble | −5 dB | ✅ | 0.87 | 8.5 → 14.9 | **ask again** | 0.995 → 0.993 |

**What the numbers actually say — read honestly, including the surprises:**

- **Both engines were markedly robust to these synthetic beds.** `tiny.en` transcribed
  the phrase correctly and openWakeWord fired (~0.99) at *every* level, including −5 dB.
  That is the measured result — and it comes with a **methodology caveat stated up
  front, not buried:** the SNR is defined on *whole-clip* RMS, and the TTS clip carries
  leading/trailing silence, so the effective SNR *during the spoken words* is higher
  than the nominal label. Synthetic, stationary noise mixed into a clean TTS clip is
  also gentler than a real room. So "robust here" means "robust on this synthetic
  bench," which is exactly why gap #13 (a real mic in a real room) stays open.
- **The pre-processing helps where a single-mic linear filter *can* help, and honestly
  not where it can't.** The high-pass lifts the estimated SNR most on the low-frequency
  `hum` bed (−0.3 → +9.3 dB at −5 dB) and least on `babble` (a talker overlapping the
  speech spectrum — the case a single mic fundamentally cannot separate). It did **not**
  change the already-correct transcription on this set, and it slightly *lowered* the
  saturated wake-word score — so its measured value here is "improves the SNR margin,
  especially for steady hum, without changing output the engine already got right,"
  **not** "rescues a failing case." That is the honest account the brief asked for.
- **The noise gate fires exactly on the stationary noise, and correctly abstains on the
  bursty one.** At ≤ 0 dB of `hum`/`babble` the folded confidence falls below 0.72, so
  the system asks again calmly rather than risk a confident mis-hear; `transient` reads
  a high stationary SNR (the speech *between* clatters is clean) and is left usable.
  This is **deliberately conservative** — it will ask again on some audio `tiny.en`
  could in fact handle — which is the right bias for a device worn by someone with
  memory loss (principle #5, "fail safe, not smart"), and is stated as a bias, not hidden.

**Constraints honored (Phase 19):**

- Thresholds are **not** tuned to flatter the result. −5 dB genuinely did *not* break
  `tiny.en` on these beds, and this file says so plainly rather than manufacturing a
  scary-then-rescued arc; the gate trips on measured SNR, not on a number picked to
  look good.
- The **stub build stays dependency-free** — the noise DSP is header-only standard C++,
  the whole measurement lives in the real-engines job, and all **12 stub suites remain
  green** (the perception SNR-folding compiles into the stub build but is inert there,
  since the stub ASR returns no transcript to fold into).
- The noise-driven low confidence rides the **Phase-17 degradation vocabulary** (the
  0.72 gate + the "ask again" line), not a new notification path.
- Single-mic verified against `sensor-pipeline/src/real_sources.cpp` before designing
  the pre-processor — no array was assumed, so no beamformer was built.

**What this phase does NOT derisk (gap #13, permanent-until-hardware):** it validates
against *synthetic* noise mixed into *clean* fixtures — **not** a real physical room's
acoustics: a real microphone's frequency response, echo off real walls, reverberation,
a real human speaking naturally while moving. It **narrows** the live-human risk with
measured evidence; it does not remove the need for the live run. The true test is still
a real wearer in a real room.

## Phase 20 — longevity & resource-leak soak (does it hold up over days, not seconds?)

Every test before this one exercised a single turn, a single fault, or a bounded
scenario. None of them proved the thing a device *worn all day, every day* needs most:
that it runs **continuously, for a long time, without degrading**. Phase 20 builds the
harness that actually looks for that class of bug — a memory creep, a scheduler that
drifts after enough ticks, a watchdog recovery that leaks a worker each time, a
retention pass that never really runs at scale — and puts **real measured numbers** on
each.

**How it's done honestly.** `tests/soak_test.cpp` drives the **real** runtime
(`boot/runtime.cpp`) with the **real** memory engine (SQLite + encryption at rest) and
the shared Phase-17/18 deterministic fakes for the other five engines, through a large
number of ticks — but in **simulated time**, not real sleeping. The runtime gained a
one-line clock seam (`Runtime::set_clock`) so the reminder scheduler reads an injected
**virtual clock**; the soak advances it 60 simulated seconds per tick. That compresses
**10,080 ticks into 7 simulated days** and runs in **~80 s** on the MinGW dev box (the
dedicated Linux CI job, build + run, finishes in ~1.5 min) — the only honest way to reach
a multi-day duration inside a CI budget. Run length is `ECHO_SOAK_TICKS`-overridable for a
longer local run.

**Measured numbers (default 10,080-tick / 7-simulated-day run, this commit):**

| Metric | Bar | Measured | Verdict |
|--------|-----|----------|---------|
| Reminder-scheduler drift (Phase 15, 600 one-shots across the week) | each fires once, within one tick, no cumulative drift | 600/600 delivered, **0 early**, **worst drift 59 s** (< the 60 s tick) | ✅ no drift |
| Recurring reminder re-arm across days | fires on **every** acknowledged occurrence | 4/4 daily occurrences fired | ✅ (bug fixed — see below) |
| Event-log retention at scale (Phase 16) | log stays ≤ cap despite thousands of firings | **200 rows** at cap=200 after 600 firings | ✅ bounded |
| Process RSS plateau | post-warmup growth < 15 % and < 16 MiB | **+~0.1 MiB (~1–2 %)** over the run | ✅ plateau |
| Open fd / handle count | no growth with save-to-disk churn | span **≤ 5** across the run | ✅ flat |
| Memory re-init (watchdog close+open) fd stability | no fd leak per recovery cycle | **300 cycles, fd unchanged** | ✅ no leak |
| Watchdog recovery (Phase 17), transient hang every 900 ticks | recovers every time, never reboots, no worker accumulation | **11 hang episodes, 0 reboots, peak 1 abandoned worker, drains to 0** | ✅ contained |

The scheduler, retention, and watchdog rows are **deterministic** (identical on every
platform). The RSS/fd rows are the **MinGW dev-box** measurement (exact bytes vary by
allocator/OS); the Linux CI `soak` job re-runs them against `/proc` and clears the same
bars — that pass is the portable proof, the byte figures above are the illustrative
sample.

**Two genuine longevity bugs found — and root-cause fixed (not papered over):**

1. **Recurring reminders went silent after day one.** The runtime de-duplicated
   in-session reminder delivery by reminder *id*, permanently. A recurring reminder that
   the wearer acknowledges *re-arms* to a new due time (`memory_engine` advances it), but
   the id-only record suppressed every occurrence after the first — so a **daily
   medication reminder would fire once and never again until reboot**. On an all-day
   device that is a safety-relevant silent failure. Fixed by keying the in-session record
   by *occurrence* (reminder id → the delivered `due` time): a re-armed occurrence has a
   new `due` and is delivered, while the original backstop (a reminder whose `mark_fired`
   write failed, so it stays "pending" on the *same* occurrence) is preserved. Regression
   test: the recurring-reminder sub-test asserts 4/4 daily occurrences fire.
   (`boot/include/echo/boot/runtime.hpp` `delivered_occurrence_`.)

2. **The store rewrote its entire encrypted image to flash on every tick.** Retention
   runs on the core tick; `prune_events` called `touch()` (marking the store dirty) every
   time the DELETE *executed*, even when it evicted **nothing** — which is the common case
   once the log is at its cap. A dirty store triggers a full `sqlite3_serialize` +
   AES-256 + file rewrite in `enforce_retention`. So a device sitting idle re-encrypted and
   rewrote the whole database **every tick, forever** — constant flash wear and CPU
   proportional to store size, for no state change. (This is also what made the soak
   infeasibly slow before the fix — ~170 ms/tick.) Fixed by gating the dirty flag on
   `sqlite3_changes(db_) > 0` so an idle retention pass is a couple of cheap SELECTs and
   **no disk write**; a real eviction still persists exactly as before. Verified by the
   existing `echo-memory` suite (retention behaviour unchanged) and by the soak's own
   throughput. (`memory/src/memory_engine.cpp` `prune_events`.)

**What this phase proves — and what it explicitly does NOT (gap #14, permanent-until-hardware).**
It proves the **orchestration layer** — the runtime loop, the reminder scheduler, the
retention/eviction path, the fault-tolerance watchdog, and the memory store's own
serialize/encrypt/persist cycle — does not leak or drift over a compressed multi-day run.
It does **not** prove the real third-party engine libraries (whisper.cpp, llama.cpp,
OpenCV, ONNX Runtime) are leak-free over real multi-day *wall-clock* uptime: this harness
drives *fakes* in their place and does not instrument those libraries, so a slow leak
*inside* one of them would not be caught here. That remains gap #14, provable only by real
engines on real hardware for real days. Simulated-time soak with fakes is a real,
valuable longevity proof of everything ECHO actually wrote — and honest about the boundary
where third-party code and real time take over.

## Quality gates in place (CI)

Every push and PR to `master` runs [`.github/workflows/ci.yml`](../.github/workflows/ci.yml):

| Gate | What it enforces |
|------|------------------|
| **Stub build + full ctest** | dependency-free build, all fast suites (incl. Phase 15 `echo-memory`, the memory-enriched `echo-e2e-fixture`, Phase 17 `echo-fault-injection`, and Phase 18 `echo-power-mgmt`) — the always-green safety net. The vendored SQLite amalgamation compiles in-tree here with zero external deps. The longer Phase 20 `echo-soak` is excluded here and runs in its own job (below) so per-PR feedback stays fast |
| **Longevity soak** (Phase 20) | its own dependency-free job builds & runs `echo-soak` — the real runtime + real memory engine through **7 simulated days** (10,080 ticks, virtual clock) — asserting reminder drift, retention boundedness, RSS plateau, fd stability, and watchdog-recovery worker-leak stability. Scoped like the real-LLM job (separate, time-bounded) so it never slows the fast suites |
| **clang-tidy / cppcheck scope** | both now cover the first-party `memory/` code; the vendored `memory/vendor/sqlite3/` amalgamation is **excluded** from both (upstream C we compile but do not lint) |
| **Network build** | `-DECHO_WITH_NETWORK=ON` compiles & links libcurl; appkit/apps tests pass on a clean machine (no live API calls) |
| **Secret scan** | `check_secrets.sh` blocks a committed API key, OAuth secret, private key, token cache, or `.env` |
| **clang-tidy** | `bugprone-*` / `cert-*` / `clang-analyzer-*` over every first-party TU; findings are hard errors |
| **cppcheck** | second independent analyzer; `warning`/`performance`/`portability` fail the job |
| **Fuzz (JSON)** | bounded libFuzzer under ASan/UBSan on the one parser that eats untrusted bytes |
| **Coverage** | gcov + gcovr; % printed to the run summary, XML to Codecov |
| **Real engines** (Phases 11 + 13 + 19) | `-DECHO_WITH_WHISPER/OPENCV/PIPER/OPENWAKEWORD=ON`; downloads + **SHA-256-verifies** the free models (incl. the openWakeWord ONNX pipeline + ONNX Runtime) and runs `tests/real_*` (ASR, vision, TTS, **wake-word**) against real weights on the fixtures + Piper-synthesized speech. **Phase 19** adds `audio_robustness` — generates + checksum-verifies the noise beds and measures ASR/wake-word degradation across defined SNR levels. Runs in parallel; models + whisper build cached |
| **Real LLM** (Phase 12) | `-DECHO_WITH_LLAMA=ON`; builds llama.cpp + downloads/**SHA-256-verifies** a tiny instruct model and runs `tests/real_llm_test.cpp` — route-tag parsing + safe-mode gate against a real model's real output. Runs in parallel; model + llama build cached |

Two one-time repo-admin steps remain outside code: **branch protection** (require
these checks before merge) and **enabling Codecov** (the badge reads `unknown`
until then; the percentage is visible in every run regardless).

The `real-engines` job (Phases 11 + 13) runs whisper.cpp, OpenCV, Piper, and — now —
openWakeWord against real models in CI, and the `real-llm` job (Phase 12) adds the
real llama.cpp reasoning path against a **tiny** instruct model — all free,
account-free, and small enough to cache. What stays deliberately **excluded**:
**Porcupine** (account-gated key — kept only as a production wake-word option, since
openWakeWord now covers wake-word in CI account-free), and **production-grade LLM
reasoning** — Phase 12 verifies the llama.cpp *integration* + parser + gate with a
0.5B model, but a deployment-size model's answer quality is a multi-GB, on-hardware
concern, not a per-commit CI one. Every model any job downloads is SHA-256-verified
before use (running unverified third-party model binaries in CI is a supply-chain
risk, not a hypothetical one).

## Known issues / inconsistencies noted during this doc pass

Per Phase 9 constraint #1 (documentation only, no source changes), anything
surfaced while writing these docs is recorded here rather than fixed:

- *None found in this pass.* The repo's existing honest style holds up — the
  README's Phase 4/5 placeholder tables, the "compiles but not run live" caveats,
  and the code comments all match what the tree actually does. If a future doc
  pass finds a real discrepancy, list it here with a file/line pointer.

## Phase 14 execution findings — real bring-up on the Windows/MinGW dev laptop

Recorded during the Phase 14 agent-side bring-up (real toolchain: MinGW-w64 ucrt
g++ 15.1, CMake 4.0, on Windows 11). These are genuine machine-specific signals
CI's Ubuntu runners could not have surfaced. What the agent *cannot* do (speak
into the physical mic, present a real face to the webcam, be the outside tester,
and therefore produce the ≥20 spoken-turn latency numbers) is left for the human —
see "What's pending real-world validation" above; those rows are **not** closed.

- ✅ **Level 0 stub build + full CTest gate pass** on this machine: `cmake -B
  build … && ctest` → 9/9 suites green (`echo-smoke`, `echo-sensor`,
  `echo-perception`, `echo-voice`, `echo-companion`, `echo-e2e-fixture`,
  `echo-appkit-tests`, `echo-apps-smoke`, `echo-nlu-routing`). `echo-demo --text`
  routes and fires the safe-mode gate correctly in stub mode.
- ✅ **Pinned models fetched + checksum-verified** into `models/`: whisper
  `ggml-tiny.en.bin`, `llm.gguf` (Qwen2.5-0.5B-Instruct Q5_K_M), openWakeWord
  trio, YuNet(2022mar)/SFace — all byte-identical to the MANIFEST SHA-256s.
- ✅ **Real LLM engine built and run headlessly**: `-DECHO_WITH_LLAMA=ON` against a
  MinGW-built llama.cpp resolves via `find_package(llama CONFIG)`, `llm.cpp`
  compiles clean with **KV-clear=`current`**, and `echo-demo --text` loads the real
  Qwen2.5-0.5B model and returns coherent reasoning (e.g. an open-ended "what
  should I do if I feel lost" → a real, on-topic answer). It does **not** falsely
  trip safe-mode — consistent with the Phase 12 finding that a real model never
  returns empty text. The real cognitive inference **overran the 120 ms budget by
  ~10×**, as the RUNBOOK predicts for a laptop CPU. (This is a *typed-text,
  cognitive-stage-only* observation — **not** a spoken-turn measurement, so it is
  deliberately kept out of the README/STATE latency tables, which still await the
  human's ≥20 real spoken turns.)
### Phase 14b — Windows native-dependency gap CLOSED

The three native libraries that blocked `-DECHO_REAL_AI=ON` in the first pass
(ONNX Runtime, OpenCV, SDL2) are now installed with this exact MinGW-ucrt
toolchain, and **the full real-engine build configures, compiles, links, and runs
a scripted turn on this Windows laptop.** Installed versions:

| Dep | Version | How it was obtained on Windows/MinGW |
|-----|---------|--------------------------------------|
| ONNX Runtime | 1.17.3 (win-x64) | official MSVC DLL; C-ABI only, so linked from MinGW via a `dlltool`-generated `libonnxruntime.dll.a` import lib |
| OpenCV | 4.10.0 | built from source with this toolchain (C++-ABI, so an MSVC build would **not** link) — modules core/imgproc/imgcodecs/videoio/objdetect/dnn |
| SDL2 / SDL2_ttf | 2.30.9 / 2.22.0 | official prebuilt MinGW devel packages |
| whisper.cpp | v1.9.1 | built `-DWHISPER_USE_SYSTEM_GGML=ON` against llama's ggml (see collision fix below) |
| llama.cpp | (Phase-12 MinGW build) | reused; provides the single shared ggml |
| Piper + voice | 2023.11.14-2 win-amd64 + `en_US-amy-medium` | official Windows binary; voice sha256 matches the MANIFEST pin |

**Two real, previously-latent bugs were found and fixed on the way (fix, not route-around):**

- **ORT header won't parse under MinGW** — root cause was *not* a hard wall: ORT's
  `onnxruntime_c_api.h` spells its calling convention `_stdcall` (an MSVC-only
  keyword) on the `_WIN32` branch. On x64 that convention is a no-op, so
  `cmake/echo_ai.cmake` now maps `_stdcall`→`__stdcall` for MinGW on the ORT
  interface target. The header then compiles and the real openWakeWord backend
  **links and runs** (verified: `Ort::Env` + version query succeed). This corrects
  the earlier Phase-13 note that ORT "does not compile on MinGW" — it does, with a
  one-token define.
- **Piper TTS silently failed on Windows** — `voice_ui.cpp` builds the piper
  command with several quoted tokens and runs it via `std::system()`, i.e. through
  **cmd.exe**, which strips the outer quote pair of the whole line and corrupts the
  command when the piper path contains spaces (`C:\Users\First Last\…`). Linux CI
  paths had no spaces, so it never surfaced. Fixed by wrapping the whole command in
  one extra quote pair on `_WIN32`. Verified: piper now synthesizes real audio.

- **ggml symbol/DLL collision (whisper vs llama)** — a genuine integration trap:
  whisper v1.7.4 and the llama build ship **different ggml revisions under identical
  DLL/symbol names** (`ggml.dll`, `ggml_*`), which cannot coexist in one process.
  Resolved by building whisper **v1.9.1** with `WHISPER_USE_SYSTEM_GGML=ON` so it
  links llama's ggml — one ggml for both engines. The three engines also live in
  **separate CMake prefixes** (shared: OpenCV/SDL2/ORT; llama; whisper) to keep
  their headers from cross-contaminating.

✅ **`echo-demo --text` load sanity (build/load check, NOT a live-hardware test):**
all five real engines initialize on this machine — `wake-word ready (openWakeWord,
ONNX 3-stage pipeline)`, `ASR ready (whisper.cpp)`, `vision ready (OpenCV)`, `LLM
ready (llama.cpp)`, `voice UI initialized (Piper TTS + SDL audio)` — and a scripted
`--text` turn runs end-to-end without crashing, real TTS included. This confirms the
build and the engine **loads**; it does **not** validate wake-word accuracy, face
recognition, or voice quality — those need real mic/webcam/ears and remain the
human's (see below).

> Still exclusively the human's, unchanged by 14b: speaking "Hey ECHO" into the
> physical mic, real face-recognition testing, the ≥20 real spoken-turn latency
> run, and a non-founder tester. No latency numbers were written to the README/STATE
> tables — those must come from real spoken turns, not this scripted load check.

## Bottom line for a new reader

- **Trust the contracts.** Interfaces, isolation boundary, safe-mode gate, latency
  budget, secret hygiene, and the mock/real seams are real and mostly tested.
- **Distrust any "it works on hardware" claim** — that step (real models, real
  APIs, real users, measured latency) has not happened yet, and the docs say so
  everywhere it matters.
- **The next engineer's first job** is the real bring-up in [RUNBOOK.md](RUNBOOK.md)
  and closing the core-loop test gap above.
