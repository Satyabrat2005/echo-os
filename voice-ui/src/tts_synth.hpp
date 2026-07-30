// ECHO OS voice-ui — internal TTS synthesis helpers (not a public API).
//
// Factored out of voice_ui.cpp so the Phase 11 real-engine test can drive the
// REAL Piper synthesis path (tone-scaled `--length_scale`, local `piper`
// subprocess) WITHOUT the SDL playback step — synthesis writes a WAV to disk and
// needs no audio device, so it runs on a headless CI runner. Playback (SDL) stays
// in PiperVoiceUi::speak() and is out of scope for CI (it needs real hardware).
//
// This header is intentionally under voice-ui/src (private): it exposes the two
// pieces the test needs without widening the public IVoiceUi contract.
#pragma once

#include "echo/voice/voice_ui.hpp"
#include "echo/result.hpp"

#include <string>

namespace echo::voice::detail {

#if defined(ECHO_WITH_PIPER)

// Map a Tone to Piper's `--length_scale` (higher = slower/warmer) so the
// reassuring/neutral distinction is an audible pacing difference, not just a
// label. Pure and side-effect free — the test asserts these values directly.
double length_scale_for(Tone t) noexcept;

// Synthesize `text` at `tone`'s pacing into the WAV file `out_wav`, by shelling
// out to the local `piper` binary (config::piper_binary()/piper_voice()). This is
// the file-producing half of speak(); it performs NO audio playback, so it is
// safe to call with no audio device present. Returns Status::Ok on a successful
// synthesis, Status::Unavailable on empty input or a piper failure.
Status piper_synthesize(const std::string& text, Tone tone, const std::string& out_wav);

#endif  // ECHO_WITH_PIPER

}  // namespace echo::voice::detail
