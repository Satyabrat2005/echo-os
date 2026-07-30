## Phase 13 — Real wake-word verification in CI (account-free)

Phases 11–12 verified every real engine in CI **except wake-word**, left out for one
reason: the Porcupine backend needs an account-gated Picovoice key we won't put in a
workflow. This phase closes that gap the same account-free way the others were
closed — by adding **openWakeWord** alongside Porcupine (not replacing it), behind
the existing `wake_word.hpp`/`IWakeWord` interface, and running it against real
models in CI.

openWakeWord is a fully local, **Apache-2.0** three-stage ONNX pipeline
(`melspectrogram` → Google `speech_embedding` → per-keyword classifier) that runs on
**MIT-licensed ONNX Runtime** — every file anonymously fetchable, no account, no key.

**With this, every real engine — wake-word, ASR, vision, TTS, LLM — is now verified
against real models in CI on synthetic/fixture input.** The only remaining gap is
live-hardware behaviour with a real human in a real room with real background noise —
which cannot be simulated and is not claimed as covered.

### What's added

- **openWakeWord backend** in [`perception/src/wake_word.cpp`](perception/src/wake_word.cpp),
  behind `ECHO_WITH_OPENWAKEWORD`, next to the untouched Porcupine path. Which one
  `make_wake_word()` returns is chosen by `-DECHO_WAKEWORD_BACKEND=openwakeword|porcupine`
  (default `openwakeword` — the one CI and anyone without a Picovoice account can run).
  Reproduces openWakeWord's pipeline faithfully (int16-as-float audio, the `x/10+2`
  mel transform, 76-frame/step-8 embedding windows, 16-embedding classifier windows;
  I/O tensor names queried at runtime, not hard-coded).
- **Pinned model assets**, the same rigorous way Phases 11–12 pinned theirs — exact
  URL, SHA-256, size, license, reason — in [`MANIFEST.md`](MANIFEST.md) and
  [`scripts/fetch_real_engine_assets.sh`](scripts/fetch_real_engine_assets.sh):
  openWakeWord's `melspectrogram.onnx`, `embedding_model.onnx`, and
  `hey_jarvis_v0.1.onnx` (Apache-2.0), plus the ONNX Runtime prebuilt (MIT). All
  checksum-verified before use.
- **`tests/real_wakeword_test.cpp`** — real openWakeWord inference against fixtures:
  a Piper-synthesized "hey jarvis" clip must clear the detection threshold; ordinary
  non-wake speech, silence, and loud garble must stay below it. It **prints every
  observed score** and asserts on the model's *actual* probabilistic behavior.
- **Wake-word fixtures**, extending Phase 10's, documented in
  [`tests/fixtures/README.md`](tests/fixtures/README.md): the two speech clips are
  **Piper-synthesized in CI** (not committed), the negatives reuse the committed
  synthetic `silence.wav`/`noisy_garble.wav`.
- **CI**: the existing `real-engines` job (which already provides Piper) gains
  `-DECHO_WITH_OPENWAKEWORD=ON`, the checksum-verified model + ONNX Runtime download,
  the wake-clip synthesis, and the wake-word test — cached by checksum like every
  other asset.

### Honest notes (constraints #2, #3, #4)

- **Custom "Hey ECHO" is a follow-up, not this phase.** This ships the **pretrained**
  "hey jarvis" model as the closest account-free stand-in. Training a bespoke
  "Hey ECHO" word (synthesize + mine negatives + train + validate) is a much larger
  undertaking than downloading a pretrained model, and is documented as such in
  `MANIFEST.md` and `docs/STATE.md` — not claimed as done.
- **Why a Piper "hey jarvis" clip is a fair positive.** openWakeWord's *own* training
  data is Piper-TTS-synthesized speech, so the clip is genuinely in-distribution —
  not a rigged input.
- **Nonzero error rates, not hidden.** Wake-word detection is probabilistic. The test
  asserts real behavior against these clips (peak score on the wake clip clears the
  threshold; each non-wake clip stays below it) and is **not** tuned to fake a
  perfect separation the model can't honestly deliver. The observed scores are
  printed in the CI log.
- **Porcupine is kept, not deleted** (constraint #4): still available behind
  `ECHO_WITH_PORCUPINE` as a higher-accuracy production option for a build that has
  done the account/key step.

### Impact on existing jobs

None. The backend and `tests/real_wakeword_test.cpp` are compiled **only** under
`-DECHO_WITH_OPENWAKEWORD=ON`, so the default + stub CI build is byte-for-byte
unaffected (stub build stays zero-dependency; verified green locally). The test
self-skips (green, loud message) if the flag is on but the models aren't present.

### The gap list now (docs/STATE.md)

- **Verified in CI against real models:** wake-word (openWakeWord), ASR (whisper),
  vision (OpenCV), TTS (Piper), LLM (llama.cpp, tiny model) — all account-free, on
  fixtures/synthetic speech.
- **Remaining, and inherently un-simulatable:** live-hardware behaviour with a real
  human — real mic/webcam, real room, real background noise, latency under load,
  SDL playback. Plus two documented non-engine items: production-grade LLM answer
  quality (deployment-size model) and a custom-trained "Hey ECHO" word.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
