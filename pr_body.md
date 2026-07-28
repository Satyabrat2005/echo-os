# Phase 3: real local AI pipeline + laptop HUD

Replaces the core pipeline's stubs with **real, fully local, offline** engines and
adds a **laptop HUD overlay** so the whole loop runs end to end on a laptop:

> webcam + mic → **"Hey ECHO"** wake word → speech-to-text → {face recognition |
> app routing | LLM reasoning, behind the safe-mode gate} → spoken response (TTS)
> + HUD subtitle.

## What's wired in

| Stage | Engine | Where |
|-------|--------|-------|
| Wake word | Picovoice **Porcupine** ("Hey ECHO", offline at runtime) | `perception/src/wake_word.*` |
| Speech-to-text | **whisper.cpp** (quantized base/small.en), batch-on-silence | `perception/src/asr.*` |
| Reasoning + intent routing | **llama.cpp** (small instruct GGUF), **behind the unchanged safe-mode gate** | `cognitive-core/src/llm.*`, `cognitive_core.cpp` |
| Text-to-speech | **Piper** (tone → `length_scale`) → SDL audio | `voice-ui/src/voice_ui.cpp` |
| Face + object | **OpenCV** YuNet + SFace, MobileNet | `perception/src/vision.*` |
| HUD overlay | borderless always-on-top **SDL2** window (same 3 primitives) | `apps/hud-compositor/src/hud.cpp` |
| Webcam/mic capture | OpenCV `VideoCapture` + SDL audio, one SPSC lane each | `sensor-pipeline/src/real_sources.cpp` |
| Integrated demo | `echo-demo` (`run_demo` target + `scripts/run_demo.*`) | `apps/demo/` |

## Constraints held (not relaxed)

- **Safe-mode gate untouched.** The LLM runs *only after* the confidence
  threshold passes; a failed/empty generation still falls to the reassuring
  safe-mode line. The model never guesses.
- **Everything local.** No network calls for wake word, ASR, reasoning, TTS, or
  vision. Porcupine's AccessKey is a one-time offline setup step. Mocked services
  stay mocked.
- **Apps isolation intact.** The HUD runs on its own thread and `present()` only
  copies a frame; the apps layer is reached only via the Router / voice bridge /
  HUD compositor — never core internals.
- **Zero-dependency build preserved.** Every engine is behind an `ECHO_WITH_*`
  option defaulting **OFF** (`-DECHO_REAL_AI=ON` flips them all). The stub build
  still compiles on any toolchain and the reference `echo-os` binary + CI stay
  deterministic. Both smoke tests pass.

## Verification & honest latency note

The integrated pipeline was verified end to end in **stub/text mode**: routing
("play some music" → media app), the **safe-mode gate** (unknown question →
reassuring fallback + caregiver flag), HUD frames, and per-turn latency logging
all confirmed working; both `ctest` smoke tests pass.

The **native models** (whisper/llama/Piper/Porcupine/OpenCV) were **not** run in
CI — they need the libraries + multi-GB weights installed locally. Real quantized
LLM/ASR on a laptop CPU will likely exceed the 120 ms budget; the demo writes
real per-turn timings to `latency_log.csv` and the README latency table is left
for those numbers to be filled from an actual run rather than fabricated.

See the README **"Phase 3: Real Local AI"** section for model download/setup, the
manual test flow, and swap-to-hardware guidance.
