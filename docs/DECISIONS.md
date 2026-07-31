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

## ADR-12 — A dedicated on-device memory & recall engine, wired into the loop
*Decided: Phase 15 · 2026-07-31*

**Decision.** Add a new `memory/` module that persists the three things the product
is actually about: **people** (a name + relationship the wearer stated, plus the
SFace embedding that identifies them), **reminders** (text + due time/recurrence +
acknowledged state), and a narrow **event log** (person-seen, reminder-fired/
acknowledged). It is wired *into* the existing pipeline, not bolted alongside it:
`cognitive-core` holds a non-owning pointer to it and, on a confident face match,
returns an **enriched recall** ("That's Priya, your daughter. You last saw her two
days ago.") *before* the safe-mode gate — a stored fact is retrieval, not an LLM
guess. The `boot/` runtime checks due reminders on the **same core tick** (no second
timer loop) and speaks them through the existing `voice-ui` path. A new
**`[route:memory]`** tag (parsed by Phase 6's unchanged route-tag parser) lets the
LLM defer a memory question to the engine and answer from the real record.

**Why.** Phases 1–14 built real, working *generic* voice-assistant plumbing; none
of it remembered anything. "Glasses that remember for you" needs a component that
actually persists who a face belongs to and what the wearer must do. Putting recall
*before* the confidence gate is deliberate: ADR-4 forbids the LLM from *guessing*,
but recalling a person the wearer explicitly named is a lookup, and gating it on the
(numerically low) face-cosine confidence would suppress the product's core feature.

**Trade-off.** `cognitive-core` now depends on `memory`, and `respond()` grew three
memory branches. Kept honest by guarding every branch on an attached, open engine so
the pre-Phase-15 behavior (and every existing test) is byte-for-byte unchanged when
no store is present. A person is created **only** when the wearer names someone —
never auto-invented from an unknown face (proven by test); the cost is that ECHO
stays silent about strangers until told who they are, which is the correct default.

## ADR-13 — SQLite (vendored amalgamation) for the store; permissions, not encryption
*Decided: Phase 15 · 2026-07-31*

**Decision.** The memory store is **SQLite**, vendored as the single-file
**amalgamation** (`memory/vendor/sqlite3/sqlite3.c`, v3.46.1, public domain) and
compiled into a small `echo-sqlite3` static lib. Not a hand-rolled flat file, and
not an in-memory-only toy — the database is a real file on disk (`echo_memory.db`),
so records survive a reboot, which is the entire point of a memory device.

**Why.** An embedded record store with concurrent readers/writers (perception writes
sightings, cognitive-core reads for recall, a future companion-sync export reads for
a caregiver view) is exactly SQLite's job: ACID, WAL-mode concurrency, a stable
on-disk format, and a decades-hardened C core. Vendoring the amalgamation keeps the
**dependency-free stub build** (ADR-8) green — it is one C file compiled in-tree,
needing no system package — so the memory tests run with zero external deps, against
fixture embeddings, on the same code path CI already uses.

**MinGW reality (constraint #4).** SQLite 3.46.1 compiles **cleanly** under the
project's MinGW-w64 ucrt g++ 15.1 toolchain — ~10 s, zero warnings, links and runs
first try. This is the opposite of the ORT/OpenCV native-dep friction documented in
Phase 14b (see [STATE.md](STATE.md)); there was no toolchain gap to work around, so
none was invented. C had to be enabled as a project language for that one TU.

**Trade-off — honest scope.** What this phase achieves at rest is **file-permission
restriction** (best-effort owner-only via `std::filesystem::permissions`), **not
encryption-at-rest**. On POSIX that is chmod 0600; on Windows `std::filesystem` maps
it loosely (ACL inheritance still applies), so the guarantee is not overstated.
True encryption would be **SQLCipher** (a heavier, non-amalgamation dependency) and
is deliberately left as a follow-up. The privacy guarantee this phase *does* make
absolute is structural: the store's raw content **cannot reach `companion-sync`** —
proven at compile time (extending ADR-5's `SensorFrame` proof to embeddings, person
records, and events; see `tests/memory_engine_test.cpp`). Recognition is a linear
cosine scan over all stored people — fine for the handful a wearer knows, an ANN
index is future work if the enrolled set ever grows large.

---

## ADR-14 — Encryption at rest: AES-256-CTR over the serialized image, not SQLCipher
*Decided: Phase 16 · 2026-07-31*

**Decision.** The memory store is encrypted at rest by holding the working database
in an **in-memory** SQLite connection (loaded with `sqlite3_deserialize`) and writing
its serialized image to disk as **AES-256-CTR ciphertext** (container:
`ECHOAES1` magic · 16-byte random IV · ciphertext). The AES-256 is a small,
dependency-free, in-tree implementation (`memory/src/aes256.cpp`), and its correctness
is pinned to the **FIPS-197** and **NIST SP 800-38A** known-answer vectors in
`tests/memory_engine_test.cpp`. The SQL logic in `memory_engine.cpp` is unchanged — it
runs against an ordinary SQLite connection either way; only the open/close paths differ.

**Why not SQLCipher (tested for real first).** SQLCipher is the textbook answer, and we
smoke-tested the assumption before committing to it — exactly as Phase 15 smoke-compiled
the SQLite amalgamation and Phase 14b smoke-tested ORT. The finding is concrete:
SQLCipher is **not** a self-contained amalgamation like SQLite — it must be generated
from a source tree **and** linked against a crypto backend (OpenSSL `libcrypto`), and
this project's MinGW-w64 ucrt toolchain has **no OpenSSL** (verified: no `openssl/*`
headers, no `libcrypto`; a bare `-lcrypto` link fails). Adding OpenSSL would break the
**dependency-free, vendorable stub build** (ADR-8 / constraint #3) — the exact native-dep
friction Phase 14b documented, the opposite of SQLite's clean in-tree vendoring. So, per
Phase 16 constraint #4, we shipped the **documented fallback** rather than force a heavy
dep or silently drop the requirement.

**Trade-off — honest scope.** This provides **confidentiality at rest**: a copied /
backed-up / offline-imaged `.db` is unreadable without the per-device key, a plain
`sqlite3_open` of the raw file reads garbage, and stored names do not appear as plaintext
in it (all asserted). It is **NOT**: (a) authenticated — there is no MAC; a wrong key or
corruption is caught only by a post-decrypt "must begin with `SQLite format 3`" sanity
check, and the engine then fails **closed**; (b) hardware-backed — the key is a random
per-device key in a local owner-only key file (same posture as appkit's `.echo-tokens/`),
so an attacker with live read access to **both** the `.db` and the key file can decrypt;
(c) per-transaction durable — the encrypted image is rewritten on close and on the
retention tick (checkpoint durability), and the whole DB is resident in RAM. All three
are acceptable for a wearable's small people/reminders/events store and are stated
plainly rather than oversold. TPM/secure-enclave or paired-phone key binding, and an
authenticated cipher (AES-GCM), are the honest follow-ups. Migration of an existing
unencrypted Phase-15 database is handled on open (detected by the SQLite magic, rewritten
encrypted, logged) so a wearer's records are never discarded.

## ADR-15 — Bounded growth: event log capped by count+age; notes cap-and-evict, not summarized
*Decided: Phase 16 · 2026-07-31*

**Decision.** The append-only **event log** is bounded by **both** a row-count cap and
an age cap (defaults 2000 / 90 days, `ECHO_MEMORY_MAX_*`-overridable), enforced on the
**same scheduler tick** that already delivers reminders (no third timer — Phase 15's
"reuse the loop" rule holds). Person **notes** are bounded differently: they are **not**
aged out — remembering people over time is the point — but a per-person segment cap
evicts oldest-first (default 20), applied both on the tick and immediately on each
`add_note`.

**Why cap-and-evict, not LLM summarization.** Summarizing old notes into a running
summary with the local LLM was the tempting richer option, and the phase brief allowed
it *if it genuinely proved out*. It doesn't clear the bar: Phase 15 already documented
how fragile the tiny CI model's prompt-following is (the route-tag prompt had to be
iterated against the real model). Making note retention depend on that fragile behavior
would trade a **guaranteed** bound for a probabilistic one, on a data-loss-sensitive
path. So we shipped the simple, robust cap-and-evict and deferred summarization — the
same "don't force a fragile feature" judgment the brief asked for.

**Trade-off — honest scope.** Pruning the event log means old sightings/reminders age
out of the on-device history (a caregiver-view export, if one is ever built, must read
before the cap, not after). Pruning touches **only** the events table, so a retained
acknowledgment's reminder state is untouched (asserted). Notes eviction is by segment
count, not semantic importance — a blunt but predictable rule; summarization would
preserve more meaning per byte and is the follow-up if the tiny-model fragility is ever
resolved.

## ADR-16 — Fault containment via a per-engine guard with a worker-thread hang bound
*Decided: Phase 17 · 2026-07-31*

**Decision.** Every engine call site (wake-word/ASR/vision, LLM, TTS, memory) runs
through an `EngineGuard` (`boot/`) that (a) contains exceptions into a `fail(...)`
`Result` on the existing `common/result.hpp` `Status`, and (b) bounds a **hung** call
by running it on a `std::async` worker and abandoning it if it outruns a per-engine
budget (`Status::Timeout`). A watchdog on the **existing** core tick (constraint #4 —
no fourth timer) reacts to guard health: it recovers transient throws by construction,
attempts in-process re-init of a hung engine, and escalates to a `reboot_required()`
request when in-process recovery is exhausted. Degraded-mode behaviour is defined
per engine in the runtime (vision→voice-only, ASR→spoken retry, LLM-no-response→calm
engine-fault line, memory-write-fail→deliver-this-session-anyway).

**Why a worker-thread timeout, and not something lighter.** Detecting a call that
*never returns* is impossible from the same thread that is blocked inside it — the
only portable way to bound it is to run it elsewhere and wait with a timeout. We reused
the existing `Result`/`Status` vocabulary rather than invent a second error channel
(the brief's constraint #1 preference), and reused the boot/scheduler tick for the
watchdog rather than add a timer (constraint #4). The engine-not-responding path is
kept **separate** from the low-confidence safe-mode gate (constraint #1): the gate is an
`Ok` `SafeMode` response decided inside `respond()`; a non-responding engine is a failed
*call* handled by the runtime. Both are asserted to take different paths in
`tests/fault_injection_test.cpp`, which injects throw/error/hang at every boundary in
the dependency-free stub build (constraint #3 — no real engines needed).

**Trade-off — honest scope.** This is containment of *misbehaviour*, not of *undefined
behaviour*. (a) A `try/catch` cannot catch a segfault, memory corruption/UB, a
`noexcept`-violation `std::terminate`, or OOM — those still crash the process and need a
supervising init system or hardware watchdog. (b) C++ cannot safely kill a wedged
thread and a `std::async` future's destructor joins, so a call that *truly* never returns
leaks one worker thread until restart and can block process exit at shutdown; if the hang
is in a driver or holds a lock the re-init needs, in-process recovery fails and we escalate
to a real reboot — the honest fallback, not a pretence of magic recovery. (c) The hang
bound costs a per-call worker dispatch + a copy of the call's argument on the hot path,
acceptable for the scaffold's small per-tick call count but heavier than the "zero jank"
ideal; a production build would likely use a persistent per-engine worker or a hardware
watchdog timer. A new `AlertKind::EngineDegraded` distinguishes a subsystem fault from the
existing low-confidence `SafeModeEngaged` alert at the caregiver layer. What is now
fault-tolerant, what still crashes, and what needs a physical restart is enumerated
precisely in `docs/STATE.md` — as with ADR-14's encryption scope, the claim is bounded on
purpose.

---

*To add an entry: append with the next ADR number, a date, the decision, the why,
and the honest trade-off. Keep it short — this is a log of real choices, not a
design spec.*
