# Core-pipeline test fixtures

Small, checked-in recordings that the Phase 10 test harness feeds through the
**real** core pipeline (`sensor-pipeline` → `perception` → `cognitive-core` →
`voice-ui`) instead of the synthetic unit-test data used elsewhere. They let CI
exercise real-shaped audio/image bytes end to end without a live mic, webcam, or
any account.

## Provenance — read this first

**Every file here is synthetic and deterministic.** None is a recording of a real
person. They are produced by [`make_fixtures.py`](make_fixtures.py), which uses a
fixed PRNG seed and closed-form waveforms, so regenerating them reproduces the
committed bytes byte-for-byte:

```bash
python make_fixtures.py
```

Synthetic is sufficient here because the default build runs the pipeline in
**stub-engine mode** (no Porcupine/whisper.cpp/OpenCV compiled in). The stub
wake-word never fires, the stub ASR returns empty text, and the stub vision
returns no faces/objects — so what the harness verifies is the *surrounding
production logic* (capture → SPSC queue → perception buffering/framing/fusion →
safe-mode gate → tone selection), which does not depend on the clips being
intelligible. What synthetic clips **cannot** exercise (the real wake/ASR/vision
model paths, which need the installed libraries) is called out explicitly in the
tests and in `docs/STATE.md` rather than papered over.

## Audio (16 kHz mono int16 WAV — the rate the wake-word/ASR path expects)

| File | Stands in for | Shape |
|------|---------------|-------|
| `speech_hello.wav` | a spoken command phrase | 0.6 s voiced waveform (~120 Hz fundamental + harmonics under a 4 Hz syllable envelope); RMS ≈ 3800, well above the endpointer's ~550 silence floor |
| `silence.wav` | a silent capture window | 0.5 s near-zero dither; RMS ≈ 7, far below the silence floor |
| `noisy_garble.wav` | a garbled / unintelligible clip | 0.5 s loud broadband noise; RMS ≈ 8100, high energy but no voiced structure |

## Images (32×32 binary PPM, P6)

PPM is used because it is self-describing (dimensions live in the header) and
trivial to parse with no image library on the C++ side. The perception stub does
not decode pixels, so only the shape (`width*height*3` bytes) matters; the content
is a recognizable stand-in.

| File | Stands in for | Content |
|------|---------------|---------|
| `face_enrolled.ppm` | an enrolled-style face | centered bright skin oval with two eye spots |
| `face_unknown.ppm`  | an unknown face | a differently-placed, differently-toned oval |
| `no_face.ppm`       | a frame with no face | a smooth color gradient, no face-like structure |

## Keep them tiny

Total footprint is well under 100 KB. Do not add large media assets here — if a
future test needs a bigger or real recording, keep it out of the repo (fetch it
in CI or gate it behind the real-hardware bring-up) so the tree stays lean.

## Real-engine assets (Phase 11) — fetched, not committed

The Phase 11 real-engine tests (`tests/real_asr_test.cpp`, `real_vision_test.cpp`,
`real_tts_test.cpp`) need inputs the synthetic fixtures above deliberately can't
provide: a 32×32 tone isn't intelligible speech, and a 32×32 oval isn't a real
face YuNet can detect. Following the rule just above, those larger/real assets are
**fetched and SHA-256-verified at run time** by
[`scripts/fetch_real_engine_assets.sh`](../../scripts/fetch_real_engine_assets.sh),
**not committed**:

- **Speech:** synthesized on the fly by Piper (no committed audio; license-clean).
- **Faces:** two public-domain photographs of public figures and classic CV test
  images — Grace Hopper (US Navy) as the enrolled identity, the "astronaut" Eileen
  Collins (NASA) as an unknown identity. Real faces are kept out of the tree on
  purpose (privacy + the rule above), living only in the ephemeral CI/checkout dir.

The two negative/robustness cases still reuse the committed synthetic fixtures:
`silence.wav` and `noisy_garble.wav` (silence/garble need no real speech) and
`no_face.ppm` (OpenCV decodes P6 PPM; a 32×32 frame → YuNet finds no face).

## Wake-word fixtures (Phase 13) — synthesized, not committed

The real wake-word test (`tests/real_wakeword_test.cpp`) needs speech, so — exactly
like the ASR speech clip above — the two speech clips are **synthesized on the fly
by Piper in CI**, not committed:

- **Positive — the wake phrase:** `wake_hey_jarvis.wav`, Piper synthesizing
  "hey jarvis". This is a **fair, in-distribution** positive: openWakeWord's own
  training data is Piper-TTS-synthesized speech, so a Piper "hey jarvis" clip is the
  kind of audio the model was trained to fire on. Passed to the test via
  `$ECHO_WAKE_CLIP`. (This phase ships the pretrained `hey_jarvis` model rather than
  a custom "Hey ECHO" — training a custom wake word is a documented follow-up, see
  `MANIFEST.md`.)
- **Negative — ordinary speech that is NOT the wake word:** `not_wake_speech.wav`,
  Piper synthesizing a plain sentence ("what time is the next train to boston").
  This tests for **false accepts on real speech**, not just on silence. Passed via
  `$ECHO_NOTWAKE_CLIP`.

The other two negatives reuse the committed synthetic fixtures `silence.wav` and
`noisy_garble.wav` (a wake detector must not fire on silence or garble either).

## Noise beds (Phase 19) — generated + checksum-verified, not committed

The audio-robustness test (`tests/audio_robustness_test.cpp`) needs *noise* to mix
into the clean clips above, so it can measure how the real ASR/wake-word engines
degrade under it. Those noise sources are three synthetic **beds** produced by
[`make_noise_fixtures.py`](make_noise_fixtures.py):

| Bed | Stands in for | Character |
|-----|---------------|-----------|
| `hum.wav` | a fan / AC / the device itself | steady 100 Hz hum + faint hiss — mostly low-frequency, so the pre-processor's high-pass measurably helps here |
| `transient.wav` | a door, a dropped dish | near-silence punctuated by short loud bursts — non-stationary |
| `babble.wav` | a second person / a TV | a buzzy competing talker overlapping the wearer's speech — the hard case a single mic cannot clean up |

Unlike the clips above, `make_noise_fixtures.py` uses **integer-only** arithmetic,
so the bytes are identical on every platform and can be **checksum-verified** (the
SHA-256s are pinned in [`MANIFEST.md`](../../MANIFEST.md); CI regenerates and
verifies them fail-closed, exactly like a fetched model). They are **not committed**
(128 KB each — kept out of the tree like the Piper clips) and are generated at run
time into the CI asset dir, passed to the test via `ECHO_NOISE_BEDS_DIR`.

The **mixing** (bed × clip at a target SNR) happens in-test via
[`tests/audio_mix.hpp`](../audio_mix.hpp) — one implementation, no committed noisy
audio to drift. SNR convention: `20·log10(rms_speech / rms_noise)` against whole-clip
RMS; levels measured are clean / +10 dB / 0 dB / −5 dB. What synthetic beds mixed
into a clean TTS clip still **cannot** exercise — a real mic's frequency response,
echo off real walls, a real human moving — stays the open live-hardware gap in
`docs/STATE.md`.

**On error rates (honest).** Wake-word detection is probabilistic — a real model has
nonzero false-accept and false-reject rates. The test asserts on the model's *actual*
behavior against these clips (peak score on the wake clip clears the threshold; peak
score on each non-wake clip stays below it), prints every observed score, and is not
tuned to manufacture a perfect separation the model cannot honestly deliver. What it
still cannot exercise — a real human, in a real room, with real background noise — is
called out in `docs/STATE.md` as the one remaining live-hardware gap.
