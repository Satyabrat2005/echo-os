# ECHO OS — Phase Ledger (1 → 22)

*How the project got here, phase by phase, and the house rules a new phase
inherits. This is the companion to [STATE.md](STATE.md): **STATE.md is the source of
truth for what is true right now**; this file is the history and the working method.
Read STATE.md first for status, this file first for planning.*

Phase briefs (the "what to build" documents) live in [phases.md](phases.md) —
verbatim for Phases 13 → 22. Phases 1–12 predate that file and are recorded only as
prose in `README.md`, `DECISIONS.md`, and `STATE.md`; their rows below are
reconstructed from those sources, and where the record is thin this file says so
rather than inventing detail.

---

## 1. The non-negotiables every phase inherits

Twenty phases have converged on the same handful of rules. They are not style
preferences — most of them exist because breaking one would make a claim in
STATE.md untrue.

**Product principles**

- The five design principles in `README.md`, and the safe-mode gate at **0.72**
  (ADR-4). Fail safe, not smart.
- **Never fabricate.** A dropped frame is dropped, not interpolated. An
  unrecognized stranger gets no invented identity. A degraded engine never
  reports a match that didn't happen. A low battery never silences a reminder by
  pretending there was nothing to deliver. This rule has been restated
  independently in Phases 15, 17, 18, and 20 — treat any new feature that
  "smooths over" a gap as suspect by default.
- **Privacy is proven, not asserted.** The compile-time `static_assert`s in
  `echo-companion` and `echo-memory` are the mechanism. Extend them; never
  weaken them. After extending, **invert one to confirm it still fails to
  compile** — a proof that doesn't bite is decoration (Phases 15 and 16 both did
  this deliberately).

**Engineering rules**

- **The dependency-free stub build stays green and dependency-free** (ADR-8).
  Every new capability needs a no-real-engines test path. If a capability can
  only be tested with a real model, the phase's first job is to build the seam
  that fixes that (see Phase 21, item 1).
- **Reuse the existing core tick.** Reminders (15), retention (16), the watchdog
  (17), and the power policy (18) all ride it. No new timer loop has ever been
  added, and none should be.
- **Reuse the existing vocabularies.** `Result`/`Status` for errors (not a second
  error channel), `AlertKind::EngineDegraded` for device-health, the Phase-17
  degradation lines for spoken fallbacks. Phase 18 explicitly reused
  `EngineDegraded` rather than adding an alert kind; Phase 19 folded noise into
  the *existing* 0.72 gate rather than adding a second gate.
- **Keep failure modes distinguishable.** Phase 17 kept "the engine didn't
  respond" separate from "the input was unclear" on purpose. Each distinct reason
  ECHO declines gets its own line and its own caregiver posture.
- **Checksum and pin every downloaded asset** in `MANIFEST.md` before use.
  Running unverified third-party model binaries in CI is a supply-chain risk,
  not a hypothetical.
- **Smoke-test a heavy dependency before committing hours to it.** Phase 14b
  (ONNX Runtime), Phase 15 (SQLite), and Phase 16 (SQLCipher) all did this; Phase
  16's smoke test is why SQLCipher was correctly abandoned in favour of a
  shippable fallback instead of consuming the phase.

**Documentation rules**

- **Honest STATE.md, every time.** Measurements are reported as measured, never
  tuned to look good. A disappointing number or a bug found is a legitimate
  phase outcome — Phase 19's "the engines were more robust than expected, and
  here is the methodology caveat that explains why" and Phase 20's two-bug
  writeup are the model.
- **Never conflate *policy-verified* with *hardware-validated*.** This is the
  single most repeated instruction in the docs (Phases 9, 11, 12, 13, 18, 19,
  20). Every phase that touches something physical must state which side of that
  line it landed on.
- A gap that no amount of code can close is labelled **permanent-until-hardware**
  (#12, #13, #14) so nobody re-plans it as work.

---

## 2. Phase index

| # | Title | Status | Headline outcome | ADRs |
|---|-------|--------|------------------|------|
| 1 | Scaffold: modules, contracts, the core loop | Done | 8 static-lib modules, lock-free SPSC ring buffer, 120 ms budget as an enforced contract, safe-mode gate, `boot/` runtime, dependency-free stub build | 1, 2, 4, 5, 8 |
| 2 | Apps layer + isolation | Done | 8 apps as supervised separate processes, IPC + router + NLU, HUD's three overlay primitives, `echo::app-sdk` links no core module | 3, 6, 7 |
| 3 | Real local AI, opt-in | Done | whisper.cpp / llama.cpp / Piper / Porcupine / OpenCV / SDL adapters behind `ECHO_WITH_*`, `echo-demo`, latency logging — all default OFF | 8, 11 |
| 4 | Real-world bring-up & validation | **Open (human)** | The plan exists; the run does not. Gaps #1–#4 | — |
| 5 | Real third-party integrations | Done (code) | Spotify / Gmail / Search / YouTube backends + `appkit` (HTTP boundary, JSON, credentials, OAuth token cache, confirm-before-send, readability). No live call has run — gaps #5, #6 | 9, 10, 11 |
| 6 | Hardening pass | Done | Specificity-aware NLU routing fix, JSON depth cap, Gmail header-injection fix, first fuzz harness | — |
| 7 | Continuous integration | Done | `.github/workflows/ci.yml` — the gates became automatic rather than remembered | — |
| 8 | Code quality gates | Done | clang-tidy (`bugprone-*`/`cert-*`/`clang-analyzer-*`, hard errors), cppcheck, libFuzzer under ASan/UBSan, gcov/gcovr. Coverage deliberately **not** threshold-gated | — |
| 9 | The documentation pass | Done | `STATE.md`, `RUNBOOK.md`, `DECISIONS.md`, `ARCHITECTURE.md`. Flagged the **coverage inversion**: newest code best-tested, core safety loop least-tested | — |
| 10 | Core-loop tests + the first privacy proof | Done | 5 fixture-driven suites; `companion-sync` 0 % → 96 %; "no API accepts a `SensorFrame`" became a **compile-time** assertion. ≈40 % → ≈44 % lines | — |
| 11 | Real engines in CI | Done | Real whisper `tiny.en`, real OpenCV YuNet+SFace, real Piper against real weights in the `real-engines` job | — |
| 12 | Real LLM in CI | Done | Real llama.cpp + Qwen2.5-0.5B-Instruct Q5_K_M in the `real-llm` job — **and the finding that Phase 21 exists to fix** | — |
| 13 | Account-free wake word | Done | openWakeWord's 3-stage ONNX pipeline on ONNX Runtime, default backend; Porcupine kept as a production option. Gap #8 opened | — |
| 14 / 14b | Real bring-up on Windows/MinGW | Done (agent half) | Full real-engine build configures, compiles, links, and runs a scripted turn. Three latent bugs fixed (ORT `_stdcall`, Piper quoting, ggml collision). Cognitive stage overran 120 ms by **~10×** | — |
| 15 | The memory & recall engine | Done | People / reminders / event log in vendored SQLite 3.46.1; face recall wired pre-gate into the loop; privacy proof extended to embeddings, person records, and events | 12, 13 |
| 16 | Memory hardening | Done | AES-256-CTR at rest (FIPS-197 / SP 800-38A KATs), per-device key file, in-place migration from Phase-15 plaintext, retention by count+age on the existing tick. Closed gap #10 | 14, 15 |
| 17 | Fault tolerance & graceful degradation | Done | `EngineGuard` (throw → `HardwareError`, hang → `Timeout` on a worker), a defined degraded mode per engine, watchdog on the existing tick with reboot escalation. **11 suites** | 16 |
| 18 | Power & thermal | Done (policy) | Battery vision duty-cycling (40 % / 15 %), thermal throttle wired *into* the Phase-17 hang budget (×1.5 / ×2.0), critical-battery reminder pass. **12 suites**. Gap #12 opened | 17 |
| 19 | Audio robustness under noise | Done (measurement) | 3 checksummed noise beds × 4 SNRs against real whisper + openWakeWord; single-mic pre-processor; SNR folded into the **existing** 0.72 gate. Gap #13 opened | — |
| 20 | Longevity & resource-leak soak | Done | 10,080 ticks = **7 simulated days** on a virtual clock, real runtime + real memory engine. Found and fixed two genuine longevity bugs. Gap #14 opened | — |
| **21** | **Answer-side safe mode** | **Shipped** | Gate the LLM's *answer*, not just the perception input; self-referential questions answered from the store or abstained (`ResponseKind::Unverified`); `[route:memory]` no longer falls back to a guess. Verified by mutation. Does **not** catch a confidently wrong answer in general | **18** |
| **22** | **The caregiver boundary** | **Shipped** | A consented, minimized digest + a real paired transport (loopback/fake, real BLE a documented stub) + a hardened inbound path (encrypt-then-MAC, hardened JSON decoder, replay + rate limiting), with the privacy proof intact | **19** |
| **23** | **Wandering & distress detection** | **Shipped (policy)** | `AlertKind::Wandering`/`::Distress` get their first-ever production producers, mirroring Phase 18's source-boundary/pure-policy/edge-triggered-alert template exactly. Zone/arousal state + dwell → risk bands → caregiver alert; digest gets `wandering_flags`/`distress_flags`. No real geofence/GPS or biometric sensor exists — documented stubs, gap #16 opened | — |
| **24** | **Security hardening, round 2** | **Shipped (crypto)** | At-rest store moves from unauthenticated AES-256-CTR to encrypt-then-MAC (reusing Phase 22's already-proven AES-256-CMAC), versioned container with migrate-forward on open, a fixed silent-data-loss bug on unrecognized files, closed the dangling `tests/crypto_test.cpp` KAT claim, and opt-in (off by default) per-install key binding | **20** |

CTest suites accumulate: 9 after Phase 14 → 10 (`echo-memory`, 15) → 11
(`echo-fault-injection`, 17) → 12 (`echo-power-mgmt`, 18). `echo-soak` (20) runs in
its own job. Phase 23 adds `echo-safety-mgmt`; Phase 24 adds `echo-crypto`.
**Not yet built or run on this machine** — see the Phase 23/24 sections' verification
notes for the exact commands still owed once a CMake/MinGW toolchain is available.

---

## 3. Phase detail

### Phase 1 — Scaffold
**Why.** Establish contracts before implementations, so every later phase could be
additive rather than a rewrite.
**Shipped.** `common`, `sensor-pipeline`, `perception`, `cognitive-core`, `voice-ui`,
`companion-sync`, `power-mgmt`, `boot` as static libs; the lock-free SPSC
`ring_buffer.hpp`; `latency.hpp` encoding the 120 ms budget as an asserted contract;
the confidence-gated safe-mode path in `cognitive_core.cpp`; a build that compiles
dependency-free on any host with deterministic stubs.
**ADRs.** 1 (embedded Linux base, not a from-scratch kernel), 2 (static library per
module, wired by `boot/`), 4 (fail safe, not smart), 5 (privacy by default, enforced
by interface shape), 8 (everything real is opt-in).
**Left dormant, until Phase 23.** `AlertKind::Wandering`, `::Distress`, and
`::LowConfidenceTrend` were defined here and went years without a producer. Phase 21
gives `LowConfidenceTrend` one; Phase 23 gives the other two theirs — see that
section for what's policy-tested versus still-simulated sensing.
**Record quality.** Thin — reconstructed from ADR dates (2026-07-28) and STATE.md's
"what's built" table. No dedicated brief exists.

### Phase 2 — Apps layer + isolation
**Why.** Keep app-level failure away from the safety-critical core loop.
**Shipped.** 8 apps as real supervised OS processes with IPC round-trip and crash
containment (`echo-apps-smoke`); the router + NLU; the HUD's deliberately minimal
three overlay primitives; the build-graph rule that `echo::app-sdk` links no core
module, enforced in CMake. The structural privacy guarantee — the companion
transport has no path that accepts a `SensorFrame` — dates from here, though it
only became a compile-time assertion in Phase 10.
**ADRs.** 3 (voice-first, the HUD is the only screen), 6 (the phone is a Bluetooth
bridge, not hardware in the glasses), 7 (hard process isolation).

### Phase 3 — Real local AI, opt-in
**Why.** Make the intelligence real on a laptop without disturbing the scaffold's
contracts.
**Shipped.** Adapters behind existing interfaces for whisper.cpp (ASR), llama.cpp
(reasoning), Piper (TTS), Porcupine (wake word), OpenCV YuNet+SFace (face detect +
recognize), and an SDL laptop HUD; real webcam/mic capture; the integrated
`echo-demo`; end-to-end latency logging. **All default OFF** so the reference binary
and CI stay deterministic (ADR-8).
**ADRs.** 11 (Porcupine for wake word; loopback OAuth for the cloud services).

### Phase 4 — Real-world bring-up & validation
**Why.** Everything above is unproven until a real person uses it on real hardware.
**Status: open, and not closeable by code.** Gaps #1 (real engines end-to-end on
real I/O), #2 (**measured** per-stage latency over ≥20 real turns — the README table
is still blank placeholders, deliberately not filled with fake data), #3 (gap
analysis vs. the 120 ms target, expected to *exceed* it on a laptop CPU), #4 (a
non-founder tester with no instructions). Phase 14 closed the agent-doable half; the
human half is untouched.

### Phase 5 — Real third-party integrations
**Why.** The apps layer had only mock backends.
**Shipped.** Real Spotify / Gmail / Search / YouTube backends behind the existing app
interfaces, plus `appkit`: the HTTP boundary (`IHttpClient`), a JSON parser,
credential loading, an OAuth token cache, the confirm-before-action gate, and
readability extraction. Gated by `-DECHO_WITH_NETWORK=ON`; the libcurl transport is
opt-in and **no live API call has ever run** (gap #5). The one-time OAuth authorize
helper `scripts/authorize.*` remains a documented manual browser-paste step (gap #6
— *the only remaining gap a phase could close with code alone*).
**ADRs.** 9 (mock-by-default backends behind unchanged interfaces — the pattern
Phase 22 should follow for the companion transport), 10 (confirm-before-send), 11.

### Phase 6 — Hardening pass
**Why.** Real inputs surfaced real bugs.
**Shipped.** The **specificity-aware NLU fix**: the browser's generic `read`/`open`
now yields to a domain-specific intent elsewhere in the utterance, so *"read my
unread email"* routes to mail (regression suite `echo-nlu-routing`). A **depth cap**
on the JSON parser. A **Gmail header-injection fix**. The first fuzz harness.
**Why it matters for later phases.** This hardened parser is the one Phase 22 must
reuse for inbound caregiver commands — it is already the component that eats
untrusted bytes, and the header-injection lesson is exactly the class of bug a
remote command path would reintroduce.

### Phase 7 — Continuous integration
**Shipped.** `.github/workflows/ci.yml`. The quality gates stopped depending on
someone remembering to run them. Jobs have since grown to: `stub-build`,
`network-build`, `secret-scan`, `clang-tidy`, `cppcheck`, `fuzz-json`, `coverage`,
`real-engines`, `real-llm`, `soak`.
**Record quality.** Thin — recorded only as a heading reference in `README.md`.

### Phase 8 — Code quality gates
**Shipped.** clang-tidy (`bugprone-*` / `cert-*` / `clang-analyzer-*` over every
first-party TU, findings are hard errors), cppcheck as a second independent analyzer
(`warning`/`performance`/`portability` fail the job), bounded libFuzzer under
ASan/UBSan on the JSON parser, gcov + gcovr with the percentage printed to the run
summary and XML to Codecov.
**A deliberate non-decision worth preserving.** Coverage is **not** gated on a
threshold: an arbitrary floor on a codebase still intentionally stubbed at its core
would reward testing stub code. The number is made visible and trending first; a
floor comes once the real engines land and the denominator settles.

### Phase 9 — The documentation pass
**Why.** Nothing described the project honestly in one place.
**Shipped.** `STATE.md`, `RUNBOOK.md`, `DECISIONS.md`, `ARCHITECTURE.md`.
**Its most consequential output was a finding, not a document:** the **coverage
inversion** — the newest, least safety-critical code was the best-tested, and the
core perception→cognitive→voice loop was the least. Phase 10 exists because of this.
**Constraint worth reusing.** Phase 9 was documentation-only, no source changes;
anything surfaced was *recorded* rather than fixed. That separation is why its
findings were trustworthy.

### Phase 10 — Core-loop tests + the first compile-time privacy proof
**Shipped.** Five fixture-driven suites (`echo-sensor`, `echo-perception`,
`echo-voice`, `echo-companion`, `echo-e2e-fixture`) driving fixture audio through
the real pipeline to a spoken safe-mode fallback. Coverage moved ≈40 % → ≈44 % lines
with the gain exactly where Phase 9 flagged it: `companion-sync` 0 % → 96 %,
`voice-ui` 0 % → 91 %, `perception` 7 % → 69 %, `sensor-pipeline` 0 % → 42 %.
**The durable artifact:** the `static_assert` block in
`tests/companion_sync_test.cpp` — `send_alert`/`send_status` accept their intended
payloads and provably **cannot** accept a `SensorFrame`, and neither payload is
constructible from one. Phase 15 extended it to memory types; Phase 22 must extend
it again without weakening it.

### Phase 11 — Real engines in CI
**Why.** Part of "the engines are unproven" never actually needed hardware.
**Shipped.** The `real-engines` job runs real whisper.cpp (`tiny.en`), real OpenCV
YuNet + SFace, and real Piper against real weights: silence → near-empty transcript,
garble → no crash, a clear Piper-synthesized phrase → the expected words; an enrolled
face → detected **and** matched, a different person → detected but **not** matched, a
non-face frame → no detection; both voice-ui tones synthesize to valid non-silent
audio with the reassuring tone measurably slower.
**Explicitly not claimed.** Field robustness. Clean, synthetic fixtures prove the
engines work *at all* on good input.

### Phase 12 — Real LLM in CI
**Why.** Phase 11 judged llama.cpp impractical for CI — a judgement written for a
full-size 3–4B model. Phase 12 revisited it with a genuinely tiny one.
**Shipped.** The `real-llm` job: real llama.cpp + Qwen2.5-0.5B-Instruct Q5_K_M
(~498 MiB, pinned + SHA-256-verified in `MANIFEST.md`), one short greedy CPU
inference. Route-tag parsing verified against the model's **actual** output format;
sanity generation verified.
**The finding that Phase 21 exists to fix.** A low-confidence observation carrying a
clear command still lands in safe mode *because the gate short-circuits before the
model* — so, unlike the stub (which returns empty text and trips the LLM-failure
fallback), **a real model produces confident text for every prompt**, and the
safe-mode guarantee rests entirely on perception confidence. Recorded honestly at
the time as a real finding rather than routed around; left open until Phase 21.
**Explicitly not claimed.** Production reasoning quality — a 0.5B model exists here
only to exercise the real integration, the parser, and the gate.

### Phase 13 — Account-free wake word
**Shipped.** openWakeWord alongside Porcupine behind the same `wake_word.hpp`
interface and made the default; its pretrained three-stage ONNX pipeline runs on ONNX
Runtime (Apache-2.0 / MIT, anonymously fetchable) in the `real-engines` job. Fires on
a Piper-synthesized "hey jarvis"; does not false-accept ordinary speech, silence, or
loud garble — asserting the model's *actual* probabilistic behaviour, printing every
observed score, not a manufactured perfect separation.
**Gap #8 opened.** It ships the pretrained "hey jarvis"; a bespoke "Hey ECHO" is a
*training* undertaking (synthesize + train), documented as a follow-up.
**With Phase 13, every real engine — wake-word, ASR, vision, TTS, LLM — runs against
real models in CI.** What remains is inherently un-simulatable.

### Phase 14 / 14b — Real bring-up on the Windows/MinGW dev laptop
**Why.** Everything above was proven on Ubuntu CI runners.
**Shipped (14).** Stub build + full CTest green on MinGW-w64 ucrt g++ 15.1 / CMake
4.0 / Windows 11 (9/9 suites); all pinned models fetched and checksum-verified; the
real LLM engine built and run headlessly, returning coherent reasoning.
**Shipped (14b).** The three native deps that blocked `-DECHO_REAL_AI=ON` — ONNX
Runtime 1.17.3, OpenCV 4.10.0 (built from source: C++-ABI, so an MSVC build would not
link), SDL2 2.30.9 — installed with this exact toolchain, and the full real-engine
build configures, compiles, links, and runs a scripted turn.
**Three genuine bugs found and fixed, not routed around.**
1. **ORT header won't parse under MinGW** — root cause was ORT spelling its calling
   convention `_stdcall` (MSVC-only) on the `_WIN32` branch. On x64 that convention
   is a no-op, so `cmake/echo_ai.cmake` maps `_stdcall`→`__stdcall`. This *corrected*
   the earlier Phase-13 note that ORT "does not compile on MinGW" — it does, with a
   one-token define.
2. **Piper TTS silently failed on Windows** — `voice_ui.cpp` runs a quoted command
   through `std::system()` i.e. cmd.exe, which strips the outer quote pair and
   corrupts paths containing spaces. Linux CI paths had none, so it never surfaced.
3. **ggml symbol/DLL collision** — whisper v1.7.4 and the llama build ship different
   ggml revisions under identical DLL/symbol names. Resolved by building whisper
   **v1.9.1** with `WHISPER_USE_SYSTEM_GGML=ON` so both engines share one ggml.
**The loose thread nobody has pulled.** The real cognitive inference **overran the
120 ms budget by ~10×**, as the RUNBOOK predicts for a laptop CPU. Kept out of the
README/STATE latency tables because it is a typed-text, cognitive-stage-only
observation, not a spoken turn. See §5.

### Phase 15 — The memory & recall engine
**Why.** The first phase since the scaffold to add a genuinely *new capability*
rather than make an existing one real. Nothing on-device had ever persisted who a
face belonged to or what the wearer needed reminding of.
**Shipped.** `memory/` over a vendored SQLite 3.46.1 amalgamation compiled in-tree:
**people** (created *only* when the wearer names them — an unknown face with no
naming utterance creates **zero** records), **reminders** (recurrence advancing on
acknowledge, delivered on the **existing** core tick, no second timer), and a narrow
**event log**. Face recall runs **before** the confidence gate because retrieval is
not generation (ADR-12). `[route:memory]` questions are answered from the log or
return `""` so the caller falls back rather than fabricate. Persistence across
restart proven with a fresh engine on the same file.
**The privacy proof extended.** `static_assert`s in `echo-memory` extend the Phase-10
`SensorFrame` proof to embeddings, person records, and events — and were confirmed to
*bite* (a deliberately-wrong "leak exists" assertion fails to compile).
**ADRs.** 12, 13.
**Documented lesson later phases lean on.** Tiny-model prompt-following is fragile —
which is why Phase 16 deferred LLM summarization and Phase 21 routes
self-referential questions directly rather than trusting `[route:memory]` to be
emitted.
**Toolchain note.** SQLite compiled cleanly on MinGW in ~10 s with zero warnings —
the opposite of Phase 14b's friction. There was no build gap to work around, so none
was invented.

### Phase 16 — Memory hardening (encryption at rest + retention)
**Why.** Phase 15 named two gaps on purpose: no encryption, no retention.
**SQLCipher was tested for real first, and correctly abandoned.** It is not a
self-contained amalgamation and must link a crypto backend (OpenSSL `libcrypto`),
which is absent from this MinGW toolchain (verified: no headers, bare `-lcrypto`
fails). Pulling in OpenSSL would break the dependency-free stub build.
**Shipped instead.** The working DB lives in an **in-memory** SQLite connection
(`sqlite3_deserialize`); its serialized image is written to disk as **AES-256-CTR**
ciphertext, from a small in-tree implementation proven against the published
**FIPS-197 and NIST SP 800-38A** known-answer vectors. Plus a random **per-device
key** in an owner-only local key file, in-place **migration** from a Phase-15
plaintext DB (proven against a real fixture DB), and **retention** — the event log
capped by count *and* age on the existing tick, person notes cap-and-evict
oldest-first (notes are the long-term value, so they are not aged out).
**What ADR-14 self-flags, and Phase 22 must respect.** The ciphertext is **not
authenticated** (no MAC; a wrong key is caught only by a post-decrypt SQLite-magic
check, and the engine fails **closed**), and the key is **not** hardware-backed — an
attacker with live read access to both files can decrypt. Binding it to a TPM or a
paired phone is the documented follow-up, and is the natural neighbour of Phase 22's
pairing work.
**ADRs.** 14, 15. **Closed gap #10.**

### Phase 17 — Fault tolerance & graceful degradation
**Why.** Sixteen phases built the pipeline; none asked what happens when a piece
breaks mid-turn — and the wearer is, by design, someone who may not notice or be
able to reset a malfunctioning device on their own face.
**Shipped.** `EngineGuard` at every engine boundary: a thrown exception becomes
`fail(HardwareError)`, and a **hung** call is bounded by running it on a worker and
abandoning it past a per-engine budget → `fail(Timeout)`. A per-tick `try/catch`
backstop. A **defined degraded mode per engine**: vision failure → voice-only, never
a fabricated match; ASR failure → a calm spoken retry, not silence; LLM
throw/timeout → a known-good engine-fault line **kept distinct from the
low-confidence gate**; memory write failure → the reminder is still delivered this
session. A watchdog on the existing tick with in-process re-init and escalation to
`reboot_required()`.
**The precise ledger** (STATE.md keeps it per failure class): throws, non-Ok
statuses, hangs, and hangs-where-re-init-also-hangs are all contained without
crashing; **segfault / memory corruption / UB / `noexcept` violation / OOM are not
containable** by `try/catch` and need a supervising init system or hardware watchdog.
**Honest limitations recorded.** A wedged thread leaks one worker and can stall
shutdown (C++ cannot safely kill a non-returning call — which is *why* the reboot
path exists). The hang bound costs hot-path overhead. An engine that re-initializes
cleanly but keeps failing is served its fallback indefinitely rather than rebooting.
**ADR.** 16. **11 suites.**

### Phase 18 — Power & thermal
**Why.** `power-mgmt` had been a top-level directory since the first scaffold and
across seventeen phases was never given real logic.
**Shipped.** `IPowerSource` + `IThermalSource` as a read-only sensing boundary with
deterministic fakes, mirroring the `IHttpClient` discipline. Battery-driven **vision
duty-cycling** (full-rate > 40 %, 1-in-4 at 15–40 %, vision-off below 15 %) — with a
dropped frame **dropped**, never fabricated to hide the lower rate; the mic is never
duty-cycled. A thermal throttle wired **into** the Phase-17 hang budget (×1.5 / ×2.0)
so a legitimately slower turn is not abandoned as if hung, rather than adding a
parallel timer. Low battery / high thermal ride the **existing**
`AlertKind::EngineDegraded` channel, edge-triggered. A critical-battery reminder pass
— a low battery must not be what silences a medication reminder. A throwing gauge
read neither crashes nor invents a charge level.
**ADR.** 17. **12 suites.** Phase 17's fakes were extracted to a shared
`tests/fake_engines.hpp` and reused rather than duplicated.
**Gap #12 opened — permanent-until-hardware.** No real fuel gauge or skin-adjacent
thermal sensor exists; the backend is a documented stub capturing the *shape* of the
sysfs read with simulated *values*. The critical-reminder pass is doubly speculative:
it fires on a threshold, not a real "about to die" signal.

### Phase 19 — Audio robustness under noise
**Why.** Every ASR/wake-word test since Phase 10 fed the engines **clean** audio —
very likely the biggest reason the pending live human test could disappoint.
**Shipped.** Three synthetic noise beds (`hum`, `transient`, `babble`), generated
integer-only so they are byte-identical everywhere and checksum-verified in
`MANIFEST.md`, mixed into the clean clips at **clean / +10 / 0 / −5 dB**. Real
whisper + real openWakeWord measured at every combination. A single-mic
`AudioPreprocessor` (high-pass + noise gate + AGC) — **single-mic by design**,
verified against the real capture code before designing it, so no beamformer was
built for a mic array that doesn't exist. An SNR estimate folded into the transcript
confidence so noisy audio rides the **unchanged 0.72 gate** and the **existing**
Phase-17 "ask again" fallback.
**The measured result, reported as measured.** Both engines were markedly robust —
`tiny.en` transcribed correctly and openWakeWord fired (~0.99) at *every* level
including −5 dB. Stated with the methodology caveat **up front**: SNR is defined on
whole-clip RMS and the TTS clip carries leading/trailing silence, so the effective
SNR during the spoken words is higher than the label. The pre-processor helps most on
`hum` (−0.3 → +9.3 dB) and least on `babble` — the case a single mic fundamentally
cannot separate — and did **not** rescue a failing case. No threshold was tuned to
manufacture a scary-then-rescued arc.
**Gap #13 opened — permanent-until-hardware.** Synthetic noise on clean fixtures is
not a real mic's frequency response, echo off real walls, or a human speaking while
moving.

### Phase 20 — Longevity & resource-leak soak
**Why.** Every prior test exercised a single turn, fault, or bounded scenario. None
proved the thing an all-day device needs most: running continuously without
degrading.
**How, honestly.** `tests/soak_test.cpp` drives the **real** runtime and the **real**
memory engine (SQLite + encryption) with the shared Phase-17/18 fakes for the other
engines, through **10,080 ticks = 7 simulated days** in **simulated time** — a
one-line `Runtime::set_clock` seam feeds the scheduler an injected virtual clock
advanced 60 s per tick. ~80 s on the dev box; the only honest way to reach a
multi-day duration inside a CI budget.
**Measured.** 600/600 one-shot reminders delivered, 0 early, worst drift 59 s (under
the 60 s tick); 4/4 recurring occurrences; event log pinned at its 200-row cap after
600 firings; RSS +~0.1 MiB over the run; fd span ≤ 5; 300 close/open cycles with fd
unchanged; 11 hang episodes, 0 reboots, peak 1 abandoned worker draining to 0. The
scheduler/retention/watchdog rows are deterministic; the RSS/fd rows are the dev-box
sample, with the Linux `soak` job's `/proc` pass as the portable proof.
**Two genuine bugs found and root-cause fixed.**
1. **Recurring reminders went silent after day one.** In-session delivery was
   de-duplicated by reminder *id*, permanently — so a re-armed recurring reminder was
   suppressed forever: *a daily medication reminder would fire once and never again
   until reboot*. Fixed by keying the record by *occurrence* (id → delivered `due`),
   preserving the original backstop for a failed `mark_fired` write.
2. **The store rewrote its entire encrypted image on every tick.** `prune_events`
   marked the store dirty whenever the DELETE *executed*, even when it evicted
   nothing — the common case at cap — triggering a full serialize + AES + file
   rewrite every tick, forever. Constant flash wear for no state change (and ~170
   ms/tick, which is what made the soak infeasible before the fix). Fixed by gating
   the dirty flag on `sqlite3_changes(db_) > 0`.
**Gap #14 opened — permanent-until-hardware.** The soak drives *fakes* in place of
whisper/llama/OpenCV/ONNX and does not instrument them, so a slow leak *inside* one
of those over real wall-clock days would not be caught.

### Phase 21 — Answer-side safe mode *(shipped)*
**Why it existed.** The 0.72 gate scored perception *input* only, so a confident-but-wrong
LLM answer reached the wearer as `ResponseKind::Normal` with `flag_caregiver = false`,
and the fallback when `answer_query()` returned `""` was the model's guess — backwards
in exactly the case where a guess is least defensible.

**What shipped.** A private DI seam (`cognitive-core/src/core_factory.hpp` +
`tests/fake_llm.hpp`) — without it the gate could not be tested at all, which is *why*
this survived twenty phases, not merely how. `LlmReply::confidence` from real
`llama_get_logits_ith` logits + softmax. `is_self_referential_query()` in
`memory/utterance.hpp`, routing questions about the wearer's own life to the store and
**abstaining** when there is no record — the LLM is never consulted for those. The
`[route:memory]` empty-store fallback now abstains too. A third decline path,
`ResponseKind::Unverified`, with its own line, `Tone::Reassuring` in voice-ui, a
non-`Success` HUD state in the demo, and a repeated-abstention trend on
`AlertKind::LowConfidenceTrend` (dormant since Phase 1). 15 suites in
`tests/answer_gate_test.cpp` + a runtime-level suite in `fault_injection_test.cpp`.

**Verified by mutation.** Removing the grounding branch fails 9 checks across 4 suites
(including `!llm->consulted()`); reverting the `[route:memory]` fallback fails 2 more.
The suite detects the pre-Phase-21 behaviour rather than merely passing alongside it.

**Honest caveat.** It does **not** detect a confidently wrong answer in general — mean
token probability is a fluency signal, not a truth signal, so the floor defaults to a
low 0.35 and is scoped as a degeneracy catch. The grounding rule, which uses no
probability at all, carries the guarantee. The classifier is a conservative curated
phrase list. The measured confidence distribution from the `real-llm` job has not been
recorded yet. **ADR-18.**

**Incidental fix.** `tests/resource_probe.hpp` now defines `NOMINMAX` before
`<windows.h>`; without it every `std::min`/`std::max` in a TU including that header
fails on MSVC, which broke `soak_test.cpp` on a native Windows build. Pre-existing and
unrelated to Phase 21 — CI is Linux, so it had never been hit.

### Phase 22 — The caregiver boundary *(planned)*
Full brief in [phases.md](phases.md). `companion-sync` is still a logging stub with
no pairing, no auth, and an unsigned firmware path — while a caregiver, half the
product's users, cannot set a reminder or see whether medication was acknowledged.
Adds a minimized, consent-gated `CaregiverDigest` (counts and states, no names or
strings) that leaves every existing `static_assert` biting, an `ICompanionTransport`
with a deterministic fake and a loopback stand-in, and an inbound path treated as
hostile — validated on Phase 6's hardened parser, rate-limited, announced to the
wearer, with firmware failing closed. **ADR-19.**

---

## 4. The gap register

Carried from [STATE.md](STATE.md) with an **owner** column, so a planner can see at a
glance what is even plannable. Do not claim any of these as done until the evidence
exists.

| # | Gap | Phase | Owner |
|---|-----|-------|-------|
| 1 | Real quantized LLM/ASR/TTS/vision run end-to-end on real I/O | 4 | hardware + human |
| 2 | **Measured** per-stage latency, ≥20 real spoken turns | 4 | human |
| 3 | Gap analysis: real end-to-end ms vs. the 120 ms target | 4 | human (but see §5) |
| 4 | A non-founder tester uses it with no instructions | 4 | human |
| 5 | Live credentialed Spotify/Gmail/Search/YouTube calls | 5 | accounts |
| 6 | One-time OAuth authorize helper (`scripts/authorize.*`) | 5 | **code** |
| 7 | On-glasses sensor DMA frontends + BLE/WiFi transport | hardware | hardware |
| 8 | Custom-trained "Hey ECHO" wake word | 13 | **code** (heavy: synthesize + train) |
| 9 | Multi-day real-world memory accuracy (SFace across days/lighting) | 15 | hardware + human |
| ~~10~~ | ~~Note summarization / pruning (bounded growth)~~ | ~~15~~ | **Closed in Phase 16** |
| 11 | A real wearer uses recall in daily life | 15 | human |
| 12 | Real battery/thermal sensor + real device thermal behaviour | 18 | hardware *(permanent)* |
| 13 | A real mic in a real room | 19 | hardware *(permanent)* |
| 14 | Real third-party engine-library uptime over real days | 20 | hardware *(permanent)* |

Phase 22 will open a **#15**-shaped gap of the same family: real BLE, a real paired
phone, and a real caregiver app.

**Only two gaps are code-shaped: #6 and #8.** Everything else needs hardware,
accounts, or people — which is why phases 15–22 add capability and close
*self-inflicted* gaps rather than chipping at this table.

---

## 5. Candidate future phases

Researched during Phase 21/22 planning and not chosen, recorded so the next planning
session starts warm.

**A. The latency reckoning.** Principle #2 (sub-120 ms) is the only one of the five
design principles **never validated in any form**. The README latency table is still
blank, and Phase 14 observed the cognitive stage overrunning by ~10× without anyone
following up. Crucially, a large part of this is *not* actually human work: per-stage
timing with **real** engines on **fixtures** is headless and CI-doable, reusing the
existing `StageTimer` and the `real-engines` / `real-llm` jobs. That would close the
*engineering* half of gaps #2/#3 the same way Phases 11–13 closed the engineering
half of gap #1 — leaving only the genuinely-human spoken-turn measurement open. The
honest outcome may well be a **revised or tiered budget** rather than an
optimization, which is a legitimate result and overdue either way.

**B. Wandering & distress detection — DONE, Phase 23.** Built exactly as scoped here:
a read-only source boundary (`ILocationSource`/`IArousalSource`, coarse zone/arousal
state + dwell, not raw IMU/GPS/prosody) with deterministic fakes plus a pure policy,
mirroring `power-mgmt` file-for-file. It did open the anticipated new
permanent-until-hardware gap (#16) — the clear-eyed call made was to ship the policy,
tested against simulated readings, and be explicit that the sensing behind it doesn't
exist yet, the same trade Phase 18 made for battery/thermal.

**C. Security hardening, round 2 — DONE (crypto half), Phase 24.** Closed the
AES-256-CTR/no-MAC gap by reusing Phase 22's already-proven encrypt-then-MAC
composition (AES-256-CMAC) rather than adding a new crypto dependency, plus opt-in
(off-by-default) per-install key binding — hardware-backed binding remains a
permanent-until-hardware gap, not attempted. Gap #6's `scripts/authorize.*` (the
OAuth helper script) is unrelated to the crypto work and was deliberately left open;
it needs its own pass.

---

## 6. Conventions for writing the next phase

**Brief format** (see any brief in [phases.md](phases.md) from Phase 13 on):

````markdown
# ECHO OS — Phase NN: Title

## Why this phase
<prose: what is actually wrong or missing, grounded in named files and
 prior findings — not a feature wish>

Branch `feature/<slug>` from `master` (confirm PR #NN is merged first).

## What to build
1. **Bold lead** — prose.

## Constraints
1. …

## Immediate deliverable for this session
1. …

## Repo step (run as the final step)
```bash
git add -A
git commit -m "Phase NN: …"
git push -u origin feature/<slug>
gh pr create --base master --head feature/<slug> \
  --title "Phase NN: …" \
  --body-file pr_body.md
```
````

**Numbering.** PR numbers trail phase numbers by 2 (Phase 20 → PR #18, Phase 21 →
#19, Phase 22 → #20). ADR numbers trail by 3 from Phase 15 on (Phase 20 added none;
Phase 21 → ADR-18, Phase 22 → ADR-19).

**A phase is finished when** the new tests are green in the dependency-free stub
build, the relevant CI job passes, `STATE.md` records what was proven *and what was
not*, an ADR captures any decision a future engineer would otherwise re-litigate, and
any newly-discovered permanent gap is in the table above with an owner.
