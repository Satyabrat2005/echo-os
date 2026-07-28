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

## Status

This is the **first-pass scaffold**: clean module boundaries, documented
interfaces, and a build system that compiles end to end. Every module exposes its
real interface; implementations are stubs marked with `TODO(<module>)`. The
priority for this pass was clean, well-documented contracts over functionality.

### What's real vs. stubbed

- **Real:** module boundaries and interfaces, the lock-free ring buffer, the
  latency-budget model, the safe-mode gate logic, the boot/runtime wiring, and
  the smoke tests.
- **Stubbed (`TODO`):** the quantized models (CNN/ASR/LLM/TTS), the sensor DMA
  frontends, and the BLE/WiFi transport.

## License

Inherits the parent ECHO project's [MIT License](../LICENSE).
