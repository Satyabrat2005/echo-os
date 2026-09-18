# ECHO OS — Architecture

A 10-second mental model of the system, then a short walkthrough of the two
things that matter most: the **safety-critical core loop** and the **hard
isolation boundary** between that loop and the apps layer.

This document describes what is actually in the repo today. Where a path is a
laptop stand-in or still stubbed, it says so — see [STATE.md](STATE.md) for the
full built-vs-pending breakdown.

## System diagram

```mermaid
flowchart TB
    classDef mock fill:#eeeeee,stroke:#999999,stroke-dasharray:3 3,color:#333333;
    subgraph CORE["🔒 Safety-critical core — sub-120 ms loop, owned & wired by boot/"]
        direction LR
        SENSOR["<b>sensor-pipeline</b><br/>cam · mic · EEG<br/>lock-free ring buffer<br/><i>5 ms</i>"]
        PERC["<b>perception</b><br/>wake-word · ASR<br/>face / object CNN<br/><i>45 ms</i>"]
        COG["<b>cognitive-core</b><br/>LLM orchestration<br/>▶ <b>safe-mode gate</b> ◀<br/><i>50 ms</i>"]
        VOICE["<b>voice-ui</b><br/>TTS + audio shell<br/>double-buffered<br/><i>18 ms</i>"]
        SENSOR -->|frames| PERC -->|confidence-scored<br/>observation| COG -->|response| VOICE
    end

    POWER["<b>power-mgmt</b><br/>observes whole loop →<br/>Idle / Interactive / Throttled"]
    POWER -.->|DVFS / duty cycle| CORE

    MEM["<b>memory</b> (Phase 15)<br/>people · reminders · events<br/>local SQLite — on-device only<br/><b>▶ no CONTENT path to companion-sync ◀</b><br/>counts &amp; states only, under consent (Phase 22)"]
    COG -.->|"recall · name<br/>[route:memory]"| MEM
    MEM -.->|"enriched recall /<br/>due reminder"| VOICE

    COMP["<b>companion-sync</b><br/>BLE / WiFi to caregiver app<br/><b>alerts · status · firmware · digest ONLY</b><br/>— never raw sensor data, never memory —"]
    COG -.->|flag caregiver<br/>on safe-mode| COMP
    VOICE -.->|status| COMP
    MEM -.->|"<b>counts only</b> (Phase 22)<br/>under a consent record"| COMP
    COMP -.->|"validated · rate-limited<br/>· announced (Phase 22)"| VOICE

    PEER["<b>caregiver side</b> — NOT BUILT<br/>in-process fake + loopback stand-in<br/>real BLE GATT = documented stub"]
    COMP <-.->|"sealed frames<br/>AES-256-CTR + CMAC"| PEER

    subgraph APPS["📱 Apps layer — separate OS processes, HARD ISOLATION from core"]
        direction TB
        SUP["<b>app-framework</b> (host side)<br/>Supervisor · Router · PermissionModel"]
        A8["8 apps — each its own supervised process<br/>media · mail · browser · search<br/>video · telephony · camera · gallery"]
        SUP -->|"intent → app (IPC)"| A8
    end

    HUD["<b>hud-compositor</b><br/>ONLY visual surface —<br/>subtitle + 1 icon + status glyph"]

    VOICE ===|"IVoiceBridge<br/><b>public interface only —</b><br/>the one seam"| SUP
    A8 -->|spoken reply| VOICE
    A8 -->|overlay frame| HUD
    VOICE -->|subtitle| HUD

    subgraph EXT["External services — each has a mock ⇄ real toggle"]
        SPOT["Spotify"]:::mock
        GML["Gmail"]:::mock
        GS["Google Search"]:::mock
        YT["YouTube"]:::mock
    end
    A8 -.->|"--mock (default) / --real<br/>+ ECHO_WITH_NETWORK build"| EXT

    class SPOT,GML,GS,YT,PEER mock;
    style CORE fill:#f0f6ff,stroke:#3366cc
    style APPS fill:#f6fff0,stroke:#339933
    style EXT fill:#f7f7f7,stroke:#999999
    style SENSOR fill:#e8f0ff,stroke:#3366cc
    style PERC fill:#e8f0ff,stroke:#3366cc
    style VOICE fill:#e8f0ff,stroke:#3366cc
    style COG fill:#ffe8e8,stroke:#cc3333
    style MEM fill:#eef0e8,stroke:#5a7d2a
    style HUD fill:#fff8e8,stroke:#cc9933
```

> **Reading the lines.** Solid arrows in the blue box are the real-time data path
> (sensor → perception → cognitive → voice). Dotted lines are out-of-band
> signals (power scaling, caregiver alerts, mock/real toggles). The double line
> is the **single sanctioned seam** between the apps layer and the core: apps
> reach core audio through `IVoiceBridge` — `voice-ui`'s *public* interface — and
> nothing else.

## The core loop (blue box)

The safety-critical path is four modules wired in one direction by the `Runtime`
in [`boot/`](../boot). Every stage carries a slice of the **120 ms
perception-to-response budget**, which is encoded as data in
[`common/include/echo/latency.hpp`](../common/include/echo/latency.hpp) and
asserted by a unit test — a build-enforced contract, not a comment.

1. **sensor-pipeline** captures camera/mic/EEG frames and hands them to
   perception over a **single-producer/single-consumer lock-free ring buffer**
   ([`ring_buffer.hpp`](../sensor-pipeline/include/echo/sensor/ring_buffer.hpp)),
   so capture never blocks inference. *(5 ms)*
2. **perception** runs wake-word detection, ASR, and the quantized face/object
   CNN, emitting a **confidence-scored** `Perception` observation. *(45 ms)*
3. **cognitive-core** is where design principle #5 — *"fail safe, not smart"* —
   lives. It is the **safe-mode gate** (below). *(50 ms)*
4. **voice-ui** speaks the response through a double-buffered, tone-aware audio
   shell (reassuring vs. neutral), and drives the HUD subtitle. *(18 ms)*

`power-mgmt` observes the whole loop and trades duty cycle for thermal headroom
without stuttering an in-flight response — as of Phase 18 it duty-cycles vision on
battery and throttles inference on temperature, wired into the Phase-17 watchdog (see
the power & thermal section below); `companion-sync` receives caregiver alerts,
status, and — as of Phase 22, only under a recorded consent — an hourly digest of
**counts and states**, but **never** sensor data and never memory content.

### The safe-mode gate — the heart of the product

The device is worn by people living with memory loss, so a confident wrong
answer is worse than no answer. In
[`cognitive-core`](../cognitive-core/include/echo/cognitive/cognitive_core.hpp),
`respond()` scores its confidence against `SafeModeConfig::min_confidence`
(default **0.72**). Below that threshold — or on any model failure — it does
**not** let the LLM guess. It returns a `ResponseKind::SafeMode` response: a
short, warm, known-good line (*"I'm not quite sure right now. Let's take a
moment."*) and sets `flag_caregiver = true`, which `companion-sync` turns into a
`SafeModeEngaged` alert. The interface guarantees this: `respond()` never throws;
a failure degrades to safe mode rather than propagating.

That is the **input** side of the gate, and until Phase 21 it was the only side:
once a clean transcript cleared 0.72, the model's sentence was spoken verbatim.
The gate now also scores the **answer**, with two mechanisms of deliberately
unequal weight:

- **Grounding**, which does the real work. `is_self_referential_query()` (in
  [`memory/utterance.hpp`](../memory/include/echo/memory/utterance.hpp), beside
  the naming and identity classifiers) recognizes a question about the wearer's
  own life — *did I take my tablets, who visited yesterday, where did I put my
  keys*. Those are answered **from the memory store or not at all**: the LLM is
  never consulted, not even as a fallback. This needs no probability estimate, so
  no amount of model weakness can erode it.
- **A degeneracy floor** on `LlmReply::confidence` — the model's own mean token
  probability, derived from real logits and defaulting to a low **0.35**. It
  catches collapsed generation. It is a *fluency* signal and does not catch a
  fluent falsehood; see ADR-18 for why the threshold is low on purpose.

Either produces `ResponseKind::Unverified` with its own known-good line (*"I
don't have a record of that, so I'd rather not guess."*). Three decline reasons
now exist and stay distinguishable — input unclear (`SafeMode`), answer
ungrounded (`Unverified`), engine silent (the runtime's own engine-fault line) —
the same separation Phase 17 insisted on. An abstention does **not** flag the
caregiver: one honest "I don't know" is correct behaviour, and only a *streak*
of them raises `AlertKind::LowConfidenceTrend` from the runtime.

### The memory & recall engine — what "remembers for you" actually means

[`memory/`](../memory) (Phase 15) is the first module whose whole purpose is the
product's differentiator, and it is wired *into* the loop, not bolted on. `boot/`
owns the engine and injects a non-owning pointer into `cognitive-core`:

- **Recall.** `perception` now hands up each face's **SFace embedding** (a derived
  vector, never pixels) on the `FaceObservation`. `cognitive-core` matches it against
  the store and, on a confident hit, returns an enriched *"That's Priya, your
  daughter. You last saw her two days ago"* — computed from the record's `last_seen`,
  **before** the safe-mode gate (a stored fact is retrieval, not a guess the gate
  should suppress).
- **Naming is the only way a person is created.** *"This is my daughter Priya"* binds
  a name to the embedding in view. An unknown face with no naming utterance creates
  nothing — the store never invents an identity for a stranger.
- **Reminders** are checked on the **existing core tick** (no second timer loop) and
  spoken through `voice-ui` like any other line; recurrence advances on acknowledge.
- **Memory-queries** ride Phase 6's route-tag parser: the LLM tags an on-device
  memory question `[route:memory]` and the engine answers from the **real event log**
  (or returns nothing, so the system falls back rather than fabricate).

**What is persisted, and — deliberately — what is not.** The store holds exactly
three narrow things: **person records** (name, relationship phrase, SFace embedding,
last-seen), **reminders** (text, due/recurrence, acknowledged), and an **event log**
of *notable* moments only — a known person recognized, a reminder fired or
acknowledged. It does **not** log verbatim transcripts of conversation: raw speech is
transcribed for the current turn and discarded, and only these caregiver-legible
summaries ("Saw Priya", "Reminded: take medication") are written. Everything a
caregiver could later review is something they could reasonably be shown; nothing
private-by-accident is captured.

**The privacy boundary is structural, not a policy.** The store is **on-device
only** (local SQLite; no new network path — constraint #2), and its raw content
**cannot reach `companion-sync`**: the same compile-time proof technique from
Phase 10 (companion-sync can't accept a `SensorFrame`) is extended in
`tests/memory_engine_test.cpp` to prove no send path can accept an embedding, a
person record, or an event.

Phase 22 added a caregiver digest and **did not weaken any of that** — it extended it.
What crosses is a `CaregiverDigest` of counts and states, and the same TU now also
proves the digest is not *constructible* from an embedding, a person record, an event
or a reminder, and that `InboundCommand` is not constructible from an embedding either
(the naming rule, as a compile-time fact). Both halves were re-verified by inverting an
assertion and confirming the build fails. See the Phase 22 section below.

**Encrypted at rest (Phase 16).** At rest the on-disk file is **AES-256-CTR
ciphertext**, keyed by a per-device key file (owner-only, same posture as appkit's
`.echo-tokens/`); the plaintext SQLite image lives only in process RAM (the working
DB is an in-memory connection, serialized→encrypted on checkpoint). This provides
**confidentiality** for a copied/imaged database, honestly **not** authentication or a
hardware-backed key — SQLCipher was ruled out because this toolchain has no OpenSSL to
back it, so a dependency-free in-tree AES (KAT-proven) is used instead. An existing
Phase-15 *plaintext* database is migrated to the encrypted format on open. Growth is
bounded on the existing scheduler tick (event log by count+age; notes cap-and-evict).
See [ADR-14](DECISIONS.md) and [ADR-15](DECISIONS.md); the earlier permission-only
baseline was [ADR-13](DECISIONS.md).

### Fault tolerance & graceful degradation (Phase 17)

The safe-mode gate above answers *"the engine responded, but I'm not confident."*
Phase 17 answers the different question the wearer's safety actually depends on:
*"what if an engine doesn't respond at all — it throws, or it hangs and never
returns?"* The wearer may not notice or be able to reset a device on their own
face, so **no single engine is allowed to take the runtime down or leave the wearer
stuck in silence.**

**Containment at every engine boundary.** Each call into wake-word / ASR / vision
(`perception`), the LLM (`cognitive-core`), TTS (`voice-ui`), and the store
(`memory`) now runs through an
[`EngineGuard`](../boot/include/echo/boot/engine_guard.hpp). The guard (1) contains
exceptions — a throw becomes a `fail(HardwareError)` `Result`, never an unwind
through the loop; (2) bounds hangs — the call runs on a worker and is **abandoned**
if it outruns a per-engine budget, returning `fail(Timeout)`; and (3) records health
for the watchdog. A final `try/catch` backstop around the whole tick contains
anything unanticipated (e.g. from `power-mgmt`) as defense in depth.

**Defined degraded behaviour — decided, not accidental** (in
[`boot/src/runtime.cpp`](../boot/src/runtime.cpp)):

| Engine fault | Degraded behaviour | Never does |
|---|---|---|
| **Vision** fails (throw/hang/error on a camera frame) | Falls back to **voice-only**: skips visual recall for that frame, keeps running | **Never fabricates a face match** (constraint #2 — the same "don't guess" rule as the safe-mode gate) |
| **ASR / wake** fails on an audio frame | Speaks a calm retry — *"Sorry, I didn't catch that. Could you say it again?"* | Leaves the wearer in silence, or crashes the turn |
| **LLM** doesn't respond (throw/**timeout**) | Speaks a known-good engine-fault line and raises an `EngineDegraded` caregiver alert | Conflate with the low-confidence gate — that path is an *Ok* response and is untouched (constraint #1) |
| **Memory** write fails (disk full/corruption) | The reminder/recall is **still delivered this session**; the failure is logged; an in-session set stops it repeating | Drop the reminder silently, or crash on a failed write |

The LLM case is deliberately **kept separate** from the safe-mode confidence gate:
low confidence is a normal `Ok` `SafeMode` response decided *inside* `respond()`;
a non-responding engine is a *failed call* handled by the runtime. Both coexist —
the fault-injection suite asserts they take different paths and speak different lines.

**The watchdog — and an honest account of what it can recover.** A watchdog runs on
the **same core tick** as reminders and retention (constraint #4 — no fourth timer).
It reads each guard's health and:

- **Fully recovers** transient throws and error-status returns *by construction*:
  containment poisons no state, so the very next call succeeds once the fault clears
  (asserted: a perception throw, then a normal turn flows end-to-end).
- **Detects a hang** (a call that never returns) via the per-engine timeout, keeps
  the pipeline ticking by abandoning the wedged worker, and **attempts in-process
  recovery** by re-initializing that engine (itself guarded). This recovers a hang
  whose engine re-initializes cleanly.
- **Escalates to a physical reboot** when in-process recovery is exhausted
  (`max_recoveries`): it sets `reboot_required()` and `run()` exits so a supervising
  init system can restart the process/device. This is the honest fallback for the
  class the watchdog *cannot* fix in process — a real restart, not a pretence of
  in-process magic.

What it **cannot** do, stated plainly because this is a safety claim:

- **It cannot kill a wedged thread.** C++ has no safe thread-cancellation, and a
  `std::async` future's destructor joins. So a call that *truly never returns* leaks
  one worker thread (parked in the guard) until the process restarts; if that thread
  holds a resource the re-initialized engine also needs, or the hang is inside a
  driver/kernel call, re-init hangs too and the watchdog escalates to reboot. A
  genuinely wedged engine can also block process exit at shutdown.
- **It only catches C++ exceptions and timeouts.** A segfault, a memory-corruption /
  UB, a `std::terminate` from a `noexcept` violation, or OOM is **not** containable
  by a `try/catch` — those still take the process down, and a supervising init system
  (or a hardware watchdog) is the only recovery. The guard bounds *misbehaviour*, not
  *undefined behaviour*.
- **The hang bound has a cost.** Each guarded call is dispatched to a worker with a
  copy of its argument, which adds thread-dispatch + copy overhead on the hot path.
  It is acceptable for this scaffold and the small per-tick call count; a production
  build would likely use a lighter mechanism (a persistent per-engine worker, or a
  hardware watchdog timer) — noted in [ADR-16](DECISIONS.md), not hidden.

The `docs/STATE.md` "fault tolerance" table is the precise, per-class ledger of what
is now contained, what degrades, and what still requires a physical restart.

### Power & thermal management (Phase 18)

Phase 17 hardened the core loop against *software* failure; Phase 18 hardens it against
the *physical* reality of running on a battery and generating heat on someone's temple.
`power-mgmt` — a top-level module since the first scaffold, but logic-free until now —
becomes three things:

1. **A read-only sensing boundary.** `IPowerSource` (battery percent + charging) and
   `IThermalSource` (a coarse nominal/warm/hot state, plus an optional numeric SoC temp
   as telemetry) — the same interface + real-hook + deterministic-fake discipline as every
   engine. The thermal signal is a discrete STATE, not a temperature, because a temple-worn
   device's realistic input is a few thermal-zone trip points and the policy needs a throttle
   *level* ([ADR-17](DECISIONS.md)).
2. **A pure, deterministic policy** (`power_policy.hpp`): BATTERY drives vision **duty-cycling**
   (full > 40 %, reduced 15–40 %, voice-only off < 15 %); THERMAL drives an inference
   **throttle** (warm/hot relax the latency budget ×1.5 / ×2.0). Two independent levers,
   combined into one `PowerDecision`.
3. **Application on the existing tick** by the `Runtime`. Un-sampled camera frames are dropped
   (slower recognition, **never a fabricated result** — the Phase-15/17 "never guess" rule); the
   thermal throttle scales the Phase-17 **cognitive hang budget up**, so a throttled-but-slower
   turn is not mistaken for a hang (the throttle integrates WITH the watchdog, it doesn't fight
   it); and low-battery / high-thermal conditions surface through the SAME
   `AlertKind::EngineDegraded` caregiver path — not a parallel device-health channel.

**Honest recovery scope, as everywhere else.** This closes a policy gap, not a hardware one.
There is no real fuel gauge or skin sensor to read, so the real backend is a documented stub and
the low-battery reminder pass fires on a threshold, not a true shutdown prediction. The policy
LOGIC is CI-verified against simulated readings (`echo-power-mgmt`); real device power/thermal
behaviour is a permanent-until-hardware gap (STATE.md gap #12), logged with the same honesty as
the live-mic gap since Phase 9.

### Wandering & distress detection (Phase 23)

`AlertKind::Wandering` and `AlertKind::Distress` were defined in Phase 1 and never had a
producer. `safety-mgmt` gives them one, mirroring Phase 18's three-part shape exactly:

1. **A read-only sensing boundary.** `ILocationSource` (a coarse home/boundary/away zone
   plus how long it's held) and `IArousalSource` (calm/elevated/high, same shape) — the same
   interface + real-hook + deterministic-fake discipline as power/thermal. Coarse STATE, not
   raw GPS lat/long or a biometric score, for the same reason Phase 18 chose a thermal state
   over a temperature: the policy needs a risk band, not a setpoint.
2. **A pure, deterministic policy** (`safety_policy.hpp`): each axis escalates
   None → Suspected → Confirmed against a grace period (a brief boundary dip must not alarm;
   a sustained one should), independently — a wandering episode implies nothing about
   distress, or vice versa.
3. **Application on the existing tick** by the `Runtime`, immediately after the power
   decision. Unlike power, this gates nothing else in the tick — its only job is
   edge-triggered alerting: a Confirmed risk raises `AlertKind::Wandering`/`::Distress` once
   per episode (re-arming after recovery), and a sensing fault rides the EXISTING
   `AlertKind::EngineDegraded` channel instead — a device-health condition, not a
   wandering/distress event, keeping failure modes distinguishable (Phase 17's rule). Each
   confirmed episode also increments a `CaregiverDigest` count (`wandering_flags`/
   `distress_flags`), the same "how the wearer's day went" bucket as
   `safe_mode_engagements`/`unverified_answers`.

**Honest recovery scope, as everywhere else.** There is no real geofence/GPS chip and no real
biometric/prosody classifier to read — not on this dev laptop, not on the not-yet-existing
glasses. The real backends are documented stubs that always report the safe state (there's no
plausible varying value to simulate at all, unlike battery drain). The POLICY is CI-verified
against simulated readings (`echo-safety-mgmt`); real sensing is a permanent-until-hardware gap
(STATE.md gap #16), and the grace-period thresholds chosen are provisional, not clinically
validated.

### The caregiver boundary (Phase 22)

The one outbound path that a person outside the device reads. It is structured so that
minimization is a property of the **type system**, not of reviewer vigilance.

```
memory store ──counts──▶ CaregiverLink ──▶ CaregiverDigest ──▶ SecureChannel ──▶ ICompanionTransport
   (names,               (re-reads       (7×u32, 2×i64,       (AES-256-CTR      (fake | loopback |
    embeddings,           consent          2 enums —           + CMAC,           real BLE = STUB)
    summaries)            EVERY tick)      46 fixed bytes)     encrypt-then-MAC)
        │                                        ▲
        └────────────── no path ─────────────────┘
                  (static_assert, two TUs)
```

Four things carry the design:

1. **The digest is a type, not a convention.** Every field is a scalar or an enum, and
   the struct plus its per-field assertions come from one X-macro list, so the proof
   cannot drift from the struct. The encoder therefore has no branch that could emit a
   length-prefixed string — it cannot leak content because the type has none. The wire
   frame is **46 bytes whether the store holds 2 people or 22**, which is the property
   the run-time test asserts directly.

2. **Consent is re-read, never cached.** `caregiver_tick()` re-asserts the consent record
   from the store on every tick; that is what makes revocation cold on the next one
   rather than "cold once something restarts". No consent ⇒ `Unavailable`, never an
   empty digest — an all-zero digest is a *claim* about the wearer's day.

3. **The transport is contained like every other engine boundary**, but on its **own**
   `EngineGuard` (`companion_budget`, 2 s) rather than the memory guard, and deliberately
   *not* supervised by `watchdog_tick()`. A caregiver who walks out of range is a normal
   condition, not a degraded device; charging a slow radio to the memory engine would
   escalate through recovery and eventually reboot the glasses because someone's phone
   went out of range.

4. **Inbound is hostile until proven otherwise.** Sealed → MAC → direction → replay →
   consent → schema (Phase 6 hardened parser) → rate limit → **announce to the wearer** →
   apply. Firmware rides the same channel but is stashed, never applied inline, and
   `poll_firmware_update()` **fails closed**.

**What is not here:** the caregiver application. This is the device half plus an honest
test double for the other half — an in-process fake and a loopback pair, both running
inside one process with no network. Real BLE GATT returns `Unavailable` and says why.
See ADR-19 and STATE.md gap #15.

## The isolation boundary (green box)

The apps layer — media, mail, browser, search, video, telephony, camera,
gallery — sits *beside* the core, not inside it. The boundary is real, not
aspirational, and it is drawn **in the build graph**:

- **Apps are separate OS processes.** `app-framework`'s `Supervisor` spawns each
  app as its own child process, detects a crash or hang, and restarts it with
  bounded backoff. A misbehaving app is a local event that **cannot block, slow,
  or crash** the sub-120 ms loop.
- **`echo::app-sdk` links no core module.** The half of the framework an app
  links (contract, permissions, HUD primitives, IPC codec, intent parser) has
  **zero** dependency on `perception`, `cognitive-core`, or the sensor path —
  enforced by CMake, not by comment.
- **One seam only: `IVoiceBridge`.** The single point where an app's output
  reaches the core is the voice bridge, which calls `voice-ui`'s *public*
  interface to speak a line. Apps never touch core internals or the latency
  budget.
- **The HUD is the only screen.** There is no window manager and no framebuffer.
  The [`hud-compositor`](../apps/hud-compositor) exposes exactly three overlay
  primitives — subtitle text, one icon, a status glyph — and nothing else. A
  scrollable list or a tappable button is *not expressible*. This is design
  principle #1 (premium and calm) made structural.

### The mock ⇄ real toggle

Every external dependency has a clean interface with two backends behind it, so
the whole voice flow is testable today and real credentials slot in later with
**zero interface changes**:

| Dependency | Mock (default) | Real path | Selected by |
|------------|----------------|-----------|-------------|
| Spotify / Gmail / Search / YouTube | realistic fake data | live API backend | `--real` flag **and** `-DECHO_WITH_NETWORK=ON` build |
| Wake word / ASR / LLM / TTS / vision | deterministic stub adapters | whisper.cpp · llama.cpp · Piper · Porcupine · OpenCV | `ECHO_WITH_*` CMake options (all default OFF; `-DECHO_REAL_AI=ON` flips all) |
| HUD / mic / speaker I/O | headless / typed input | SDL2 laptop overlay + audio | `-DECHO_WITH_SDL=ON` |
| Network transport | absent (backends report `Unavailable`) | libcurl | `-DECHO_WITH_NETWORK=ON` |

The default build is **dependency-free**: every toggle is OFF, so the stub build
compiles on any host toolchain and stays deterministic in CI. See the
[README](../README.md) Phase 3 and Phase 5 sections for the per-engine detail.

The **memory engine is the exception that proves the rule**: it is *not* behind an
`ECHO_WITH_*` toggle, because persistence across restarts is its entire point and
must always be present. It keeps the stub build dependency-free anyway by vendoring
the single-file SQLite amalgamation in-tree ([ADR-13](DECISIONS.md)) — real on-disk
persistence with zero external packages. In the stub build the memory *tests* run
against fixture embeddings, so no OpenCV/ORT is needed to exercise the store. Its
Phase-16 encryption stays dependency-free the same way: a small in-tree AES-256
(no OpenSSL/SQLCipher), so the stub build gains encryption at rest with still-zero
external packages ([ADR-14](DECISIONS.md)).

## Where to look next

- **What's built vs. pending, and coverage by module:** [STATE.md](STATE.md)
- **How to actually run the real stack, end to end:** [RUNBOOK.md](RUNBOOK.md)
- **Why it's built this way (the architectural choices):** [DECISIONS.md](DECISIONS.md)
