# ECHO OS — Architectural Decision Log

The real architectural choices behind this codebase and *why* — for onboarding a
new engineer, and for answering "why did you build it this way?" in an investor
conversation. Newest-relevant first; dates are at phase granularity (the project
was built in nine phases over late July 2026).

Each entry: the decision, the reasoning, and — where it applies — the honest
trade-off it costs.

---

## ADR-1 — Embedded Linux base, not a from-scratch kernel
*Decided: Phase 1 · 2026-07-28*

**Decision.** ECHO OS runs on a stripped-down embedded Linux base
(Yocto/Buildroot-style minimal image) and layers a custom runtime, voice-first UI
shell, and local AI pipeline on top. It is explicitly **not** a bespoke kernel.

**Why.** The same relationship Android has to its kernel, or watchOS to Darwin.
Writing a kernel would spend the entire engineering budget on solved problems
(schedulers, drivers, memory management) and buy nothing a caregiver or wearer can
feel. The differentiators — sub-120 ms perception, the safe-mode gate, privacy by
default — all live *above* the kernel. A minimal Linux base gets mature drivers,
a real scheduler, and a cross-compile story for free.

**Trade-off.** A general-purpose kernel is heavier than a hand-tuned RTOS and
gives less deterministic worst-case latency. Mitigated by the latency budget being
a build-enforced contract and `power-mgmt` owning duty-cycle/thermal scheduling —
if Linux scheduling ever becomes the bottleneck, the module seams allow swapping
the hot path without touching the rest.

## ADR-2 — Static library per module, wired by `boot/`
*Decided: Phase 1 · 2026-07-28*

**Decision.** Every module (`common`, `sensor-pipeline`, `perception`,
`cognitive-core`, `voice-ui`, `companion-sync`, `power-mgmt`) is its own static
library with a narrow public interface; `boot/` owns the `Runtime` that wires them
into the core loop and produces the `echo-os` binary.

**Why.** Sharp module boundaries let each engine be swapped behind an unchanged
interface — the whole Phase 3 "real AI adapters" and Phase 5 "real API backends"
strategy depends on this. It also keeps the dependency graph a straight line
(build order follows it), which makes the latency budget attributable per stage.

**Trade-off.** More interface boilerplate than a monolith. Paid back the moment
the first real engine slotted in with zero downstream changes.

## ADR-3 — Voice-first, no touchscreen; the HUD is the only screen
*Decided: Phase 2 (apps layer) · 2026-07-28*

**Decision.** No touchscreen, no window manager, no framebuffer. Every
interaction is a *voice command → spoken / HUD response* loop. The sole visual
surface, the HUD compositor, exposes exactly **three** primitives: subtitle text,
one icon, a status glyph — and nothing else.

**Why.** Design principle #1 (premium and calm) for a wearer with memory loss:
minimal visual noise, large clear audio cues, nothing to navigate or get lost in.
Making it *structural* — a scrollable list or a tappable button is literally not
expressible in the API — means no future app can quietly reintroduce clutter.

**Trade-off.** Some interactions are genuinely awkward without a screen (picking
one of ten results). Accepted deliberately: the answer is better summarization and
disambiguation by voice, not a screen.

## ADR-4 — Fail safe, not smart: a confidence-gated safe-mode
*Decided: Phase 1, made real Phase 3 · 2026-07-28*

**Decision.** `cognitive-core` scores its confidence and, below a conservative
threshold (default **0.72**) or on any model failure, returns a short, warm,
known-good safe-mode line and flags the caregiver — it never lets the LLM guess.
`respond()` never throws; failure degrades to safe mode.

**Why.** The device is worn by people living with memory loss. A confident *wrong*
answer is far more harmful than an honest "I'm not sure right now." This is
principle #5 and it is the ethical core of the product, not a feature flag.

**Trade-off.** The system will sometimes decline to answer things it *could* have
gotten right. That asymmetry is intended.

## ADR-5 — Privacy by default, enforced by interface shape
*Decided: Phase 1 · 2026-07-28*

**Decision.** No raw camera, microphone, or EEG data leaves the device, ever. All
inference is local. The one off-device path (`companion-sync`) carries **only**
alerts, status, and firmware — its transport has **no API that accepts a
`SensorFrame`**. Privacy is enforced by the *shape* of the interface, not a
runtime policy toggle.

**Why.** A caregiver-worn device that streams a wearer's camera/mic/EEG would be
unconscionable and unsellable. Making the leak *inexpressible in the type system*
is stronger than any config flag someone could flip. Local inference also removes
network latency from the 120 ms budget.

**Trade-off.** Local-only inference caps model size to what the SoC can run and
rules out cloud "smarts." Consistent with ADR-4 (calm and correct beats clever)
and ADR-1 (differentiation lives on-device).

## ADR-6 — The phone is a Bluetooth bridge, not hardware in the glasses
*Decided: Phase 2 (apps layer) · 2026-07-28*

**Decision.** Telephony models "phone" as a Bluetooth bridge to the wearer's
paired smartphone (like AirPods or a smartwatch), not a SIM/radio in the glasses.

**Why.** Cellular hardware costs power, size, weight, thermal budget, and
regulatory certification — all scarce on a head-worn device — to duplicate a phone
the wearer already carries. Bridging is lighter, cheaper, and matches the mental
model of every successful wearable.

**Trade-off.** The glasses aren't standalone-connected without a nearby phone.
Acceptable for the target user, who is not off-grid.

## ADR-7 — Hard process isolation between apps and the core loop
*Decided: Phase 2 (apps layer) · 2026-07-28*

**Decision.** The apps layer runs as **separate OS processes** under a supervisor;
`echo::app-sdk` (what an app links) depends on **no core module**, enforced by
CMake. The only seam to the core is `IVoiceBridge` calling `voice-ui`'s public
interface. A crashed or hung app is restarted with bounded backoff and never
reaches the loop.

**Why.** The sub-120 ms safety loop must be un-crashable by application code. An
in-process plugin model would let any app's bug, leak, or infinite loop take down
perception and the safe-mode gate. Isolation in the build graph makes the boundary
un-bypassable rather than merely a guideline.

**Trade-off.** IPC costs more than a function call. Measured against the core
loop's budget, the apps path is deliberately off that budget entirely — the cost
lands only on non-safety-critical app latency.

## ADR-8 — Everything real is opt-in; the default build is dependency-free
*Decided: Phase 3 · 2026-07-28*

**Decision.** Every real engine and transport sits behind a CMake option that
defaults **OFF** (`ECHO_WITH_WHISPER/LLAMA/PIPER/PORCUPINE/OPENCV/SDL`,
`ECHO_WITH_NETWORK`, `ECHO_REAL_AI`, and the CI-only `ECHO_BUILD_FUZZERS`/
`ECHO_COVERAGE`). The default build compiles end-to-end with deterministic stubs
and **zero external dependencies**.

**Why.** An always-green, dependency-free stub build is the safety net every later
phase depends on — CI stays fast and deterministic, a new contributor builds in
minutes, and the real-engine seams are exercised by stub adapters on the same code
paths. It also cross-compiles to the target unchanged.

**Trade-off.** The most product-critical real paths (models, live APIs) are then
the *least* exercised by default — visible directly in the coverage numbers (see
[STATE.md](STATE.md)). Accepted knowingly; closing it is the next testing
investment, gated on the real-hardware bring-up.

## ADR-9 — Mock-by-default third-party backends behind unchanged interfaces
*Decided: Phase 2, extended Phase 5 · 2026-07-28*

**Decision.** Spotify, Gmail, Google Search, and YouTube each ship a `--mock`
backend returning realistic fake data behind the same `IApp` interface the real
backend implements. Wiring real credentials later is **zero interface change**.

**Why.** Developer credentials for those services didn't exist when the apps were
built. Mocking behind the real interface made the entire voice flow testable and
CI-deterministic *today*, and de-risked the real integration to "fill in a
backend" rather than "redesign the app." Phase 5 proved it: the real backends
slotted in without changing a single caller.

**Trade-off.** Mock data can mask real-API failure modes (rate limits, token
expiry). Addressed by an explicit anticipated-failure table to be validated on the
live run (STATE.md gap #5).

## ADR-10 — Confirm-before-send for state-changing actions
*Decided: Phase 5 · 2026-07-28*

**Decision.** No email is ever sent on a single utterance. *"reply saying …"*
**stages** the message and speaks it back; only an explicit **"send"/"confirm"**
actually sends. "Cancel", an unrelated command, or an unrecognized word all **fail
safe** — nothing sends, and the pending action is one-shot so a later stray "yes"
can't resurrect it.

**Why.** Voice + ASR is lossy, and the wearer has memory loss — an irreversible
side effect (sending mail) must never fire on a mishearing. This is ADR-4's "fail
safe" discipline applied to outbound actions. It is the one Phase 5 piece fully
covered in CI (the gate in isolation *and* the whole reply→confirm flow).

**Trade-off.** One extra turn per send. Correct and non-negotiable for
irreversible actions.

## ADR-11 — Porcupine for wake word; loopback OAuth for the cloud services
*Decided: Phase 3 (wake word) / Phase 5 (OAuth) · 2026-07-28*

**Decision.** Wake word uses **Picovoice Porcupine** (over openWakeWord); Spotify
and Gmail use the **Authorization Code flow with a loopback redirect**, not
device-code.

**Why.** Porcupine has the simpler local C API and validates its AccessKey
**offline** — no audio or request leaves the device at runtime (consistent with
ADR-5). For OAuth: Spotify has no device-code grant, and loopback is the flow both
providers recommend for a native app that can pop a browser **once**. On the
glasses that one-time consent happens during pairing on the companion phone/laptop,
after which a cached refresh token drives silent renewals and the glasses never
show a browser again.

**Trade-off.** Porcupine's AccessKey and a custom "Hey ECHO" `.ppn` are
account-gated (a one-time setup step, never committed). The loopback consent is
still a manual browser paste until `scripts/authorize.*` is written (STATE.md
gap #6).

---

*To add an entry: append with the next ADR number, a date, the decision, the why,
and the honest trade-off. Keep it short — this is a log of real choices, not a
design spec.*
