## Phase 11 — Real engine verification in CI (no account required)

`docs/STATE.md` has honestly carried a "real wake/ASR/vision/LLM paths remain
untested — need installed libraries and the credentialed hardware run" line. That
is true for **wake-word** (Porcupine, account-gated) and any **full LLM** (too
large/slow for CI) — but it was *not* true for everything on the list. whisper.cpp
`tiny.en` (~75 MB), OpenCV's YuNet + SFace ONNX models (a few MB), and a Piper
voice are all free, direct downloads with **no account or credential**. This phase
runs those **real engines** — not the stub logic around them — against Phase 10's
fixtures, in CI, closing the slice of the gap that never actually needed the live
hardware session.

### What's added

- **`tests/real_asr_test.cpp`** — the **real whisper.cpp** engine (`tiny.en`) on
  real audio: `silence.wav` → near-empty transcript, `noisy_garble.wav` → no crash
  and a well-formed in-range result (extends Phase 10's robustness focus to the
  real parser), and a clear, Piper-synthesized phrase → a **non-empty** transcript
  containing the **expected words**. Content asserts on presence/rough content, not
  brittle exact strings.
- **`tests/real_vision_test.cpp`** — the **real OpenCV YuNet + SFace** on real
  face images: an enrolled identity → **detected and matched**, a different person
  → **detected but not matched**, and a non-face frame → **no detection**.
- **`tests/real_tts_test.cpp`** — the **real Piper** engine synthesizing both the
  neutral and reassuring voice-ui tones into valid, **non-silent** audio of
  plausible duration, and asserting the reassuring tone is **measurably slower**
  than neutral (the tone setting changes the real waveform, not just a label). Also
  asserts the exact `length_scale` mapping voice-ui uses.
- **A new `real-engines` CI job** (`.github/workflows/ci.yml`) that downloads the
  three free models + two public-domain face photos, **SHA-256-verifies every one**
  before use, builds whisper.cpp from source, builds ECHO with `ECHO_WITH_WHISPER/
  OPENCV/PIPER=ON`, and runs the suite above. Kept a **separate, parallel** job so
  it never gates the fast stub/network/analysis jobs; **models + the whisper build
  are cached** (the model cache is keyed on the checksum manifest) so a warm run
  skips ~230 MB of downloads and the source build.
- **`scripts/fetch_real_engine_assets.sh`** — the idempotent, checksum-guarded
  fetcher the CI job and the README's local instructions both use.

### Small refactor (to test the real path honestly)

`voice-ui` gained an internal `tts_synth.hpp`: the Piper synthesis half of
`speak()` (tone-scaled `--length_scale`, local `piper` subprocess) is split from
the SDL **playback** half, so the test drives real synthesis **headlessly** on a CI
runner. Playback (real speakers, a human ear) stays in `speak()` and is explicitly
out of scope. `PiperVoiceUi::speak()` now calls the extracted helper — behavior is
unchanged. `tests/fixture_io.hpp` gained `load_wav_path()` (absolute-path loader).

### Honesty (constraint #3)

These fixtures are **clean and synthetic** — a TTS phrase, well-lit frontal
public-domain portraits — so the tests prove the engines **work at all** on good
input, **not** that `tiny.en` or SFace hold up under field noise, motion, or poor
light. `docs/STATE.md` now splits the old gap line accordingly:

- **Now covered** (real weights, on fixtures, in CI): ASR, vision, TTS.
- **Still uncovered** (needs libraries/credentials/hardware): Porcupine wake-word
  (account-gated key), llama.cpp / full LLM (size + runtime), and all live
  mic/webcam/latency/human behaviour plus the SDL playback path.

### Deliberately out of scope, and documented as such

- **Porcupine wake-word** — needs an account-gated Picovoice access key.
- **llama.cpp / full LLM** — model size + CI runtime cost.
- **Live mic/webcam capture** — needs physical hardware and a human; this phase
  only exercises the engines against pre-recorded/synthesized fixtures.

### Impact on existing jobs

None. The real tests are built **only** when their `ECHO_WITH_*` flag is ON, so the
default + stub CI build is completely unaffected — verified locally: **9/9 stub
CTest suites still green**. Each real test also self-skips (green, loud message) if
its flag is on but the model isn't present, so bringing engines up one at a time
locally doesn't go red.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
