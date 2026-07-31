# ECHO OS — State of the Project

*One honest page: what's built, what's verified, what's still pending real-world
validation, how well it's tested, and what quality gates are in place. This is
the document to read first — a YC partner or a new engineer should be able to
trust every line of it. It is an internal reference, not marketing copy.*

Last updated: Phase 17 (fault tolerance & graceful degradation — containment and a
hung-call watchdog around every engine boundary; see the dedicated section below).
Source of truth for every claim is the repo at this commit; nothing here is
aspirational unless explicitly labelled.

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

## Quality gates in place (CI)

Every push and PR to `master` runs [`.github/workflows/ci.yml`](../.github/workflows/ci.yml):

| Gate | What it enforces |
|------|------------------|
| **Stub build + full ctest** | dependency-free build, all 11 suites (incl. Phase 15 `echo-memory`, the memory-enriched `echo-e2e-fixture`, and Phase 17 `echo-fault-injection`) — the always-green safety net. The vendored SQLite amalgamation compiles in-tree here with zero external deps |
| **clang-tidy / cppcheck scope** | both now cover the first-party `memory/` code; the vendored `memory/vendor/sqlite3/` amalgamation is **excluded** from both (upstream C we compile but do not lint) |
| **Network build** | `-DECHO_WITH_NETWORK=ON` compiles & links libcurl; appkit/apps tests pass on a clean machine (no live API calls) |
| **Secret scan** | `check_secrets.sh` blocks a committed API key, OAuth secret, private key, token cache, or `.env` |
| **clang-tidy** | `bugprone-*` / `cert-*` / `clang-analyzer-*` over every first-party TU; findings are hard errors |
| **cppcheck** | second independent analyzer; `warning`/`performance`/`portability` fail the job |
| **Fuzz (JSON)** | bounded libFuzzer under ASan/UBSan on the one parser that eats untrusted bytes |
| **Coverage** | gcov + gcovr; % printed to the run summary, XML to Codecov |
| **Real engines** (Phases 11 + 13) | `-DECHO_WITH_WHISPER/OPENCV/PIPER/OPENWAKEWORD=ON`; downloads + **SHA-256-verifies** the free models (incl. the openWakeWord ONNX pipeline + ONNX Runtime) and runs `tests/real_*` (ASR, vision, TTS, **wake-word**) against real weights on the fixtures + Piper-synthesized speech. Runs in parallel; models + whisper build cached |
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
