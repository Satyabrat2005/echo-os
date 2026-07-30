## Phase 10 — Core pipeline test harness (fixture-driven)

Phase 9's coverage audit exposed an inversion: the newest plumbing (apps
framework/appkit, 70 %+) was the best-tested part of the tree, while the actual
product — the core loop that recognizes a face or transcribes speech — was the
least-tested (`perception` 7 %; `sensor-pipeline` / `voice-ui` / `companion-sync`
at 0 %). This phase closes the part of that gap that is **fully verifiable
without live hardware**: a fixture-driven harness that feeds recorded,
checked-in audio/image data through the **real** core modules and asserts on the
output — the same spirit as `apps_smoke.cpp`, but for the core loop.

It is not a substitute for the live human run (still gated on real
mic/webcam/models); it is the piece that was achievable now, aimed squarely at
the modules Phase 9's data flagged.

### What's added

- **`tests/fixtures/`** — tiny, deterministic, **synthetic** fixtures (< 100 KB
  total): three 16 kHz WAV clips (voiced speech-shaped, silence, loud garble) and
  three 32×32 PPM images (enrolled-style face, unknown face, no face). A committed
  generator (`make_fixtures.py`) reproduces the bytes exactly; a
  [`README`](tests/fixtures/README.md) documents provenance and why synthetic is
  sufficient for stub-engine mode.
- **`echo-sensor`** — feeds fixture bytes through the **real** `SensorPipeline` +
  SPSC ring buffer via a real `ISensorSource` (not the no-op stub factories):
  FIFO order, bounded/non-blocking overflow, zero-copy view integrity, camera
  dimensions, start/stop lifecycle.
- **`echo-perception`** — runs fixtures through the **real** `PerceptionEngine`
  in stub-engine mode: modality routing, the wake-framing buffer, the
  weakest-link confidence fusion, and the input-validation guards. The
  wake-gated ASR branch and the real model paths are **explicitly asserted as an
  uncovered gap**, not padded.
- **`echo-voice`** — synthetic `Response` objects through the real
  `to_utterance()` tone mapping (safe-mode → reassuring, normal → neutral, keyed
  off kind not confidence) and the stub shell, minus SDL playback.
- **`echo-companion`** — real alert/status/firmware construction + lifecycle, and
  Phase 2's privacy guarantee ("no transport path accepts a `SensorFrame`") as an
  actual **compile-time `static_assert`**, not a comment.
- **`echo-e2e-fixture`** — the headline test: a fixture clip through
  sensor-pipeline → perception → cognitive-core → voice-ui in sequence. Asserts
  the safety-critical safe-mode fallback end-to-end, across both the
  low-confidence gate and the confident-but-no-LLM branch (distinguished by the
  confidence carried through).

All five are framework-free (shared `check.hpp`), read fixtures via
`ECHO_FIXTURES_DIR`, and are wired into CTest so Phase 7's CI runs them
automatically. Full suite: **9/9 CTest suites green**.

### Honest coverage movement (re-measured, `-DECHO_COVERAGE=ON`)

| Module | Was | Now |
|--------|----:|----:|
| `companion-sync` | 0 % | **96 %** |
| `voice-ui` | 0 % | **91 %** |
| `cognitive-core` | 67 % | **84 %** |
| `perception` | 7 % | **69 %** |
| `sensor-pipeline` | 0 % | **42 %** (real pipeline at 85 %; stub factories + SDL/OpenCV capture stay 0 %) |

Overall ≈40 % → **≈44 %** lines, with the gain concentrated on the core loop.
`docs/STATE.md`'s coverage table is updated to match.

### Constraints honored

- **Real code paths, not mocks** — every test drives production code; where the
  real logic genuinely can't be reached without the installed libraries
  (Porcupine/whisper/OpenCV/llama.cpp, SDL/Piper), the gap is stated plainly in
  the tests and STATE.md rather than reported as coverage.
- **Fixtures stay tiny and documented** — no large media assets.
- **CI structure untouched** — Phases 7/8 already own that; this just gives CI
  more real code to run.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
