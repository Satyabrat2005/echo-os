| Area | Status | Evidence |
|------|--------|----------|
| Module boundaries & public interfaces | **Built** | `common`, `sensor-pipeline`, `perception`, `cognitive-core`, `voice-ui`, `companion-sync`, `power-mgmt`, `boot` all compile as static libs |
| Lock-free SPSC ring buffer (sensor handoff) | **Built & tested** | [`ring_buffer.hpp`](../sensor-pipeline/include/echo/sensor/ring_buffer.hpp); fixture frames traverse the real queue in `echo-sensor` |
| 120 ms latency budget as an enforced contract | **Built & tested** | encoded in [`latency.hpp`](../common/include/echo/latency.hpp), asserted by unit test |
| Safe-mode gate, INPUT side (principle #5) | **Built & tested** | [`cognitive_core.cpp`](../cognitive-core/src/cognitive_core.cpp), threshold 0.72; degrades on failure |
| **Safe-mode gate, ANSWER side** (principle #5, the other half) | **Built & tested** (Phase 21) | `echo-answer-gate` drives the real core + real memory store with a scripted LLM through the private DI seam. Questions about the wearer's own life are answered **from the record or not at all**; `[route:memory]` no longer falls back to a guess; ungrounded turns return `ResponseKind::Unverified`. **Does NOT detect a confidently wrong answer in general — see the STATE.md Phase 21 section and ADR-18** |
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

# ECHO OS — Phase 13: Real Wake-Word Verification in CI (Account-Free)

## Why this phase, and why it's likely the last of its kind

After Phase 12, the only documented gaps are Porcupine wake-word
(account-gated) and true live-hardware/human behavior. The second is
permanent and cannot be closed in CI — that's fine and expected. The
first is not actually permanent: Phase 3's original scaffold named
openWakeWord as an alternative precisely because it needs no account,
key, or payment — it's Apache-2.0, runs on small ONNX models, and can be
verified against fixture audio the same way whisper/OpenCV/Piper/Qwen
were in Phases 11-12. If this works, Phase 13 is likely the last
"close a real gap in CI" phase — everything remaining afterward is either
live-hardware validation or genuinely new feature work, not gap-closing.

Branch feature/real-wakeword-ci from master (confirm PR #11 is merged
first).

## What to build

1. *Add openWakeWord as a second wake-word backend* behind the existing
   wake_word.hpp interface from Phase 3 — don't rip out the Porcupine
   path (it may still matter for production if you register for a key
   later); add openWakeWord alongside it, selectable via a build flag
   (e.g. ECHO_WAKEWORD_BACKEND=openwakeword|porcupine, defaulting to
   openwakeword since it's the one CI and anyone without a Picovoice
   account can actually exercise).
2. *Pin the exact model file(s)* the same rigorous way Phases 11-12
   did: exact URL, SHA-256 checksum, license, and reasoning, added to
   MANIFEST.md and fetch_real_engine_assets.sh. openWakeWord ships
   pretrained ONNX models (including generic "hey jarvis"-style ones);
   identify the closest available pretrained model to a custom "Hey ECHO"
   phrase, and document honestly whether a truly custom-trained "Hey
   ECHO" model is in scope for this phase or a documented follow-up
   (training a fully custom wake-word model is a bigger undertaking than
   downloading a pretrained one — be honest about which this phase does).
3. *Fixture audio for wake-word testing* — extend Phase 10's fixtures
   with a couple of short clips: one containing the target wake phrase
   (synthesized via Piper, consistent with how Phase 11 synthesized
   speech for the ASR test), and a few "should NOT trigger" clips (the
   existing silence/garble fixtures, plus ordinary speech that doesn't
   contain the wake word) to test for false accepts, not just true
   positives.
4. *tests/real_wakeword_test.cpp* — real openWakeWord inference
   against the fixtures: the wake-phrase clip should trigger detection
   above threshold; the non-wake clips should not (within a documented,
   reasonable false-accept tolerance — wake-word detection is
   probabilistic, so assert on the model's actual behavior, not a
   demand for perfection it can't honestly meet).
5. *CI job* — extend the real-engines job (or add a parallel one,
   consistent with how Phase 12 handled the heavier llama.cpp build) with
   checksum-verified model download and caching.
6. *Honest STATE.md update* — if this lands, the gap list should now
   read: every real engine (wake-word, ASR, vision, TTS, LLM) verified
   against real models in CI on synthetic/fixture input; the only
   remaining gap is live-hardware behavior with a real human, in a real
   room, with real background noise — which cannot be simulated and
   shouldn't be claimed as covered.

## Constraints

1. No account, key, or payment for the openWakeWord path — that's the
   entire point of this phase.
2. Same checksum/caching discipline as Phases 11-12.
3. Be honest about false-accept/false-reject rates observed against the
   fixtures — a wake-word model has real, nonzero error rates; don't
   tune the test to hide that, document the actual observed behavior.
4. Don't remove the Porcupine path — keep it as a documented, better-
   accuracy option for later if the account/key step is ever done; this
   phase adds an alternative, it doesn't delete an existing one.

## Immediate deliverable for this session

1. openWakeWord backend implemented behind the existing interface.
2. Pinned model asset(s) with checksums in MANIFEST.md and the fetch
   script.
3. New wake-word fixture clips, documented like Phase 10's fixtures.
4. tests/real_wakeword_test.cpp, green in CI.
5. STATE.md updated to reflect the (likely final) narrowed gap: only
   live-hardware/human validation remains undone.

## Repo step (run as the final step)

bash
git add -A
git commit -m "Phase 13: real openWakeWord verification in CI (account-free wake-word backend)"
git push -u origin feature/real-wakeword-ci
gh pr create --base master --head feature/real-wakeword-ci \
  --title "Phase 13: real wake-word verification in CI (account-free)" \
  --body-file pr_body.md

# ECHO OS — Phase 14: The Live Run (No More Code)

## Where things actually stand

Every real engine in the pipeline — wake-word (openWakeWord), ASR
(whisper.cpp), vision (OpenCV YuNet/SFace), TTS (Piper), and reasoning
(a real quantized LLM) — has been verified against real models in
automated CI, across 13 phases and roughly a dozen PRs. That is a
genuinely strong, well-tested foundation. It is also, as of right now,
still a foundation nobody has spoken to. This phase has no CMake, no CI
job, and no PR at the end of it — its only deliverable is you, a laptop,
a mic, and ten minutes.

## Step 0 — merge what's open

`PR #12` (Phase 13) is open and green but not yet merged. Merge it before
doing anything else, so the bring-up runs against the real master, not a
feature branch.

## Step 1 — install the real dependencies, for real

Follow `docs/RUNBOOK.md` (built in Phase 9, this is exactly what it's
for) top to bottom on your actual laptop: OpenCV, SDL2/SDL2_ttf,
whisper.cpp, llama.cpp, Piper, ONNX Runtime, and either openWakeWord
(account-free, matches what CI now verifies) or Porcupine (if you've
registered for a key). Download the same pinned model files CI uses —
`MANIFEST.md` has the exact URLs and checksums, so you're running the
identical, verified assets, not different ones you found elsewhere.

## Step 2 — build with everything on

```
./scripts/build_real.ps1
```
Confirm a clean build. If anything fails that CI didn't catch, that's
real signal about your specific machine — fix it and note it, don't
paper over it.

## Step 3 — say it

Run `echo-demo` (or `run_demo`). Say "Hey ECHO" into your actual laptop
mic. This is the first time in this project's history that phrase will
have been spoken to a running instance of the software, rather than fed
in as a Piper-synthesized fixture. Notice what actually happens — not
what you expect to happen.

## Step 4 — the honest test battery

Run the same manual flows documented across Phases 4-6, for real:
- "play some music" → media app
- "who is this" → face recognition, using your own face and someone
  else's
- an ambiguous/out-of-scope question → confirm the safe-mode gate fires
  and the response is calm, not confused
- a real email/search/calendar-style request, if you've registered the
  Phase 5 developer accounts

## Step 5 — measure and record, honestly

Run `analyze_latency.ps1` against at least 20 real turns. Write the real
numbers into `STATE.md`, replacing every remaining "fill from your run"
placeholder. If the total exceeds the 120ms target — likely, on a laptop
CPU — write down the real number and which stage is responsible. This
is not a failure; a true number beats a fabricated one every time,
including in front of YC.

## Step 6 — get one outside reaction

Have one person who has never seen this project sit down with zero
instructions beyond "say 'Hey ECHO' and talk to it." Watch, don't help.
Write down exactly what confused them or what worked. This is the first
piece of user signal in the project's history that isn't your own team's
interviews or your own testing.

## What "done" means here

Not a PR. A sentence you can say out loud and mean: "I said 'Hey ECHO'
into my laptop, and it worked" — or, just as valuably, an honest account
of exactly what didn't, with real numbers and a real stranger's real
reaction attached. Either outcome is worth more right now than a
fourteenth phase of code.

# ECHO OS — Phase 14 Execution: Real Dependency Bring-Up

## What this prompt is and isn't

This is not a new code phase — it's the execution prompt for Phase 14
itself, meant to get as far as an agent with shell access on the real
laptop honestly can. Be explicit and honest at every step about what you
did versus what genuinely requires a human: you cannot speak "Hey ECHO"
into a physical microphone, and you cannot be the outside stranger in
Step 6. Do not simulate, fake, or narrate around those — say plainly
where the agent's part ends and the human's part begins.

Confirm the local checkout is on `master` at the PR #12 merge commit
before starting (it should already be, per the last sync).

## What to actually do

1. **Follow `docs/RUNBOOK.md` top to bottom** to install the real
   dependencies on this machine: OpenCV, SDL2/SDL2_ttf, whisper.cpp,
   llama.cpp, Piper, ONNX Runtime, and openWakeWord (account-free —
   matches what CI verifies; skip Porcupine unless a key already exists).
   Install via whatever package manager/build process the RUNBOOK
   specifies. Report exact installed versions as you go, the same way
   Phase 6 recorded them.
2. **Download the exact pinned model files from `MANIFEST.md`**, using
   `scripts/fetch_real_engine_assets.sh` (or its logic ported to
   PowerShell if that's the native shell here) so the models are byte-
   identical to what CI already verified — not different files found
   elsewhere with the same name.
3. **Run `./scripts/build_real.ps1`.** Report a clean build or the exact
   real error if one occurs — a failure here on the actual hardware is
   genuine signal CI's Ubuntu runners couldn't have caught, and should be
   diagnosed for real, not glossed over.
4. **Attempt a headless/scripted invocation first**, if `echo-demo`
   supports a text-mode or fixture-input path (Phase 3/4 mentioned a
   `--text` mode) — this is a legitimate thing an agent can verify:
   confirm the binary starts, loads all five real engines without
   crashing, and processes at least one scripted turn end to end on this
   specific machine. This is not the live mic test, and must not be
   reported as one — it's a sanity check that clears the way for it.
5. **Stop there and hand off explicitly.** Once the real build is
   confirmed working end-to-end in scripted/text mode on this machine,
   say clearly: "The build is real and working on this machine. The
   remaining steps — speaking 'Hey ECHO' into the physical mic, testing
   face recognition against real faces, running the latency script over
   20 real spoken turns, and finding a non-founder tester — are yours to
   do. I cannot do these." Do not attempt to fabricate or approximate
   them.

## If something breaks

Diagnose and fix real build/runtime issues encountered on this specific
machine the same way Phase 6 did — root-cause it, don't route around it,
and note the fix (or the open issue if unresolved) in `docs/STATE.md`'s
known-issues section.

## What NOT to do

- Do not write latency numbers into `docs/STATE.md` — those must come
  from the human's real spoken turns.
- Do not claim the wake-word/face-recognition/safe-mode flows were
  "tested" based on the scripted/text-mode check alone — that check
  verifies the build runs, not that the live experience works.
- Do not simulate a non-founder tester's reaction.

## Final step, once the human's live run is also done

Only after the human reports back the real spoken-turn results, latency
numbers, and outside-tester notes:

```bash
git add -A
git commit -m "Phase 14: real dependency bring-up and live validation results"
git push
```

# ECHO OS — Phase 14b: Close the Windows Native-Dependency Gap

## Why this exists

The last run got genuinely far: stub build green (9/9), all 7 pinned
models staged and checksum-verified, and one real engine (LLM via
llama.cpp) built and run headlessly against a real model with honest,
non-fabricated output. But `./scripts/build_real.ps1` did not configure,
because three native libraries aren't installed on this machine yet:
OpenCV, SDL2/SDL2_ttf, and a Windows-native ONNX Runtime. `setup_deps.ps1`
currently only builds whisper.cpp/llama.cpp from source and silently
leaves these three to manual installation — that's the actual gap, and
it's agent-doable. This is not new feature work, so it isn't "Phase 15" —
it's finishing what Phase 14 couldn't reach last time.

The physically-embodied human steps (speaking into a real mic, real face
recognition, the 20-real-turn latency measurement, a non-founder tester)
are explicitly **out of scope for this prompt** — do not attempt, simulate,
or approximate any of them. This prompt ends the moment the real build
configures and runs cleanly in scripted/text mode.

## What to actually do

1. **Install OpenCV and SDL2/SDL2_ttf via vcpkg** (or the RUNBOOK's
   documented alternative if vcpkg isn't already set up):
   ```
   vcpkg install opencv4:x64-mingw-dynamic sdl2:x64-mingw-dynamic sdl2-ttf:x64-mingw-dynamic
   ```
   Adjust the triplet if the project's toolchain uses a different one —
   check `cmake/echo_ai.cmake` and `docs/RUNBOOK.md` for the expected
   triplet/toolchain-file convention before assuming. Report exact
   installed versions.

2. **Get a Windows-native ONNX Runtime build.** The CI-pinned ORT asset is
   a Linux build and won't link on MinGW. Locate (or build) a genuine
   Windows/MinGW-compatible ONNX Runtime release — check whether
   Microsoft's official releases ship a MinGW-compatible package, or
   whether the MSVC-built release can be linked against MinGW with the
   right import-library handling; if neither works cleanly, document the
   real reason honestly in `docs/STATE.md` rather than forcing a broken
   link. Do not silently swap in the Linux binary and hope.

3. **Get a Windows Piper build + the `amy` voice**, per the RUNBOOK,
   if not already resolved from a prior session.

4. **Re-point `build_real.ps1` / `cmake/echo_ai.cmake`** at these newly
   installed packages (vcpkg toolchain file, ORT include/lib paths) so
   `find_package`/manual path detection actually locates them. Fix real
   CMake wiring bugs if the existing detection logic assumed a Linux/Unix
   layout that doesn't match vcpkg's Windows layout — that would be a
   real, previously-latent bug, not a new feature.

5. **Run `./scripts/build_real.ps1` again.** Report the exact outcome —
   clean build, or the exact real error. If it still fails, root-cause
   it the way Phase 6 did; don't route around it or quietly fall back to
   a partial build without saying so plainly.

6. **If it builds: run the scripted/text-mode sanity check** (`echo-demo
   --text` or equivalent) to confirm all five real engines load and
   process at least one scripted turn end-to-end without crashing. This
   is a build/load sanity check, not a live-hardware test — do not
   describe it as validating wake-word, face recognition, or voice
   quality.

7. **Update `docs/STATE.md`'s "Phase 14 execution findings" section**
   with the real outcome — either "the full real-engine build now
   configures and runs in scripted mode on this machine, dependency gap
   closed" with exact versions, or an honest, specific account of what
   still blocks it and why.

## What NOT to do

- Do not install or link a Linux-built ONNX Runtime binary and claim it
  works — verify it actually links and runs on this machine.
- Do not touch latency numbers, wake-word/face-recognition validity, or
  tester reactions — those remain exclusively the human's to produce.
- Do not commit/push. The Phase 14 commit is still gated on the human's
  real spoken-turn results, exactly as before. If this prompt's changes
  are worth preserving on their own (e.g., real CMake wiring fixes), stage
  them but leave the commit decision to the user, noting clearly what's
  staged and why it wasn't pushed.

## Handoff, once this is done

Whether the build closes cleanly or not, end with the same honest
reminder as before: speaking "Hey ECHO" into the physical mic, real face
recognition testing, the 20-real-turn latency run, and a non-founder
tester's reaction are still exclusively the user's to do, and nothing in
this prompt substitutes for them.

# ECHO OS — Phase 15: The Memory & Recall Engine (the actual dementia-care core)

## Why this phase, and why now

Phase 14/14b closed the last dependency gap: every real engine (wake-word,
ASR, vision, LLM, TTS) now builds, links, and runs end-to-end on both CI
and the actual dev machine. What remains open in Phase 14 — a human
speaking into a real mic, real face recognition, a real 20-turn latency
run, an outside tester — is permanently human-only and cannot be closed
by any prompt. That work stays exactly where it is, still gated on you.

Everything built in Phases 1-14 is real, working *generic voice-assistant
plumbing*: wake word, transcribe, route to an app (media/mail/search/
browser/video), synthesize a reply. None of it is actually about
dementia. The pitch, the founder story, and the entire reason this
product exists is "glasses that remember for you" — and there is
currently no module that remembers anything. `companion-sync` moves data
off-device; nothing on-device persists who a face belongs to, what was
said about them, or what the person needs reminding of. Phase 15 builds
that core: a local, private, on-device memory and recall engine. This is
new feature work, not gap-closing — the first phase since the scaffold
that adds a genuinely new capability rather than making an existing one
real.

Branch `feature/memory-recall` from `master` (confirm PR #13 is merged,
or at least not required as a dependency — this phase doesn't touch
`echo_ai.cmake`/`voice_ui.cpp`, so it can proceed in parallel with that
PR's review if needed).

## What to build

1. **A new module, `memory/`** (mirroring the existing module layout:
   `memory_engine.hpp/.cpp`, its own `CMakeLists.txt`, own unit tests),
   responsible for a local, encrypted-at-rest (or at minimum
   file-permission-restricted — be honest about which this phase
   actually achieves) on-device store of:
   - **Person records**: a stable identity key (linked to `vision`'s
     SFace embedding via a similarity match, not a raw image), a
     display name once the wearer has stated one ("this is my daughter
     Priya"), free-text notes accumulated over time, and last-seen
     timestamp.
   - **Reminder records**: short text + a due time or recurrence
     (e.g., "take blood pressure medication", daily 9am), with an
     acknowledged/not-acknowledged state.
   - **Routine/event log**: a lightweight append-only log of notable
     recognized events (a known person seen, a reminder fired and
     acknowledged or missed) — this is the raw material a caregiver or
     the wearer could review later, not a full transcript store.

2. **Storage format**: pick something genuinely appropriate for an
   embedded on-device store — SQLite (via a small vendored/linked
   library) is the standard honest choice here over a hand-rolled flat
   file, given concurrent read/write from perception + cognitive-core +
   a future companion-sync export; if you choose something else, justify
   it in `docs/DECISIONS.md` the way prior ADRs have. Do not build a
   toy in-memory-only store and call it done — persistence across
   restarts is the entire point.

3. **Wire `memory_engine` into the existing pipeline, not around it**:
   - `perception`'s face recognition result (an SFace embedding + match
     confidence) is looked up against stored person records; on a
     confident match, `cognitive_core` gets an enriched context (name,
     last-seen, notes) instead of a bare "face detected" signal — this
     is the real behavior change "who is this" should produce now:
     not just a name, but "That's Priya, your daughter — she visited
     you two days ago."
   - Reminders are checked on a timer/tick already present in the
     boot/scheduler loop (find and reuse it — don't add a second
     competing timer loop) and delivered through the existing
     `voice_ui` speak path, same as any other response.
   - A new route-tag (extend Phase 6's hardened route-tag parsing,
     don't fork it) lets the LLM ask the memory engine a question
     (e.g., "did I take my medication today") and get a real answer
     back, not a hallucinated one — this is a genuine retrieval-
     augmented step, and it matters that the LLM is answering from the
     memory engine's real record, not making it up.

4. **Safety and privacy constraints, made concrete, not aspirational**:
   - Face embeddings and notes never leave the device via
     `companion-sync`'s existing structural guarantee (Phase 10 proved
     via `static_assert` that its interface can't accept a
     `SensorFrame`; extend that same proof, or an equivalent one, to
     show the memory store's raw content can't reach the sync path
     either — don't just assert this in a comment, prove it the way
     Phase 10 did).
   - A person record is only created when the wearer explicitly names
     someone ("this is my daughter Priya") — never auto-created from an
     unrecognized face with a guessed name. Write a test that an
     unrecognized face with no naming utterance produces zero new
     person records.
   - Every reminder-delivery and person-recognition event you log is
     something a caregiver could reasonably see; do not log verbatim
     transcripts of unrelated conversation into this store — scope what
     gets persisted narrowly and say so in `docs/ARCHITECTURE.md`.

5. **Tests**: `tests/memory_engine_test.cpp` (unit-level: create/query/
   update person and reminder records, recurrence logic, the
   no-auto-create-from-unnamed-face rule) plus an extension to the
   existing `e2e_pipeline_test.cpp`-style fixture test showing a
   fixture face embedding + a prior "this is Priya" utterance producing
   an enriched recognition response end to end, without any real
   hardware.

6. **Honest `STATE.md`/`ARCHITECTURE.md`/`DECISIONS.md` updates**: this
   is the first module whose entire purpose is the product's actual
   differentiator, so document plainly what's now true ("ECHO can
   remember a person it's told about, and recall them later") and what
   still isn't (multi-day real-world memory accuracy, how notes get
   summarized/pruned over time so the store doesn't grow unbounded
   forever, and — same as ever — that none of this has been tried by a
   real wearer yet).

## Constraints

1. Stub build (no real engines) must still compile and pass — the
   memory engine's tests should run against fixture face-embedding data,
   not require the real OpenCV/ORT stack, consistent with how every
   other module keeps a dependency-free test path.
2. No new "phone home" path — this module must not create any new
   network call; if a future phase wants a caregiver-facing companion
   view of reminders/notes, that stays a boundary discussion for
   `companion-sync`, not something this phase quietly opens.
3. Reuse Phase 6's route-tag parsing and Phase 10's boot/scheduler timer
   rather than inventing parallel mechanisms.
4. Be honest if SQLite (or whatever you choose) doesn't build cleanly
   under MinGW the way OpenCV/whisper did in Phase 14b — document any
   real toolchain friction rather than silently switching approaches
   without saying why.

## Immediate deliverable for this session

1. `memory/` module: person records, reminder records, event log,
   backed by real on-device persistence (not in-memory-only).
2. Real wiring into `perception` → `cognitive_core` → `voice_ui` for
   both face-based recall and a memory-query route-tag.
3. The privacy proof extending companion-sync's structural guarantee to
   the memory store.
4. `tests/memory_engine_test.cpp` green in the stub build; an extended
   e2e fixture test showing enriched recognition end to end.
5. `docs/STATE.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md` updated
   honestly.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 15: on-device memory and recall engine (person records, reminders, privacy-proven local storage)"
git push -u origin feature/memory-recall
gh pr create --base master --head feature/memory-recall \
  --title "Phase 15: memory & recall engine" \
  --body-file pr_body.md
```

# ECHO OS — Phase 15: The Memory & Recall Engine (the actual dementia-care core)

## Why this phase, and why now

Phase 14/14b closed the last dependency gap: every real engine (wake-word,
ASR, vision, LLM, TTS) now builds, links, and runs end-to-end on both CI
and the actual dev machine. What remains open in Phase 14 — a human
speaking into a real mic, real face recognition, a real 20-turn latency
run, an outside tester — is permanently human-only and cannot be closed
by any prompt. That work stays exactly where it is, still gated on you.

Everything built in Phases 1-14 is real, working *generic voice-assistant
plumbing*: wake word, transcribe, route to an app (media/mail/search/
browser/video), synthesize a reply. None of it is actually about
dementia. The pitch, the founder story, and the entire reason this
product exists is "glasses that remember for you" — and there is
currently no module that remembers anything. `companion-sync` moves data
off-device; nothing on-device persists who a face belongs to, what was
said about them, or what the person needs reminding of. Phase 15 builds
that core: a local, private, on-device memory and recall engine. This is
new feature work, not gap-closing — the first phase since the scaffold
that adds a genuinely new capability rather than making an existing one
real.

Branch `feature/memory-recall` from `master` (confirm PR #13 is merged,
or at least not required as a dependency — this phase doesn't touch
`echo_ai.cmake`/`voice_ui.cpp`, so it can proceed in parallel with that
PR's review if needed).

## What to build

1. **A new module, `memory/`** (mirroring the existing module layout:
   `memory_engine.hpp/.cpp`, its own `CMakeLists.txt`, own unit tests),
   responsible for a local, encrypted-at-rest (or at minimum
   file-permission-restricted — be honest about which this phase
   actually achieves) on-device store of:
   - **Person records**: a stable identity key (linked to `vision`'s
     SFace embedding via a similarity match, not a raw image), a
     display name once the wearer has stated one ("this is my daughter
     Priya"), free-text notes accumulated over time, and last-seen
     timestamp.
   - **Reminder records**: short text + a due time or recurrence
     (e.g., "take blood pressure medication", daily 9am), with an
     acknowledged/not-acknowledged state.
   - **Routine/event log**: a lightweight append-only log of notable
     recognized events (a known person seen, a reminder fired and
     acknowledged or missed) — this is the raw material a caregiver or
     the wearer could review later, not a full transcript store.

2. **Storage format**: pick something genuinely appropriate for an
   embedded on-device store — SQLite (via a small vendored/linked
   library) is the standard honest choice here over a hand-rolled flat
   file, given concurrent read/write from perception + cognitive-core +
   a future companion-sync export; if you choose something else, justify
   it in `docs/DECISIONS.md` the way prior ADRs have. Do not build a
   toy in-memory-only store and call it done — persistence across
   restarts is the entire point.

3. **Wire `memory_engine` into the existing pipeline, not around it**:
   - `perception`'s face recognition result (an SFace embedding + match
     confidence) is looked up against stored person records; on a
     confident match, `cognitive_core` gets an enriched context (name,
     last-seen, notes) instead of a bare "face detected" signal — this
     is the real behavior change "who is this" should produce now:
     not just a name, but "That's Priya, your daughter — she visited
     you two days ago."
   - Reminders are checked on a timer/tick already present in the
     boot/scheduler loop (find and reuse it — don't add a second
     competing timer loop) and delivered through the existing
     `voice_ui` speak path, same as any other response.
   - A new route-tag (extend Phase 6's hardened route-tag parsing,
     don't fork it) lets the LLM ask the memory engine a question
     (e.g., "did I take my medication today") and get a real answer
     back, not a hallucinated one — this is a genuine retrieval-
     augmented step, and it matters that the LLM is answering from the
     memory engine's real record, not making it up.

4. **Safety and privacy constraints, made concrete, not aspirational**:
   - Face embeddings and notes never leave the device via
     `companion-sync`'s existing structural guarantee (Phase 10 proved
     via `static_assert` that its interface can't accept a
     `SensorFrame`; extend that same proof, or an equivalent one, to
     show the memory store's raw content can't reach the sync path
     either — don't just assert this in a comment, prove it the way
     Phase 10 did).
   - A person record is only created when the wearer explicitly names
     someone ("this is my daughter Priya") — never auto-created from an
     unrecognized face with a guessed name. Write a test that an
     unrecognized face with no naming utterance produces zero new
     person records.
   - Every reminder-delivery and person-recognition event you log is
     something a caregiver could reasonably see; do not log verbatim
     transcripts of unrelated conversation into this store — scope what
     gets persisted narrowly and say so in `docs/ARCHITECTURE.md`.

5. **Tests**: `tests/memory_engine_test.cpp` (unit-level: create/query/
   update person and reminder records, recurrence logic, the
   no-auto-create-from-unnamed-face rule) plus an extension to the
   existing `e2e_pipeline_test.cpp`-style fixture test showing a
   fixture face embedding + a prior "this is Priya" utterance producing
   an enriched recognition response end to end, without any real
   hardware.

6. **Honest `STATE.md`/`ARCHITECTURE.md`/`DECISIONS.md` updates**: this
   is the first module whose entire purpose is the product's actual
   differentiator, so document plainly what's now true ("ECHO can
   remember a person it's told about, and recall them later") and what
   still isn't (multi-day real-world memory accuracy, how notes get
   summarized/pruned over time so the store doesn't grow unbounded
   forever, and — same as ever — that none of this has been tried by a
   real wearer yet).

## Constraints

1. Stub build (no real engines) must still compile and pass — the
   memory engine's tests should run against fixture face-embedding data,
   not require the real OpenCV/ORT stack, consistent with how every
   other module keeps a dependency-free test path.
2. No new "phone home" path — this module must not create any new
   network call; if a future phase wants a caregiver-facing companion
   view of reminders/notes, that stays a boundary discussion for
   `companion-sync`, not something this phase quietly opens.
3. Reuse Phase 6's route-tag parsing and Phase 10's boot/scheduler timer
   rather than inventing parallel mechanisms.
4. Be honest if SQLite (or whatever you choose) doesn't build cleanly
   under MinGW the way OpenCV/whisper did in Phase 14b — document any
   real toolchain friction rather than silently switching approaches
   without saying why.

## Immediate deliverable for this session

1. `memory/` module: person records, reminder records, event log,
   backed by real on-device persistence (not in-memory-only).
2. Real wiring into `perception` → `cognitive_core` → `voice_ui` for
   both face-based recall and a memory-query route-tag.
3. The privacy proof extending companion-sync's structural guarantee to
   the memory store.
4. `tests/memory_engine_test.cpp` green in the stub build; an extended
   e2e fixture test showing enriched recognition end to end.
5. `docs/STATE.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md` updated
   honestly.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 15: on-device memory and recall engine (person records, reminders, privacy-proven local storage)"
git push -u origin feature/memory-recall
gh pr create --base master --head feature/memory-recall \
  --title "Phase 15: memory & recall engine" \
  --body-file pr_body.md
```

# ECHO OS — Phase 16: Hardening the Memory Store (Encryption at Rest + Retention)

## Why this phase

Phase 15 shipped a real, working memory engine (PR #14, merged, all CI
green) — but its own honest `STATE.md` entry flags two gaps it deliberately
left open rather than glossing over: the on-device SQLite store is
protected by file permissions, not encryption at rest, and there is no
policy yet for pruning or summarizing notes/events so the store doesn't
grow unbounded forever. Both are real, scoped, and closeable without any
human-only step — this phase closes them rather than letting them sit
as a permanent asterisk on a module that stores a wearer's face-linked
identity data.

Branch `feature/memory-hardening` from `master` (confirm PR #14 is merged
first).

## Part 1 — Encryption at rest

1. **Adopt SQLCipher** (or document honestly why not, if it genuinely
   doesn't build under this MinGW toolchain the way plain SQLite did in
   Phase 15 — test this for real before committing to the approach,
   exactly the way Phase 15 smoke-compiled the amalgamation first and
   Phase 14b smoke-tested ORT before committing hours to it). SQLCipher
   is a drop-in SQLite replacement with page-level AES-256 encryption,
   so the existing `memory_engine.cpp` SQL logic should not need to
   change — only the connection-open path.
2. **Key management, stated honestly**: this is a wearable with no
   keyboard and, today, no companion-device pairing flow — so a
   passphrase prompt isn't realistic yet. Use a per-device key derived
   and stored via whatever this platform's least-bad local option is
   (e.g., a key file written once at first boot with restrictive
   permissions, analogous to how `.echo-tokens/` is already handled in
   `appkit`). Do not oversell this as "hardware-backed" or "secure
   enclave"-grade if the actual implementation is a protected local key
   file — say plainly what security property this does and doesn't
   provide, the same honest framing Phase 15 used for its own
   file-permission-only baseline.
3. **Migration**: if an existing unencrypted `memory.db` is found on
   disk (from a Phase 15 install), write a one-time migration path that
   opens the old file and re-writes it into a new encrypted store,
   logged clearly, rather than silently discarding a wearer's existing
   person/reminder records.
4. **Tests**: extend `memory_engine_test.cpp` to prove the on-disk file
   is not readable as plaintext SQLite without the key (e.g., attempt to
   open the raw file with a plain, unkeyed connection and assert it
   fails/reads garbage rather than valid tables), plus a migration test
   using a fixture unencrypted DB checked into `tests/fixtures/`.

## Part 2 — Retention and pruning

1. **A bounded retention policy** for the event log (Phase 15's
   append-only recognized-event/reminder-fired log) — this is the part
   most likely to grow without limit. Add a configurable cap (by count
   and/or age, e.g., keep the last N days or last N events) enforced on
   a natural existing tick (reuse the same scheduler tick Phase 15 used
   for reminders — don't add a third timer).
2. **Person notes are handled differently from the event log** — notes
   are meaningfully valuable long-term (the whole point is remembering
   people over time), so this phase should not prune them by age.
   Instead, add a simple, honest first pass at bounding growth: a
   per-person note-count or note-length cap with oldest-note eviction,
   or (if time allows and is genuinely tractable) a summarization pass
   using the existing local LLM to periodically compress older notes
   into a shorter running summary. If summarization proves too fragile
   given Phase 15's already-documented tiny-model prompt fragility,
   document that honestly and ship the simpler cap-and-evict approach
   instead — don't force a fragile LLM-dependent feature into this phase
   if it doesn't hold up under real testing the way the Phase 15
   route-tag prompt had to be iterated on against the real model.
3. **Tests**: a test proving the event log is actually bounded after
   exceeding the cap (oldest entries evicted, newest retained, reminder
   acknowledgment state for retained entries intact), and a test for
   whichever notes-bounding approach is chosen.

## Constraints

1. Do not weaken the Phase 15 privacy proof — the `static_assert`
   guarantee that memory data can't reach `companion-sync` must still
   hold and still be shown to bite (the deliberately-broken-assertion
   check Phase 15 used to prove it).
2. Do not regress the person-record creation rule (only created on an
   explicit naming utterance) or the reminder-delivery path.
3. Stub build (no real engines) must stay green and dependency-free —
   SQLCipher, like plain SQLite in Phase 15, should be vendorable and
   buildable without any real-engine flag.
4. If SQLCipher genuinely doesn't build cleanly on this toolchain,
   don't force it — document the real blocker in `STATE.md` and
   implement encryption via a documented fallback (e.g., an
   application-layer AES pass over the SQLite file, or a clearly-scoped
   "encryption deferred, gap still open" entry) rather than silently
   dropping the requirement.

## Immediate deliverable for this session

1. Encrypted-at-rest memory store (or an honest, specific account of why
   not, with a real fallback or clearly documented open gap).
2. A working, tested key-management approach, described accurately.
3. A bounded event log with a real eviction policy, tested.
4. A bounded notes-growth approach (cap-and-evict, or summarization if it
   genuinely proves out — document which), tested.
5. `docs/STATE.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md` updated
   with the real outcome of both parts.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 16: encrypt memory store at rest and bound its long-term growth"
git push -u origin feature/memory-hardening
gh pr create --base master --head feature/memory-hardening \
  --title "Phase 16: memory store hardening (encryption + retention)" \
  --body-file pr_body.md
```

# ECHO OS — Phase 17: Fault Tolerance & Graceful Degradation

## Why this phase

Sixteen phases in, the honest gap list has narrowed to almost nothing
that's actually closeable by code — live human validation is the last
open item, and it's permanent by nature. But there's a real gap that
isn't about adding a new capability or closing a CI blind spot: nothing
in ECHO OS has ever been asked what happens when it breaks. This matters
more here than in most software, because the wearer is, by design, a
person who may not notice or be able to correct a malfunctioning device
on their own face. If `perception` throws on a malformed camera frame,
does the whole process die mid-sentence? If the memory store's disk
write fails because storage is full, does a reminder silently vanish, or
does the process abort? If the LLM engine hangs, does the wearer get
stuck listening to nothing, forever, with no fallback? None of this has
been tested, because nothing has ever been asked to fail.

Branch `feature/fault-tolerance` from `master` (confirm PR #15 is merged
first).

## What to build

1. **A supervisor layer around each engine boundary** (wake-word, ASR,
   vision, LLM, TTS, memory) that catches and contains failures at the
   call site rather than letting an exception or crash propagate out of
   `runtime`'s main loop. Prefer to build on whatever error-handling
   pattern already exists (`common/result.hpp`'s `Status`/`Result<T>`)
   rather than inventing a second one — extend it if it doesn't already
   cover "this engine call failed, but the process should keep running."
2. **A defined degraded-mode behavior per engine**, decided deliberately,
   not accidentally:
   - Vision failure → fall back to voice-only interaction; don't crash
     the turn, and don't silently pretend a face was recognized.
   - ASR failure → a short, calm spoken fallback ("I didn't catch that,
     can you say it again?") rather than silence or a crash.
   - LLM failure/hang → this already has a safe-mode gate (Phase 8-12)
     for *low-confidence* output; this phase adds the missing case of
     the engine *not responding at all* (timeout), which is a different
     failure mode from "responded but uncertain" and isn't currently
     covered — add a bounded timeout with a safe fallback utterance.
   - Memory store write failure (disk full, corruption, permission
     loss) → the wearer should still get their reminder/recall for the
     current session even if persistence fails; log the failure
     honestly rather than silently dropping it, and don't crash the
     process over a failed write.
   - Any engine failure should never crash `runtime`'s process outright
     — the wearer relies on this device working through the rest of the
     day even if one subsystem degrades.
3. **A watchdog/heartbeat** in the boot/scheduler loop that detects a
   genuinely hung engine call (not just an exception, but something that
   never returns) and can recover the pipeline without a full device
   reboot if possible, falling back to a real reboot path if not —
   describe honestly which failure classes this phase's watchdog can
   actually recover from versus which still require a physical restart.
4. **Fault injection tests** — this is the part that makes the claim
   real rather than aspirational. Add a test harness that deliberately
   makes each engine boundary fail (throw, return an error `Status`,
   hang past the timeout) and asserts: the process doesn't crash, the
   defined degraded behavior actually happens, and the pipeline recovers
   on the next turn once the fault clears. This should run in the stub
   build (inject faults into the stub engines) so it's fast and
   dependency-free, the same way every other module keeps a
   no-real-engines test path.
5. **Honest `STATE.md`/`ARCHITECTURE.md` update**: document exactly
   which failure classes are now handled gracefully, which still bring
   the process down, and which require a physical reboot — this is a
   safety-relevant claim, so it should be as precise and unexaggerated
   as the memory-encryption scope note in Phase 16's ADR-14.

## Constraints

1. Do not weaken or bypass the existing safe-mode confidence gate — this
   phase adds handling for *non-responding*/*crashing* engines, which is
   a different failure mode from *responding with low confidence*; both
   should coexist correctly, not be conflated into one path.
2. Do not let a degraded-mode fallback ever fabricate content (e.g., a
   vision failure must not report a face match that didn't happen) —
   this is the same "don't guess" principle that shaped the safe-mode
   gate and the memory engine's naming-gated record creation.
3. Keep the stub build dependency-free and fast; fault injection must
   not require real engines to exercise.
4. Reuse the existing boot/scheduler loop for the watchdog tick — don't
   add a fourth independent timer (Phase 15 and 16 both reused the same
   one; keep that discipline).

## Immediate deliverable for this session

1. A supervisor/containment layer around every engine call site.
2. Defined, tested degraded-mode behavior for each engine's failure.
3. A watchdog for hung (non-exception, non-returning) calls, with honest
   documentation of its actual recovery scope.
4. `tests/fault_injection_test.cpp` (or integrated into existing suites),
   green in the stub build.
5. `docs/STATE.md` and `docs/ARCHITECTURE.md` updated with a precise,
   unexaggerated account of what's now fault-tolerant and what isn't.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 17: fault containment, graceful degradation, and a watchdog for hung engines"
git push -u origin feature/fault-tolerance
gh pr create --base master --head feature/fault-tolerance \
  --title "Phase 17: fault tolerance and graceful degradation" \
  --body-file pr_body.md
```

# ECHO OS — Phase 18: Power & Thermal Management (the module that's existed in name only since Phase 1)

## Why this phase

`power-mgmt` has been a top-level module directory since the very first
scaffold, alongside boot, sensor-pipeline, perception, cognitive-core,
voice-ui, and companion-sync — but across seventeen phases it has never
been given real logic. Every other module on that original list has since
been made real, tested, hardened (Phase 17 wrapped every engine boundary
in fault containment), or extended (Phase 15-16's memory work). Power and
thermal behavior is the one dormant item left from the original scaffold,
and it matters more here than in almost any other kind of device: this is
something meant to be worn all day, on a wearer who may not reliably
notice a dead battery, an uncomfortably hot temple, or a device that's
silently throttling itself into uselessness partway through the
afternoon. Phase 17 made ECHO OS resilient to software failures; Phase 18
makes it resilient to the physical reality of running on limited power
and generating real heat.

Branch `feature/power-thermal` from `master` (confirm PR #16 is merged
first).

## What to build

1. **A real `power-mgmt` interface** exposing battery level (0-100%) and
   a thermal signal (a simple state: nominal/warm/hot, or a temperature
   reading if the target hardware would realistically expose one —
   document which you chose and why) behind an `IPowerSource` /
   `IThermalSource`-style boundary, mirroring how `IHttpClient` and the
   engine interfaces were abstracted in earlier phases. Provide a real
   backend hook (however this laptop or a target embedded platform would
   realistically read this — be honest if there's no real sensor to read
   on a dev laptop and the "real" backend is necessarily a documented stub
   for now) and a fully deterministic fake for tests, the same dual-path
   discipline every other engine has followed since Phase 3.
2. **A duty-cycling policy for `perception`**: continuous camera-based
   face detection is expensive; add a policy that reduces vision-sampling
   frequency as battery drops past defined thresholds (e.g., full-rate
   above 40%, reduced-rate 15-40%, vision-off-voice-only below a critical
   floor), with the same "never fabricate a result to hide degraded
   sampling" principle Phase 17 established for engine failures — a
   lower sampling rate means slower/less frequent recognition, not a
   guessed one.
3. **A thermal-aware LLM/perception throttle**: when the thermal signal
   reports warm/hot, reduce inference load (e.g., shorter context,
   longer allowed latency before the Phase 17 timeout fires, or a
   documented reduced-capability mode) rather than letting the device
   silently overheat or crash. Be honest about what this phase can
   actually verify without real hardware thermal data — the policy logic
   is testable, but real thermal behavior on real skin-adjacent hardware
   is not something this phase can claim to have proven.
4. **Wire this into the Phase 17 supervisor/degraded-mode vocabulary**
   rather than inventing a parallel one** — low battery and high thermal
   states should surface through the same `EngineDegraded`-style alert
   path already built, not a second notification mechanism.
5. **A low-battery reminder-priority policy**: if the device is about to
   shut down, critical reminders (e.g., medication) should get a final
   best-effort delivery before power loss, if the platform can predict
   shutdown at all — document plainly if this is speculative given no
   real battery hardware exists to test against yet.
6. **Tests**: `tests/power_mgmt_test.cpp` driving the fake power/thermal
   sources through defined threshold transitions, asserting the correct
   duty-cycle/throttle decisions at each level, and that no fabricated
   perception result is ever produced during reduced sampling. Extend
   the fault-injection-style DI seam from Phase 17 if it fits naturally.
7. **Honest docs**: `STATE.md` should state plainly that policy logic is
   verified in the stub build against simulated battery/thermal input,
   and that real device power/thermal behavior remains unvalidated
   until real hardware exists — this is a new, permanent-until-hardware
   gap, not a temporary one closeable by more code, and should be logged
   with the same honesty as the live-mic gap has been since Phase 9.

## Constraints

1. Do not fabricate a recognition result to compensate for reduced
   sampling — same non-negotiable principle as Phase 15's naming-gated
   memory and Phase 17's degraded-mode rules.
2. Reuse Phase 17's alert/degradation vocabulary; don't build a second,
   parallel "device health" notification system.
3. Keep the stub build dependency-free — this entire module should be
   testable with the fake power/thermal sources, no real hardware sensor
   required.
4. Be explicit in `STATE.md` that this phase closes a *design and policy*
   gap, not a *real hardware validation* gap — conflating the two would
   overstate what's actually been proven, the same mistake the project
   has consistently avoided since Phase 9.

## Immediate deliverable for this session

1. `power-mgmt` module: real `IPowerSource`/`IThermalSource` interfaces,
   a documented real-backend hook, and a deterministic fake for tests.
2. Battery-based perception duty-cycling policy, wired into the existing
   pipeline without fabricating degraded results.
3. Thermal-aware throttle for LLM/perception load, integrated with
   Phase 17's timeout/degradation path.
4. Low-battery critical-reminder best-effort delivery policy (or an
   honest note on why it's out of reach without real hardware).
5. `tests/power_mgmt_test.cpp`, green in the stub build.
6. `docs/STATE.md`/`docs/ARCHITECTURE.md`/`docs/DECISIONS.md` updated
   with a precise, unexaggerated account of what's policy-verified versus
   still hardware-unvalidated.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 18: power and thermal management (duty-cycling, throttling, low-battery reminder priority)"
git push -u origin feature/power-thermal
gh pr create --base master --head feature/power-thermal \
  --title "Phase 18: power and thermal management" \
  --body-file pr_body.md
```

# ECHO OS — Phase 19: Audio Robustness (Noise, Echo, and Real-Room Conditions)

## Why this phase, and why now specifically

Every ASR and wake-word test written since Phase 10 uses clean,
synthetically-generated fixture audio — Piper-synthesized speech, clean
silence, clean garble. That was the right call for proving the engines
work at all, but it has never once been asked to handle what a real room
actually sounds like: background noise, a TV or another conversation in
the background, the wearer's own voice reflecting off a table (echo),
or a phrase spoken while the wearer is moving. This is very likely the
single biggest reason the still-pending live human test (Phase 9's
oldest open item) could disappoint — not because the engines don't work,
but because they've only ever been asked to work in ideal conditions.
Doing this now, before that live test, is a deliberate act of derisking
it with real, CI-provable engineering rather than finding out the hard
way.

Branch `feature/audio-robustness` from `master` (confirm PR #17 is
merged first).

## What to build

1. **Noise-augmented fixture generation** — extend Phase 10/13's fixture
   pipeline (`make_fixtures.py` or equivalent) to mix a handful of
   representative noise beds (e.g., steady background hum, brief
   transient noise like a door or dish clatter, a second overlapping
   voice) into the existing clean wake-word and speech fixtures at
   several defined SNR levels (e.g., clean, +10dB, 0dB, -5dB). Keep the
   noise sources either synthetically generated or from a clearly
   license-compatible source, checksummed and documented in
   `MANIFEST.md` exactly like every other asset.
2. **Measure real degradation, honestly** — run the real whisper.cpp and
   real openWakeWord engines (Phase 11/13's real-engine CI path) against
   every noise/SNR combination and record actual detection/transcription
   accuracy at each level, not an assumed or hoped-for number. This is
   the crucial difference from every prior phase's binary pass/fail
   fixture tests: this phase's deliverable is partly a measurement, and
   the measurement itself — even if the news is that accuracy falls off
   badly at -5dB — is the honest output the project has valued
   throughout.
3. **A basic pre-processing stage** (before ASR/wake-word, in the
   existing `sensor-pipeline` capture path) — a simple, real noise
   reduction or automatic gain control pass, evaluated against the same
   noisy fixtures to show whether it measurably helps. Do not add a
   complex beamforming/multi-mic algorithm if this is a single-mic
   pipeline (confirm this against `sensor-pipeline`'s actual capture
   code rather than assuming); if the target hardware's real mic array
   configuration is unknown or undecided, say so plainly rather than
   building for an assumed configuration.
4. **A defined "confidence under noise" signal** feeding into the
   existing safe-mode gate and Phase 17's degradation vocabulary — if
   the engines can expose a real confidence/SNR estimate rather than
   just a hard yes/no, low-confidence-due-to-noise should route through
   the same "ask again calmly" fallback Phase 17 built for ASR failure,
   not a new mechanism.
5. **Tests**: `tests/audio_robustness_test.cpp` (or extending
   `real_asr_test.cpp`/`real_wakeword_test.cpp`) asserting the real
   engines behave reasonably at each defined noise level — reasonably
   meaning "doesn't crash, doesn't hallucinate confident wrong output,
   degrades in a defined and measured way" — not "achieves perfect
   accuracy," which would be dishonest to assert for any real ASR/wake-
   word system under noise.
6. **Honest `STATE.md` update** — record the actual measured
   accuracy-vs-SNR numbers (a real table, not a summary claim), state
   plainly that this validates against synthetic noise mixed into clean
   fixtures, not a real physical room's actual acoustics (echo off real
   walls, a real mic's real frequency response), and that the true test
   is still the live human run — this phase narrows that risk, it
   doesn't eliminate the need for it.

## Constraints

1. Do not tune the noise/SNR test thresholds to make results look better
   than they are — if -5dB genuinely breaks wake-word detection, that's
   the honest finding to report, the same way Phase 13 reported real
   false-accept/false-reject rates instead of hiding them.
2. Keep the stub build dependency-free; this entire phase's tests belong
   in the real-engines CI path (Phase 11/13's job), not the fast stub
   path.
3. Reuse the Phase 17 degradation vocabulary for noise-driven low
   confidence; don't add a parallel notification mechanism.
4. Verify the actual mic configuration assumption (single-mic vs. array)
   against real code before designing pre-processing around it — don't
   guess.

## Immediate deliverable for this session

1. Noise-augmented fixtures at defined SNR levels, checksummed and
   documented in `MANIFEST.md`.
2. Real measured accuracy/detection numbers across those levels, for
   both ASR and wake-word.
3. A basic, measured noise-reduction/AGC pre-processing stage (or an
   honest account of why it doesn't help / isn't appropriate for the
   real capture configuration).
4. A noise-driven confidence signal wired into the existing safe-mode/
   degradation paths.
5. Real-engine tests green in CI, with honestly-set (not padded)
   pass thresholds.
6. `docs/STATE.md` updated with the real measured table and a clear
   statement of what this does and doesn't derisk ahead of the live
   human test.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 19: audio robustness under noise (measured degradation, basic pre-processing, noise-aware confidence)"
git push -u origin feature/audio-robustness
gh pr create --base master --head feature/audio-robustness \
  --title "Phase 19: audio robustness under real-room noise conditions" \
  --body-file pr_body.md
```

# ECHO OS — Phase 20: Longevity & Resource-Leak Soak Testing

## Why this phase

Every test written across nineteen phases exercises a single turn, a
single fault, or a bounded scenario — realistic for proving correctness,
but none of them prove the one thing this specific product actually
needs most: running continuously, for many hours, without degrading.
ECHO is meant to be worn all day, every day, by someone who won't notice
if memory usage creeps upward, if the event log's retention pruning
(Phase 16) only gets exercised once in a unit test rather than across
thousands of real ticks, if the Phase 17 watchdog's recovery path leaves
a small resource leak every time it fires, or if reminder scheduling
(Phase 15) drifts after enough real-time ticks. None of this has ever
been run for longer than a single test's few seconds. Phase 20 builds
the thing that actually looks for this class of bug: a soak harness that
runs the real pipeline for a long, compressed-time duration and watches
for the failure modes that only show up over hours, not seconds.

Branch `feature/longevity-soak` from `master` (confirm PR #18 is merged
first).

## What to build

1. **A soak-mode driver** — reuse the DI seam from Phase 17/18's fake
   engines (`tests/fake_engines.hpp`) to drive the real `runtime` through
   a large number of simulated ticks (thousands, representing a
   compressed multi-hour or multi-day day-in-the-life) rather than real
   wall-clock time — this must be fast enough to run in CI, so simulated
   time, not `sleep()`-based real time, is the only honest way to do
   this at this scale.
2. **Instrument what "degrading" actually means** for this system,
   concretely:
   - Process memory (RSS) sampled across the run — assert it plateaus
     rather than growing unbounded (a real, if coarse, leak detector).
   - Open file/handle count for the memory engine's SQLite connection
     and any other resource the fault-tolerance/watchdog path touches —
     assert it doesn't grow with repeated recovery cycles.
   - Phase 16's event-log retention actually stays bounded after
     thousands of ticks, not just the single-cap unit test from that
     phase — this is the first time it's been exercised at realistic
     scale.
   - Reminder delivery timing doesn't drift over the simulated run
     (Phase 15's scheduler tick, exercised at scale for the first time).
   - Phase 17's watchdog, deliberately triggered repeatedly throughout
     the soak (inject faults periodically, not just once), recovers
     every time without accumulating leaked workers/threads — put a
     real number on this rather than asserting it vaguely.
3. **A defined pass/fail bar for each metric**, decided honestly rather
   than picked to make the test pass — if the true finding is "memory
   grows by X% over the simulated week and that's a real, currently-
   unaddressed leak," document that finding plainly in `STATE.md` rather
   than loosening the assertion until it's green. A found leak is a
   legitimate, valuable outcome of this phase; a test tuned to hide one
   is not.
4. **Root-cause and fix what's fixable within scope** — if the soak run
   surfaces a genuine, containable leak (e.g., a container that grows
   because an eviction path has an off-by-one, or a watchdog recovery
   path that doesn't release a resource), fix it the way Phase 6 fixed
   the JSON/email bugs it found — root-cause, not paper over. If a
   finding is real but out of scope to fix in this phase (e.g., it would
   require a larger architectural change), document it honestly as an
   open, known issue rather than silently deferring it with no record.
5. **CI integration**: add this as its own job or extend an existing one
   — it will likely run longer than the fast stub-build jobs, so scope
   it the way Phase 12's real-LLM job was scoped (a separate, clearly
   time-bounded job, not something that makes every PR's feedback loop
   slower).
6. **Honest `STATE.md` update**: record the actual simulated duration
   tested, the actual measured numbers for each metric, and — as always
   — the honest boundary of what this proves. Simulated-time soak
   testing with fake engines proves the orchestration logic doesn't leak
   or drift; it does not prove real engine libraries (whisper.cpp,
   llama.cpp, OpenCV, ONNX Runtime) are leak-free over real multi-day
   uptime, since those are third-party dependencies this phase's harness
   doesn't instrument. Say this plainly rather than implying the soak
   test covers more than it does.

## Constraints

1. This must run fast enough for CI — simulated/compressed time, not
   real-time sleeping. If achieving a meaningfully long simulated
   duration within a reasonable CI budget isn't possible, say so and
   scope down to the largest honestly-achievable simulated run rather
   than quietly claiming a shorter one represents "a day."
2. Do not tune pass thresholds to hide a real finding — a discovered
   leak or drift is a legitimate, reportable outcome of this phase.
3. Reuse the Phase 17/18 fake-engine and fault-injection infrastructure
   rather than building a third parallel test harness.
4. Be explicit about what this phase's soak testing does and doesn't
   cover — orchestration-layer longevity, not third-party engine library
   longevity, which remains genuinely unproven until real multi-day
   hardware use happens.

## Immediate deliverable for this session

1. A soak-mode test driving the real runtime through a large number of
   simulated ticks with periodic injected faults.
2. Real measured numbers for memory, handle/resource count, retention-
   log boundedness, reminder-timing drift, and watchdog-recovery
   resource stability.
3. Any genuine, in-scope leak found actually root-caused and fixed;
   anything out-of-scope documented honestly as a known open issue.
4. CI job integration, scoped for reasonable runtime.
5. `docs/STATE.md` updated with the real numbers and an honest statement
   of scope (orchestration longevity proven; engine-library longevity
   still not).

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 20: longevity soak testing (simulated multi-hour runs, leak/drift detection)"
git push -u origin feature/longevity-soak
gh pr create --base master --head feature/longevity-soak \
  --title "Phase 20: longevity and resource-leak soak testing" \
  --body-file pr_body.md
```

# ECHO OS — Phase 21: Answer-Side Safe Mode (gate what ECHO *says*, not just what it heard)

## Why this phase

The safe-mode gate has been scoring the wrong half of the problem for
twenty phases. `cognitive-core/src/cognitive_core.cpp` computes
`observation.aggregate_confidence()`, compares it to 0.72, and — if the
wearer spoke clearly — hands the utterance to the LLM and returns
whatever comes back verbatim as `ResponseKind::Normal` with
`flag_caregiver = false`. `LlmReply` carries `text` and `intent` and
nothing else: there is no confidence on the answer, because nothing has
ever looked at one. Phase 12 already found this and recorded it in
STATE.md — "a real model produces confident text for *every* prompt, so
the safe-mode guarantee here rests on the perception-confidence gate."
That sentence is accurate, and read plainly it says the gate defends
against a bad *transcript* and against nothing else.

ADR-4 exists because a confident wrong answer is more harmful than an
honest "I'm not sure" to someone who cannot check. Three cases reach the
wearer completely ungated today, all of them off a clean transcript: an
autobiographical question the store has no record of ("did my daughter
visit yesterday?"); a medication question where `answer_query()` returns
`""` and the code deliberately "keep[s] the LLM's own (calm,
non-committal) sentence"; and an ordinary factual question that a small
quantized model simply gets wrong. The first two are the dangerous ones,
and they are the two this phase can genuinely fix — not with a better
model, but by refusing to answer from nothing.

Branch `feature/answer-side-safe-mode` from `master` (confirm PR #19 is
merged first).

## What to build

1. **An `ILlm` injection seam, so the gate is testable at all** —
   `CognitiveCore`'s constructor calls `make_llm()` itself, so the
   confident path can only ever be exercised with a real model compiled
   in. That is precisely why this hole survived twenty phases of tests.
   Add an optional `std::unique_ptr<ILlm>` parameter to
   `make_cognitive_core()`, defaulting to `make_llm()` so every existing
   caller is unchanged — the same "guard the new branch, leave the old
   behaviour untouched" move Phase 15 used for `memory_ == nullptr`.
   Add `tests/fake_llm.hpp` beside `tests/fake_engines.hpp`: a scriptable
   `ILlm` returning canned text, intent, and confidence. Everything
   below then runs in the dependency-free stub build rather than only in
   the `real-llm` job.
2. **A real answer-side confidence, derived the way Phase 19 derived the
   ASR one** — add a `Confidence` to `LlmReply`. On the
   `ECHO_WITH_LLAMA` path compute it from the actual sampler: whisper's
   mean token probability already feeds `Transcript::confidence`, so do
   the LLM analogue with `llama_get_logits_ith(ctx_, -1)` + softmax over
   the sampled token, meaned across the generation loop in `llm.cpp`.
   Reuse the existing idea rather than inventing a second notion of
   confidence.
   **State the limit up front, not in a footnote:** a mean token
   probability is a *fluency* signal, not a *truth* signal. It catches
   degenerate drift and near-random continuation; it does **not** catch
   a fluent, confident falsehood, and no amount of threshold tuning will
   make it. That is exactly why item 3 carries the real weight and is
   not allowed to depend on this number.
3. **Grounding: a question about the wearer's own life is answered from
   the record or not at all.** This is the part that actually reduces
   harm, and it needs no probability estimate. Extend
   `memory/include/echo/memory/utterance.hpp` — already the home for
   exactly this kind of pure, SQLite-free string logic (`parse_naming`,
   `is_identity_query`) — with a deliberately conservative
   self-referential classifier: *did I…*, *when did I…*, *who visited…*,
   *where did I put…*, *have I taken…*. For any transcript it fires on:
   - route it to the memory engine **directly**, rather than waiting for
     the model to choose to emit `[route:memory]` (Phase 15 documented
     how fragile tiny-model prompt-following is — don't rest a safety
     property on it);
   - when `answer_query()` returns a real answer, speak it, exactly as
     Phase 15 does today;
   - when it returns `""`, **abstain**. Never fall through to the LLM's
     sentence. This is the concrete fix to the "keep the LLM's own
     sentence" branch, where the fallback is currently the wrong way
     round: the one case where the store *knows* it has nothing is the
     one case where a guess is least acceptable.
4. **A third decline path, kept distinct from the other two.** ECHO now
   declines for three different reasons, and collapsing them would repeat
   the mistake Phase 17 explicitly avoided when it kept "the engine
   didn't respond" separate from "the input was unclear". Add
   `ResponseKind::Unverified` (or an equivalent) alongside
   `Normal`/`SafeMode`:
   - *input unclear* → `SafeMode`, threshold 0.72, unchanged (ADR-4);
   - *engine silent or timed out* → the Phase-17 engine-fault line,
     unchanged;
   - *input clear, engine answered, answer not grounded* → the new path.
   Each gets its own known-good line and its own caregiver posture. For
   the repeated-abstention pattern reuse `AlertKind::LowConfidenceTrend`
   — a value that has sat unused in the enum since Phase 1 — rather than
   adding a new alert kind, the same discipline ADR-17 followed when it
   reused `EngineDegraded`.
5. **Tests, in the stub build and against the real model.**
   `tests/answer_gate_test.cpp` via the item-1 fake: a scripted
   low-confidence answer abstains; a confident one passes; a
   self-referential question *with* a matching record answers from the
   record; the same question with **no** record abstains and the LLM's
   text provably never reaches `voice-ui`; an ordinary non-self-referential
   question is unaffected; and the three decline paths assert to distinct
   lines and distinct alert postures. Then extend
   `tests/real_llm_test.cpp` in the `real-llm` job to record the tiny
   model's **actually observed** answer-confidence spread across several
   prompts — a Phase-19-style measurement table, printed, not a threshold
   tuned to look decisive.

## Constraints

1. Do not re-tune or weaken the existing 0.72 input gate. This phase adds
   a **second, independent** gate downstream of it; both must coexist and
   stay separately identifiable in the code and in the tests.
2. Never fabricate, and never soften. An abstention must plainly say it
   doesn't know. A hedge that a wearer could mistake for an answer is
   worse than the guess it replaced, because it launders the guess.
3. Recall stays **pre-gate**. ADR-12 deliberately returns a stored person
   fact before the confidence gate because retrieval is not generation.
   Do not regress that — a stored fact is exactly what this phase wants
   more of, not less.
4. The stub build stays dependency-free and green. The entire answer gate
   must be exercisable without llama.cpp; that is what item 1 is for.
5. Set the answer-side threshold from what item 5 **measures**, not from
   what would make the table look good. If the honest finding is that on
   a 0.5B model this number barely separates a good answer from a bad
   one, write that down and let item 3 carry the guarantee. Do not let
   STATE.md imply ECHO can now detect a confidently-wrong answer in
   general — it cannot, and claiming otherwise would be the first
   dishonest line in the file.

## Immediate deliverable for this session

1. The `ILlm` DI seam on `make_cognitive_core()` + `tests/fake_llm.hpp`.
2. `LlmReply::confidence`, computed from real llama.cpp logits on the
   real path and inert in the stub.
3. Self-referential classification in `utterance.hpp` plus
   grounded-or-abstain routing, replacing the `answer_query() == ""`
   fall-through.
4. A third, distinct decline path with its own line and its
   `LowConfidenceTrend` caregiver posture.
5. `tests/answer_gate_test.cpp` green in the stub build (**13 stub-build
   suites**), and a measured answer-confidence table from the real tiny
   model in the `real-llm` job.
6. `docs/STATE.md`, `docs/ARCHITECTURE.md`, and `docs/DECISIONS.md`
   (**ADR-18**) updated — including a plain statement of what this
   phase does *not* catch.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 21: answer-side safe mode (grounded recall, abstention, answer confidence)"
git push -u origin feature/answer-side-safe-mode
gh pr create --base master --head feature/answer-side-safe-mode \
  --title "Phase 21: answer-side safe mode — gate the answer, not just the input" \
  --body-file pr_body.md
```

# ECHO OS — Phase 22: The Caregiver Boundary (a consented link that doesn't break the privacy proof)

## Why this phase

`companion-sync` has been the device's only off-device path since the
first scaffold, and twenty-one phases later
`companion-sync/src/companion_sync.cpp` is still a logging stub:
`connect()` sets a bool and logs "connected via BLE (stub)",
`send_alert()` logs the alert kind, `send_status()` logs "status
heartbeat sent (stub)", and `poll_firmware_update()` returns
`Unavailable` under a `TODO(companion): verify signature before
applying`. There is no pairing, no authentication, no encryption, and no
signature verification on the device's only **inbound** path.

Meanwhile the product has a hole the size of half its users. ECHO is worn
by someone with memory loss **and used by their caregiver** — the README
says so in its first paragraph. Today a caregiver cannot set a reminder,
cannot see whether the 9am medication reminder was acknowledged, and
cannot enrol anyone in advance. The only way anything reaches the store is
the wearer speaking to the glasses, which assumes a wearer who reliably
remembers to. Phases 15 and 16 built exactly the data a caregiver needs
and then — correctly — proved at compile time that none of it can reach
`companion-sync`.

That tension is this phase's actual subject, and it must be resolved
deliberately rather than by quietly loosening the proof. The Phase-10/15
guarantee is not "nothing about memory may ever leave". It is that **raw**
memory content — embeddings, person records, event rows — has no path
off-device. A caregiver learning *"the 9am reminder was acknowledged"*
does not require shipping an SFace embedding. So: keep every existing
`static_assert` biting, unchanged, and add a separate, minimized,
consent-gated channel that carries the answer without carrying the record.

Branch `feature/caregiver-boundary` from `master` (confirm PR #20 is
merged first).

## What to build

1. **Define what may cross as a type, then prove the rest still can't.**
   Add a `CaregiverDigest` to `companion-sync`: counts and states only —
   reminders due / delivered / acknowledged over a window, safe-mode
   engagements today, last sync time. **No** name, **no** embedding,
   **no** free-text note, **no** event `summary`. Then extend the
   existing `static_assert` blocks in `tests/companion_sync_test.cpp` and
   `tests/memory_engine_test.cpp` so the proof reads precisely: no send
   path accepts a `SensorFrame`, an `Embedding`, a `PersonRecord`, or an
   `EventRecord`; the digest is constructible from none of them; and
   every digest field is a scalar or an enum, never a string or a buffer.
   Note the existing proof deliberately allows `Alert::note` to be a
   `std::string` "human summary" — the digest gets no such affordance,
   because a periodic structured feed is a very different exposure from a
   one-off alert. Re-run the Phase-15/16 discipline of **inverting an
   assertion to confirm it fails to compile**; a privacy proof that
   doesn't bite is decoration.
2. **Consent, held on-device and revocable.** A digest is built only when
   consent exists — granted by the wearer, or by a caregiver during a
   supervised pairing session — recorded in the memory engine like any
   other record, with a scope and a timestamp. Revocation is immediate and
   the path goes cold on the next tick. With no consent, `send_digest()`
   returns `Unavailable`; it must **not** return an empty digest, which
   would read to a caregiver as "nothing happened today" rather than "you
   are not permitted to see this". Log grant and revoke as narrow events —
   exactly the kind of thing a caregiver could reasonably be shown — and
   nothing more.
3. **A real transport boundary behind the existing interface.** Follow the
   ADR-9 pattern that has already worked twice: an `ICompanionTransport`
   with a deterministic in-process fake for CI and a real backend hook.
   Real BLE GATT is hardware and stays a **documented stub** — say so
   plainly, don't fake it — but a loopback / local-socket transport is an
   honest stand-in for the protocol layer, the same way `IHttpClient` let
   Phase 5 exercise the entire request/response shape without a live
   credential. Pairing derives a shared key once, out-of-band, in a
   supervised session; reuse Phase 16's `device_key.hpp` and appkit's
   `.echo-tokens/` posture rather than inventing a third key-storage
   mechanism, and be as precise about what pairing does and does not
   guarantee as ADR-14 was about AES-CTR being unauthenticated.
4. **Treat the inbound path as hostile.** This is the first time anything
   has ever come *into* the device from outside, and it deserves the
   paranoia the outbound path has had since Phase 1.
   - **Firmware.** Implement the signature verification that
     `poll_firmware_update()`'s TODO has been promising since the
     scaffold — or, if a real signing story is genuinely more than one
     phase, make it **fail closed** and say so in STATE.md. Do not ship a
     path that half-verifies. An unsigned update channel on a device worn
     by someone who cannot evaluate a prompt is the worst inbound surface
     in the system.
   - **Caregiver commands.** A caregiver may add a reminder or pre-enrol a
     person's *name*. Every inbound command is untrusted input:
     length-capped, schema-validated on the same hardened JSON parser
     Phase 6 fixed (the depth cap, and the Gmail header-injection lesson
     about trusting structured input), and rate-limited. A caregiver-set
     reminder is a **write to the wearer's device**, so it rides ADR-10's
     confirm-before-send discipline in spirit: announced to the wearer,
     never silently applied.
   - **The naming rule stands.** No inbound command may ever create a
     person *from an embedding*. Phase 15's rule — a face is bound to a
     name only by the wearer, on-device, in the moment — is not
     negotiable, and a remote enrolment path is exactly how it would
     erode.
5. **Tests + honest docs.** `tests/caregiver_link_test.cpp`, in the stub
   build: a digest is built only under consent; revocation takes effect
   immediately; a digest carries no identifying content (assert
   field-by-field at run time as well as at compile time); an inbound
   reminder is validated, rate-limited, announced, and applied; malformed,
   oversized, and deeply-nested inbound payloads are rejected without
   crashing (point the Phase 6 fuzz corpus at the new decoder); firmware
   with an absent or bad signature is refused. Add the digest/heartbeat
   path to `tests/soak_test.cpp` so the new periodic path is proven
   leak-free across the existing 7 simulated days — Phase 20's harness is
   already there and a new recurring path is exactly what it was built to
   catch.

## Constraints

1. **The Phase-10/15 privacy proof must still bite, unchanged.** This
   phase adds a narrowly-typed consented channel; it does not relax an
   existing assertion. If a proposed digest field would require weakening
   a `static_assert`, that field does not ship. No exceptions, and no
   "temporarily" — this is the one guarantee the whole product rests on.
2. Minimization is the default. If a caregiver's question can be answered
   with a count or a state, it must not be answered with a name or a
   string.
3. Reuse: `device_key.hpp`, the appkit token-store posture, Phase 6's
   hardened JSON parser, Phase 17's `EngineGuard` around the new
   transport calls, Phase 20's soak harness. No parallel mechanisms —
   that discipline has held since Phase 15 and this phase, which touches
   keys, storage, IO, and the tick, is where it would be easiest to
   break.
4. The stub build stays dependency-free: the in-process and loopback
   transports must need no libcurl, no BLE stack, and no runtime network
   in CI.
5. Do not claim a caregiver app exists. This phase builds the device half
   of the boundary plus an honest test double for the other half, and
   STATE.md must say so in those words.

## Immediate deliverable for this session

1. `CaregiverDigest` plus the extended, re-verified compile-time privacy
   proof.
2. A consent record with immediate revocation, persisted in the memory
   engine.
3. `ICompanionTransport` with a deterministic fake, a loopback stand-in,
   and a documented real-BLE stub.
4. A hardened inbound path: validated, rate-limited, wearer-announced
   caregiver commands, and firmware that fails closed without a valid
   signature.
5. `tests/caregiver_link_test.cpp` green in the stub build (**14
   stub-build suites**), and the digest path added to the Phase 20 soak.
6. `docs/STATE.md`, `docs/ARCHITECTURE.md`, and `docs/DECISIONS.md`
   (**ADR-19**) updated — including the new permanent-until-hardware gap
   (real BLE, a real paired phone, a real caregiver app) recorded in the
   gap table alongside #12/#13/#14.

## Repo step (run as the final step)

```bash
git add -A
git commit -m "Phase 22: caregiver boundary (consented digest, paired transport, hardened inbound path)"
git push -u origin feature/caregiver-boundary
gh pr create --base master --head feature/caregiver-boundary \
  --title "Phase 22: the caregiver boundary — a consented link that preserves the privacy proof" \
  --body-file pr_body.md
```

