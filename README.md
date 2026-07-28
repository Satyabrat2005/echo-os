# ECHO OS

**The on-device software runtime for ECHO smart glasses.**

ECHO OS is the real-time software platform that runs on ECHO's glasses hardware
(ESP32-S3 class SoC, OV2640 camera, mic, EEG frontend, PAM8403 audio). It is
**not a from-scratch kernel** — it runs on top of a stripped-down embedded Linux
base (Yocto/Buildroot-style minimal image) and layers a custom runtime, a
voice-first UI shell, and a fully local AI pipeline on top. This is the same
architectural relationship Android has to its kernel, or watchOS to Darwin.

The device is worn by people living with memory loss and by their caregivers.
Every decision in this codebase is made in service of five non-negotiable design
principles.

## Design principles

1. **Premium and calm.** Minimal visual noise, voice-first interaction, large
   clear audio cues. The experience must feel reassuring, never cluttered.
2. **Sub-120 ms perception-to-response.** Camera/mic/EEG input → recognition →
   voice output, end to end, in under 120 ms. Every architectural choice is
   evaluated against this budget.
3. **Zero jank.** Audio and voice transitions must be smooth and consistent —
   never stutter or drop frames — even under thermal or battery pressure.
4. **Privacy by default.** No raw camera, audio, or EEG data leaves the device
   under any circumstance. All inference is local.
5. **Fail safe, not smart.** On low model confidence the system gives a minimal,
   reassuring safe-mode response and flags the caregiver app — it never guesses.

## Architecture

```
                          ┌──────────────────────────────────────────┐
                          │                boot/                     │
                          │   init sequence + Runtime (core loop)    │
                          └──────────────────────────────────────────┘
                                          │ owns & wires
   ┌───────────────┐   frames   ┌───────────────┐  observation ┌───────────────┐
   │ sensor-       │ ─────────► │  perception/  │ ───────────► │ cognitive-    │
   │ pipeline/     │ lock-free  │ face/obj/wake │  confidence  │ core/         │
   │ cam·mic·eeg   │  buffer    │ /ASR (quant.) │  scored      │ LLM + safe-   │
   └───────────────┘            └───────────────┘              │ mode gate     │
                                                               └──────┬────────┘
                                                          response    │
                                                                      ▼
   ┌───────────────┐   alerts / status / firmware ONLY      ┌───────────────┐
   │ companion-    │ ◄──────────────────────────────────────│  voice-ui/    │
   │ sync/ (BLE/Wi)│         (never raw sensor data)        │ TTS + shell   │
   └───────────────┘                                        └───────────────┘

            power-mgmt/  observes the whole loop → sets Idle/Interactive/Throttled
```

### Modules

| Module              | Language | Responsibility |
|---------------------|----------|----------------|
| `boot/`             | C++      | Fast cold-boot init on the embedded Linux base; owns the `Runtime` that wires the core loop and produces the `echo-os` binary. |
| `sensor-pipeline/`  | C++      | Real-time camera/mic/EEG capture with a **single-producer/single-consumer lock-free ring buffer**, minimal-copy handoff to perception. |
| `perception/`       | C++      | Quantized face/object recognition (lightweight CNN), wake-word detection, Whisper-class ASR. All local. |
| `cognitive-core/`   | C++      | Orchestration around the quantized on-device LLM (ONNX Runtime / TensorRT-equivalent), plus the **confidence-threshold safe-mode gate** (principle #5). |
| `voice-ui/`         | C++      | TTS output and the audio-first interaction shell; double-buffered for zero jank; tone-aware (reassuring vs. neutral). |
| `companion-sync/`   | C++      | BLE/WiFi sync to the caregiver companion app. Carries **alerts, status, and firmware only — never raw sensor data**, enforced by the interface shape (principle #4). |
| `power-mgmt/`       | C++      | DVFS / duty-cycle scheduling balancing the latency target against battery and thermals. |
| `common/`           | C++      | Shared vocabulary: `SensorFrame` (non-owning view), `Result<T>`, `Confidence`, the latency-budget definition, and logging. |

> The scaffold is written in C++17. The sensor pipeline's hot path is the natural
> home for Rust on the target (lock-free capture, zero-copy buffers); the module
> boundary is drawn so it can be swapped to Rust behind the same `ISensorSource`
> contract without touching downstream modules.

## The 120 ms latency budget

The end-to-end budget is encoded as data in
[`common/include/echo/latency.hpp`](common/include/echo/latency.hpp) and asserted
by a unit test — it is a build-enforced contract, not a comment.

| Stage             | Budget | What runs here |
|-------------------|-------:|----------------|
| Sensor capture    |   5 ms | Frontend latch + lock-free handoff to perception |
| Perception        |  45 ms | Wake-word / ASR / quantized face + object CNN |
| Cognitive core    |  50 ms | Quantized LLM inference + safe-mode gate |
| Voice output      |  18 ms | TTS synth + first audio sample out the amp |
| Cross-stage overhead | 2 ms | Scheduling slack |
| **End to end**    | **120 ms** | Sensor input → first sample of speech |

Every stage wraps its work in a `StageTimer` (RAII) that logs a warning on
overrun and feeds `power-mgmt`, which trades duty cycle for thermal headroom
while keeping any in-flight response smooth.

## Apps layer

On top of the safety-critical core sits [`apps/`](apps/) — the voice-first
application layer (media, browser, search, video, mail, telephony, camera,
gallery). It is a **separate layer with hard isolation from the core**: apps run
as their own OS processes under a supervisor, and nothing an app does can block,
slow, or crash the sub-120 ms perception → cognitive → voice loop. See
[`apps/README.md`](apps/README.md) for the full design; the essentials:

- **Voice-first, no touchscreen.** These are not phone apps. The glasses have no
  touchscreen and no window manager — every app is a *voice command → spoken /
  HUD response* loop. The only visual surface is the **HUD compositor**, which
  exposes three overlay primitives (subtitle text, one icon, a status glyph) and
  nothing else. No app ever gets a screen or a window (principle #1).
- **Isolated processes.** `apps/app-framework/` supervises each app as its own
  process, restarts a crashed app with bounded backoff, and bridges an app's
  output to the core's `voice-ui` through its **public interface only** — never
  its internals (principle #2, extended to the apps boundary).
- **Mock by default.** Spotify, Gmail, YouTube, and Google Search each need
  developer credentials that don't exist yet, so every external service ships a
  `--mock` backend returning realistic fake data behind a clean interface. The
  entire voice flow is testable today; wiring real credentials later requires
  **zero interface changes** (principle #4 — privacy by default, plus no surprise
  network access without a granted capability).

### Testing the apps layer on a laptop

The layer builds and runs standalone on a dev machine, with the laptop's
keyboard/mic standing in for the glasses' ASR and a simulated HUD band standing
in for the overlay. It is built by default as part of the top-level build (toggle
with `-DECHO_BUILD_APPS=OFF`), or on its own with `cmake -S apps -B build-apps`.

```bash
# Run the whole layer: spawns the 8 apps as isolated processes, routes typed
# "voice" commands to the owning app over IPC, and speaks/draws the mock reply.
./build/apps-bin/echo-apps                 # --window for the simulated HUD band
```

```text
> play some jazz          → media:     "Now playing Kind of Blue by Miles Davis."
> any unread messages     → mail:      "You have 3 unread messages…"
> call Sam                → telephony: "Calling Sam…"  (Bluetooth to paired phone)
```

Smoke tests cover command-in → correct-app → mocked-response-out for media, mail,
and telephony; that two apps render through the one shared HUD surface; the
permission gate; and real cross-process supervision (IPC round-trip + crash
containment). They run as part of `ctest` below.

## Building

Requires CMake ≥ 3.16 and a C++17 compiler. No external dependencies — the
scaffold compiles cleanly end to end with stub logic on any host toolchain, and
cross-compiles to the target unchanged.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the reference binary (boots the runtime, runs one tick of the core loop
through the stub pipeline, shuts down cleanly):

```bash
./build/boot/echo-os          # (build/boot/echo-os.exe on Windows/MinGW)
```

## Phase 3: Real Local AI

Phase 3 replaces the core pipeline's stubs with **real, fully local, offline**
models and adds a **laptop HUD overlay** that stands in for the glasses display,
so the whole loop runs end to end on a laptop:

> webcam + mic in → **"Hey ECHO"** wake word → speech-to-text → {face
> recognition | app routing | LLM reasoning, behind the safe-mode gate} →
> spoken response (TTS) + HUD subtitle out.

Nothing here touches the network — wake word, ASR, reasoning, TTS, and face/
object recognition all run on-device (principle #4). The mocked external services
(Spotify, Gmail, …) stay mocked; this phase makes the *core intelligence* real,
not the third-party integrations.

### Engines and models used

Every engine is behind a CMake option that defaults **OFF**, so the tree still
builds end to end with zero dependencies (the stub fallback). Turn one on once
its library is installed and its model placed under [`models/`](models/README.md).
Flip them all with `-DECHO_REAL_AI=ON`.

| Stage | Module | Engine (option) | Suggested model + quantization |
|-------|--------|-----------------|--------------------------------|
| Wake word | `perception/` | Picovoice **Porcupine** (`ECHO_WITH_PORCUPINE`) | custom `Hey ECHO` `.ppn` keyword |
| Speech-to-text | `perception/` | **whisper.cpp** (`ECHO_WITH_WHISPER`) | `ggml-base.en-q5_1.bin` (or `small.en`) |
| Reasoning + intent routing | `cognitive-core/` | **llama.cpp** (`ECHO_WITH_LLAMA`) | `Llama-3.2-3B-Instruct` **Q4_K_M** (or `Phi-3.5-mini-instruct` Q4_K_M) |
| Text-to-speech | `voice-ui/` | **Piper** (`ECHO_WITH_PIPER`) | `en_US-amy-medium` (`.onnx` + `.onnx.json`) |
| Face detect + recognize | `perception/` | **OpenCV** YuNet + SFace (`ECHO_WITH_OPENCV`) | `face_detection_yunet.onnx`, `face_recognition_sface.onnx` |
| Object/scene | `perception/` | **OpenCV** dnn (`ECHO_WITH_OPENCV`) | `mobilenet_v2.onnx` (optional) |
| HUD overlay + mic/speaker I/O | `apps/hud-compositor/`, `voice-ui/`, `sensor-pipeline/` | **SDL2** (`ECHO_WITH_SDL`, +`SDL2_ttf` for subtitles) | — |

Notes on the choices:
- **Wake word — Porcupine over openWakeWord.** Porcupine has the simpler local C
  API. It needs a **free AccessKey** obtained once from the Picovoice console;
  that key is a one-time *setup* step and is validated **offline** — no audio or
  request ever leaves the device at runtime. Put the key in
  `models/porcupine_access_key.txt` (or `$PV_ACCESS_KEY`). The wake detector runs
  continuously on the mic at negligible idle CPU.
- **ASR** streams as batch-on-silence for this phase: the engine spots the wake
  word, captures until ~700 ms of trailing silence, then transcribes the
  utterance (energy-based endpointing in `perception/src/perception_engine.cpp`).
- **Reasoning** runs the LLM **only after** the confidence-threshold safe-mode
  gate passes, and it also emits an optional `[route:<app>]` tag so the runtime
  can hand app-shaped requests to the apps layer. A failed or empty generation
  falls back to safe mode — **the model never guesses** (principle #5, untouched).
- **TTS** maps the scaffold's reassuring/neutral tone onto Piper's `length_scale`
  so safe-mode replies are spoken a little slower and warmer.
- **HUD** is a borderless, always-on-top SDL overlay near the bottom of the
  screen rendering the **same three primitives** as the on-device compositor
  (subtitle, one icon, a status dot) and nothing else. It runs on its own thread
  and `present()` only copies the latest frame, so an app can never block or
  crash the core loop (constraint #4).

### Building the real stack

The Phase 4 helper scripts automate the dependency bring-up (see
[Phase 4](#phase-4-real-world-bring-up--validation) below):

```powershell
# 1. fetch/build whisper.cpp + llama.cpp, download the free models:
.\scripts\setup_deps.ps1
#    ...then do the manual steps it prints (OpenCV/SDL2, Piper binary, LLM gguf,
#    and the account-gated Porcupine key + custom "Hey ECHO" wake word).
# 2. configure with every ECHO_WITH_* ON, and build:
.\scripts\build_real.ps1 -PorcupineRoot "<unpacked porcupine sdk>"
```

Or drive CMake directly:

```bash
# Everything on at once (needs all libs installed + models placed):
cmake -S . -B build -DECHO_REAL_AI=ON
# ...or pick engines à la carte, e.g. reasoning + TTS + HUD only:
cmake -S . -B build -DECHO_WITH_LLAMA=ON -DECHO_WITH_PIPER=ON -DECHO_WITH_SDL=ON
cmake --build build
```

Point CMake at any dependency that isn't on the default search path with the
usual variables: `-DOpenCV_DIR=…`, `-DSDL2_DIR=…`, `-Dwhisper_DIR=…`,
`-Dllama_DIR=…`, and `-DPORCUPINE_ROOT=<unpacked Porcupine SDK>`. See
[`cmake/echo_ai.cmake`](cmake/echo_ai.cmake) for exactly how each is resolved.

> **llama.cpp API drift.** If the `-DECHO_WITH_LLAMA=ON` build fails on one line
> in [`cognitive-core/src/llm.cpp`](cognitive-core/src/llm.cpp) — the KV-cache
> clear (`llama_memory_clear` / `llama_kv_self_clear` / `llama_kv_cache_clear`) —
> your llama.cpp is a different API revision than the default. Reconfigure with
> `-DECHO_LLAMA_KV_CLEAR=self` (early–mid 2025) or `=cache` (pre-2025). This is
> the single call most likely to move between versions; the rest of the adapter
> targets the current API.

### Getting the models

Place these under [`models/`](models/README.md) (see that file for the exact
filenames and per-file env overrides). Sketch of the sources:

```bash
# whisper.cpp model (quantized English base):
#   from the whisper.cpp repo:  ./models/download-ggml-model.sh base.en-q5_1
#   -> copy ggml-base.en-q5_1.bin into echo-os/models/

# llama.cpp instruct GGUF (example: Llama-3.2-3B-Instruct Q4_K_M) — download the
#   .gguf from its model card, then:  cp <file>.gguf echo-os/models/llm.gguf

# Piper voice:  download en_US-amy-medium.onnx AND en_US-amy-medium.onnx.json
#   from the Piper voices release into echo-os/models/

# OpenCV face models (YuNet + SFace) from the OpenCV Zoo, into models/ as
#   face_detection_yunet.onnx and face_recognition_sface.onnx

# Porcupine:  train a custom "Hey ECHO" keyword in the Picovoice console, download
#   hey-echo.ppn + porcupine_params.pv into models/, and save your AccessKey to
#   models/porcupine_access_key.txt

# Enrolled faces (the demo test set): a photo per person, named after them:
#   echo-os/models/faces/grace.jpg   ->  ECHO will say "That's Grace."
```

### Running the demo

The demo binary is `echo-demo` (target `run_demo`), wired in
[`apps/demo/echo_demo.cpp`](apps/demo/echo_demo.cpp). The wrapper scripts build
and launch it and set the real-AI options for you:

```bash
# Windows (PowerShell):
.\scripts\run_demo.ps1                 # real: webcam + mic + wake word + HUD overlay
.\scripts\run_demo.ps1 -Mode stub      # no models needed: type commands, headless HUD

# Linux / macOS:
./scripts/run_demo.sh                   # real mode
./scripts/run_demo.sh --mode stub       # stub mode
```

Or straight from CMake once configured: `cmake --build build --target run_demo`.
Flags on `echo-demo`: `--window` (SDL HUD, default via scripts), `--text` (type
utterances instead of speaking — no mic/wake needed), `--no-camera`,
`--headless-hud`.

> **Runtime DLL note (Windows).** Run with your build toolchain's `bin` **first**
> on `PATH`, and put the dependency DLLs (`SDL2.dll`, OpenCV, `whisper.dll`,
> `llama.dll`, Piper) beside the exe or on `PATH`. A mismatched `libstdc++`/
> runtime from an unrelated tool (e.g. Git's bundled MinGW) on `PATH` ahead of
> yours will crash the binary at startup — this is an environment/ABI issue, not
> a bug in the demo.

### Manual test flow

With `models/` populated and a couple of faces enrolled, run
`.\scripts\run_demo.ps1` and try:

1. **Wake + reason.** Say **"Hey ECHO, what should I do now?"** → you hear a
   short spoken reply and see it on the HUD. Ask something it can't know and it
   deliberately falls to the calm safe-mode line instead of guessing.
2. **Face recognition.** Look at the webcam with an enrolled person in frame and
   say **"Hey ECHO, who is this?"** → it answers **"That's &lt;name&gt;."** and the
   HUD shows the name with a ✓ status. An unknown face → "I don't recognize them
   yet."
3. **App routing.** Say **"Hey ECHO, play some music"** → routed to the mocked
   media app, which speaks *"Now playing Kind of Blue by Miles Davis."* and draws
   a ▶ icon + subtitle on the HUD.

No mic set up yet? `.\scripts\run_demo.ps1 -Mode stub` (or `echo-demo --text`)
lets you type the same utterances and exercises routing + the safe-mode gate with
zero models installed.

### Measured latency (fill from your run)

Each turn's real timings are written to `latency_log.csv` and printed live; a
min/avg/max summary prints on exit (see
[`apps/demo/latency.hpp`](apps/demo/latency.hpp)). The per-stage **targets** below
are the build-enforced budget from `common/include/echo/latency.hpp`; the
**measured** column is intentionally left for you to populate from a handful of
real runs on your hardware, so this table holds a real number, not a simulated
one.

Fill this in from **≥20 real end-to-end turns** (Phase 4, Step 4). Columns are
min / avg / max across the run — copy them straight out of the on-exit summary;
leave them blank until you have real numbers.

| Stage | Target | Min | Avg | Max | CSV column |
|-------|-------:|----:|----:|----:|------------|
| Perception (wake + ASR) | 45 ms | _—_ | _—_ | _—_ | `perception_ms` |
| Cognitive (LLM + gate) | 50 ms | _—_ | _—_ | _—_ | `cognitive_ms` |
| Voice output (TTS start) | 18 ms | _—_ | _—_ | _—_ | `voice_ms` |
| **End to end** | **120 ms** | _—_ | _—_ | _—_ | `total_ms` |

**Gap analysis (fill after the run):** measured end-to-end **___ ms** vs. the
120 ms target; the dominant stage was **___** (expected: cognitive — quantized
LLM decode on a laptop CPU). Do **not** adjust the budget to match — record the
real overrun. Forward-looking options to close the gap, as future work, *not*
changes made in this phase:

- a smaller / more aggressively quantized instruct model (e.g. 1–3B, Q4 → Q3);
- streaming ASR instead of batch-on-silence, so transcription overlaps speech;
- GPU offload (`n_gpu_layers`) where a discrete GPU is available;
- a lighter wake-word / endpointing path to shave perception ms.

> **Honest note on this deliverable.** Through Phase 3 the integrated pipeline
> was verified only in **stub/text mode** (routing, the safe-mode gate, HUD
> frames, and latency logging all confirmed working); the native models were
> never run in the dev sandbox. Phase 4 is where the real libraries and weights
> get installed on the laptop and the numbers above become real. Real quantized
> LLM/ASR inference on a laptop CPU will very likely **exceed the 120 ms
> budget** — when it does, the number stays as measured and the gap analysis
> above explains it. The `StageTimer` overrun path and `latency_log.csv` are
> exactly how that honest number is captured.

### Swapping in the embedded-hardware versions later

The engine seams are drawn so the laptop stand-ins swap for the on-SoC
equivalents **without touching any module's interface** — only the
`ECHO_WITH_*` option and the `echo::dep-*` target behind it change:

| Laptop (Phase 3) | Embedded target |
|------------------|-----------------|
| Porcupine on the laptop mic | on-DSP keyword spotter (same `IWakeWord`) |
| whisper.cpp on CPU/GPU | quantized ASR on the NPU (same `IAsr`) |
| llama.cpp GGUF on CPU/GPU | quantized LLM via the SoC runtime (same `ILlm`) |
| Piper subprocess + SDL audio | on-device TTS → PAM8403 amp (same `IVoiceUi`) |
| OpenCV YuNet/SFace on the webcam | quantized CNN on the OV2640 stream (same `IVision`) |
| SDL overlay window | the glasses' actual HUD (same `IHudCompositor` primitives) |

Because each real path is a `#if defined(ECHO_WITH_*)` adapter behind an
unchanged interface, the embedded build just defines a different option and links
a different `echo::dep-*` — downstream modules never notice.

## Phase 4: real-world bring-up & validation

Phase 4 makes Phase 3 **real**: it installs the actual dependencies on the actual
laptop, builds with every `ECHO_WITH_*` flag ON, fixes what real library versions
surface, tunes the engines against real behavior, and records genuine measured
numbers. No new features. The physical/hardware steps below are done on the
laptop with a mic, speaker, and webcam in the loop — they can't be run in a
headless sandbox, so this section is filled in **during** that bring-up, not
before.

**Ordered bring-up checklist**

1. `.\scripts\setup_deps.ps1` — builds whisper.cpp + llama.cpp, downloads the free
   models, writes `models/INSTALLED_VERSIONS.md`. Then do its printed manual steps:
   OpenCV/SDL2 (vcpkg), the Piper binary, an instruct `.gguf`, and — the only
   account-gated one — the Picovoice **AccessKey + custom "Hey ECHO" `.ppn`**.
2. `.\scripts\build_real.ps1 -PorcupineRoot <sdk>` — configure all flags ON and
   build warning-free. If the llama.cpp KV-clear line fails, add
   `-KvClear self` (or `cache`) — see the note under *Building the real stack*.
3. `.\scripts\run_demo.ps1` — run the real demo end to end and confirm each item
   in [Manual test flow](#manual-test-flow) with real hardware.
4. Run ≥20 real turns, then `.\scripts\analyze_latency.ps1` — it reads
   `latency_log.csv` and prints the min/avg/max table + gap-analysis line ready to
   paste into the [latency table](#measured-latency-fill-from-your-run) (it warns
   if you logged fewer than 20 turns rather than pretend).
5. Have one **non-founder** use it with no instructions beyond *"say 'Hey ECHO'
   and talk to it."* Log every failure mode below.

**Installed dependency versions** (Step 1 — fill from `models/INSTALLED_VERSIONS.md`,
which `setup_deps` generates; document exact versions for the future embedded move):

| Dependency  | Version / commit | Notes |
|-------------|------------------|-------|
| Toolchain (MinGW/GCC) | MinGW-w64 ucrt g++ **15.1.0** | measured Phase 6 (this dev laptop) |
| CMake       | **4.0.2** | measured Phase 6 |
| libcurl     | **8.21.0** | official curl win64-mingw (UCRT/SChannel); `ECHO_WITH_NETWORK=ON` compiles + links |
| whisper.cpp | _fill (commit)_ | + `base.en-q5_1` model |
| llama.cpp   | _fill (commit)_ | KV-clear variant: _current/self/cache_ |
| OpenCV      | _fill_ | YuNet + SFace |
| SDL2 / SDL2_ttf | _fill_ | |
| Piper       | _fill (tag)_ | voice: `en_US-amy-medium` |
| Porcupine   | _fill (SDK ver)_ | custom "Hey ECHO" `.ppn` |
| LLM         | _fill (model + quant)_ | e.g. Llama-3.2-3B-Instruct Q4_K_M |

**Wake-word tuning** (Step 3): Porcupine sensitivity starts at the library default
— record what you actually observe (false accepts vs. missed wakes over a handful
of tries) and the value you settle on here: _sensitivity = ___ ; rationale: ___._

### Known Issues

Observed failure modes from real runs (Steps 3–5). Keep this **honest and
populated** rather than falsely clean — an accurate known-issues list is the most
valuable output of this phase. Fill in as you find them; delete the placeholder
once real entries exist.

| # | Symptom | Where (stage/app) | Severity | Notes / next step |
|---|---------|-------------------|----------|-------------------|
| _—_ | _e.g. wake-word missed at conversational volume_ | _perception_ | _—_ | _—_ |

_First outside-user test:_ tester (non-founder) — _date / who_; top confusions
observed — _fill in_.

## Phase 5: real third-party integrations

Phase 5 replaces the mock Spotify / Gmail / Google-Search / YouTube backends with
real network-facing ones, **behind the exact same `IApp` interfaces** the apps
already exposed since Phase 2 — swapping the backend changes not one caller. The
`--mock` default is untouched, so CI and the stub build stay deterministic and
credential-free; real backends are opt-in.

> ⚠️ **Prerequisite — not yet satisfied at time of writing.** This phase builds
> network integrations *on top of* the reasoning pipeline, so that pipeline must
> already be trustworthy: Phase 4's real-hardware bring-up (≥20 measured turns, a
> real mic/speaker/webcam demo, a non-founder tester) must be **done and recorded**
> first. As of this commit the [Phase 4](#phase-4-real-world-bring-up--validation)
> validation tables are still unfilled — so the "run it for real" parts of this
> section (the live credentialed test, the observed Known Issues) are written as
> **procedures to execute, not results claimed.** Do not tick them off until they
> have actually run.

### What was built

- **`apps/appkit/`** — the shared toolkit every real backend links: the single
  network boundary (`net::IHttpClient`, libcurl behind `ECHO_WITH_NETWORK`), a
  minimal JSON reader, URL/base64 encoding, `.env`/credential loading, an OAuth
  token cache, the **confirm-before-action gate**, and headless readability
  extraction. Everything except the libcurl transport is dependency-free and
  **unit-tested** (`echo-appkit-tests`).
- **Real backends** for `media` (Spotify Web API), `mail` (Gmail API), `search`
  (Google Custom Search), `browser` (live fetch + on-device readability, reusing
  the search result set), and `video` (YouTube Data API). Each app now selects a
  mock or real backend from the same `--mock`/`--real` flag; a missing credential
  or absent network transport degrades to a calm spoken line, never a crash
  (constraint #2).

### The two switches: `--mock` and `ECHO_WITH_NETWORK`

Real backends need **both** the runtime `--real` flag **and** a build compiled with
the network transport:

```bash
# Stub/CI build (default): no libcurl, no sockets, backends report Unavailable
cmake -S apps -B build-apps -G "MinGW Makefiles"

# Real build: compile the libcurl transport (needs libcurl installed)
cmake -S apps -B build-apps -G "MinGW Makefiles" -DECHO_WITH_NETWORK=ON
```

At runtime, `echo-apps --real` asks each app for its real backend; without
`ECHO_WITH_NETWORK` the HTTP client is null and every real backend cleanly reports
`Unavailable` (so `--real` on a stub build degrades, it does not lie).

> **Compile status (updated Phase 6):** both builds now compile and pass `ctest` in
> this repo's toolchain. The `-DECHO_WITH_NETWORK=ON` transport in
> [`http.cpp`](apps/appkit/src/net/http.cpp) was previously written-but-uncompiled;
> it is now built and linked against **libcurl 8.21.0** (official curl win64-mingw,
> UCRT/SChannel — provisioned by `scripts/setup_deps.ps1`). The whole project links
> it, the app binaries import `libcurl-x64.dll`, and all suites are green. What is
> still **not** proven is the *live* API traffic — no Spotify/Gmail/Search/YouTube
> call has run against real credentials yet; that remains the credentialed real run.

### Obtaining each credential (and under which account)

Copy [`.env.example`](.env.example) to `.env` (gitignored — **never commit it**)
and fill it in. Record in `.env.example`'s "Registered under" lines which account
owns each developer app — this matters later for company-vs-personal ownership of
the API credentials.

| Service | Where to register | Key variables | Scopes / notes |
|---------|-------------------|---------------|----------------|
| **Spotify** | [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) → create app | `ECHO_SPOTIFY_CLIENT_ID`, `ECHO_SPOTIFY_CLIENT_SECRET`, `ECHO_SPOTIFY_REDIRECT_URI` | Add the redirect URI to the app settings verbatim |
| **Gmail** | Google Cloud Console → enable **Gmail API** → OAuth consent + OAuth client (Desktop) | `ECHO_GMAIL_CLIENT_ID`, `ECHO_GMAIL_CLIENT_SECRET` | Request **only** `gmail.readonly` + `gmail.send` — nothing broader |
| **Google Search** | Google Cloud → enable **Custom Search API**; create a Programmable Search Engine | `ECHO_GOOGLE_SEARCH_KEY`, `ECHO_GOOGLE_SEARCH_CX` | `CX` is the search-engine id |
| **YouTube** | Same Cloud project → enable **YouTube Data API v3** | `ECHO_YOUTUBE_API_KEY` | Falls back to the search key if unset |

### OAuth flow choice (and why)

Both Spotify and Gmail use the **Authorization Code flow with a loopback redirect**
(`http://127.0.0.1:8888/callback`), not device-code. Rationale for a headless
glasses context: Spotify has no device-code grant, and loopback is the flow both
providers recommend for a native app that can pop a browser **once**. On the
glasses that one-time consent happens during pairing on the companion phone/laptop
— not on the device — after which the cached **refresh token** drives silent
renewals and the glasses never show a browser again. So the interactive consent is
a documented, one-time setup step; the backends implement everything after it
(token exchange, silent refresh, and the API calls themselves).

**One-time authorize (manual, per service):** open the authorize URL the backend
builds (`spotify_authorize_url(...)` / the Gmail equivalent) in a browser, approve,
copy the `code` from the loopback redirect, exchange it once for tokens, and let
the token store cache them under `ECHO_TOKEN_DIR` (`.echo-tokens/`, gitignored).
_A small `scripts/authorize.ps1` helper to script this loopback capture is a
follow-up; until then it is a manual paste, documented here rather than faked._

### Confirm-before-send (constraint #1)

No email is ever sent on a single utterance. `"reply saying <text>"` **stages** the
message and speaks it back — *"Ready to reply to Dr. Alvarez: '…'. Say 'send' to
confirm, or 'cancel'."* — and only an explicit **"send"/"confirm"** actually sends.
An explicit "cancel", an unrelated command, or an unrecognized word all **fail
safe**: nothing is sent and the pending action is cleared (it is one-shot, so a
later stray "yes" can't resurrect it). This gate
([`apps/appkit/.../confirmation.hpp`](apps/appkit/include/echo/apps/confirm/confirmation.hpp))
is the one piece of Phase 5 fully covered in CI — the live send can't run there,
but the gate that guards it is exercised by both `echo-appkit-tests` (the gate in
isolation) and `echo-apps-smoke` (`test_send_email_confirmation_gate`, the whole
reply→confirm flow through the real `MailApp` with a mock backend).

### Secret hygiene (constraint #3)

`.env` and `.echo-tokens/` are gitignored; only `.env.example` (no real values) is
tracked. Run the credential scan before every push — it blocks the commit if an API
key, OAuth secret, token file, or `.env` is about to enter the repo:

```bash
bash scripts/check_secrets.sh
```

```bash
powershell -File scripts/check_secrets.ps1
```

Wire it as a hook with `ln -sf ../../scripts/check_secrets.sh .git/hooks/pre-commit`.

### Manual test flow — real credentialed run (to run once, then record here)

⚠️ **Not yet run.** These steps need real credentials on a laptop with the network
build; they cannot run in CI. Execute once and paste the evidence
(screenshot/log) into the table — do not tick them off unclaimed.

1. **Spotify playback by voice.** `echo-apps --real` → *"play some music"* → real
   playback starts and the HUD shows real now-playing metadata. Evidence: _fill_.
2. **A real unread email read aloud.** *"read my unread email"* → the latest real
   unread sender + subject are spoken. (As of Phase 6 the greedy-`read` misroute is
   fixed — see Known Issue 8 — so the natural phrasing now works.) Evidence: _fill_.
3. **A real search summarized aloud.** *"search for …"* → the top real result is
   **summarized**, not dumped. Evidence: _fill_.
4. **Confirm-before-send, end to end.** *"reply saying …"* then *"send"* → a real
   email is sent; repeat with *"cancel"* and confirm nothing sends. Evidence: _fill_.

| Test | Ran? (date) | Evidence (screenshot/log) | Result |
|------|-------------|---------------------------|--------|
| Spotify playback | _—_ | _—_ | _—_ |
| Unread email read | _—_ | _—_ | _—_ |
| Search summarized | _—_ | _—_ | _—_ |
| Send-email confirm gate | _—_ | _—_ | _—_ |

### Known Issues (real APIs, real failure modes)

Real APIs surface failure modes mocks never could — rate limits, token expiry,
network flakiness. The rows below are **anticipated** and how the code is meant to
handle them; replace/annotate each with what you **actually observe** on the real
run (and add the ones you didn't predict). Keep this honest rather than falsely
clean.

| # | Failure mode | Anticipated handling | Observed? |
|---|--------------|----------------------|-----------|
| 1 | OAuth access token expired mid-session | silent refresh via cached refresh token before each call | _not yet run_ |
| 2 | Refresh token revoked / consent withdrawn | backend reports `Unavailable` → calm "I can't check email right now" | _not yet run_ |
| 3 | API rate limit / quota (HTTP 429) | request completes with non-2xx → degrade, no crash | _not yet run_ |
| 4 | Network timeout / DNS / TLS failure | transport failure (status 0) → `HardwareError` → degrade | _not yet run_ |
| 5 | Spotify "no active device" for playback control | control call is best-effort; `current()` reports true state | _not yet run_ |
| 6 | ASR mishears the reply body before "send" | user hears the staged summary and can "cancel"; nothing sends without explicit "send" | _not yet run_ |
| 7 | Stale pending reply if user switches apps then says "send" later | gate is one-shot per compose; residual risk — _watch for this on the real run_ | _not yet run_ |
| 8 | ~~Greedy first-token NLU: *"read my unread email"* matches `read` (browser) before `unread` (mail)~~ | **FIXED in Phase 6** — NLU is now specificity-aware: the browser's generic `read`/`open` yield to a domain-specific intent elsewhere in the utterance. Regression test `echo-nlu-routing` covers the exact phrase + variants. | resolved |

_First real credentialed run:_ _date / who_ — _fill in after it happens._

## Status

Phase 3 makes the **core intelligence real** on a laptop while keeping the
scaffold's contracts intact. The safe-mode gate, the lock-free ring buffer, the
latency-budget model, and the apps-layer isolation are all **unchanged**; the
real engines are additive adapters behind their existing interfaces.

### What's real vs. stubbed

- **Real:** module boundaries and interfaces; the lock-free ring buffer; the
  latency-budget model; the safe-mode gate logic; the boot/runtime wiring; the
  smoke tests; and — new in Phase 3 — the wake-word / ASR / LLM / TTS / face+
  object adapters (whisper.cpp, llama.cpp, Piper, Porcupine, OpenCV), the SDL
  laptop HUD overlay, real webcam/mic capture, the integrated `echo-demo`, and
  end-to-end latency logging. All default OFF so the dependency-free stub build
  still compiles and the reference `echo-os` binary + CI stay deterministic.
- **New in Phase 5 (code real, live run pending):** real Spotify/Gmail/Search/
  YouTube backends behind the existing app interfaces, plus the `appkit` toolkit
  (HTTP boundary, JSON, credentials, OAuth token cache, confirm-before-action gate,
  readability). The request-building, response-parsing, credential loading, and the
  confirmation gate are **unit-tested in CI**; the libcurl transport is opt-in
  (`-DECHO_WITH_NETWORK=ON`) and the actual live API calls have **not yet been run
  against real credentials** (blocked on Phase 4 hardware validation — see the
  Phase 5 prerequisite note).
- **Still stubbed (`TODO`):** the on-glasses sensor DMA frontends and the BLE/
  WiFi companion transport (hardware phase); the scripted one-time OAuth authorize
  helper (`scripts/authorize.*`) — currently a documented manual step.

## License

Inherits the parent ECHO project's [MIT License](../LICENSE).
