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
