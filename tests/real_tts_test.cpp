// ECHO OS — REAL TTS engine verification (Phase 11).
//
// voice_ui_test.cpp exercises the STUB voice UI (it logs what it would speak).
// This target is built ONLY with -DECHO_WITH_PIPER=ON and drives the real Piper
// synthesis path (voice-ui/src/voice_ui.cpp -> detail::piper_synthesize), which
// shells out to the local `piper` binary and a real voice model to produce a WAV.
//
// It uses the SDL-free synthesis half of speak() (tts_synth.hpp), so it runs on a
// headless CI runner: PLAYBACK (SDL, real speakers) is deliberately out of scope
// — that needs hardware and a human ear.
//
// What this proves: the real Piper path compiles, links, runs the binary+voice,
// and emits valid, non-silent audio; and voice-ui's tone -> pacing mapping
// (length_scale_for) produces an actually-slower reassuring clip than the neutral
// one for the same text.
//
// What this does NOT assert: audio *content* / intelligibility — that isn't
// meaningfully testable without a human ear (STATE.md is honest about this). We
// assert the WAV is well-formed, non-silent, and of a plausible duration.

#if !defined(ECHO_WITH_PIPER)
#error "real_tts_test requires ECHO_WITH_PIPER=ON — it verifies the real Piper engine"
#endif

#include "tts_synth.hpp"  // voice-ui/src (private): detail::length_scale_for / piper_synthesize

#include "echo/voice/voice_ui.hpp"
#include "echo/config.hpp"
#include "echo/result.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace echo;
using echo::voice::Tone;
using echo::voice::detail::length_scale_for;
using echo::voice::detail::piper_synthesize;
using echo::test::load_wav_path;
using echo::test::WavClip;

namespace {

// A short, fixed phrase (task: "a short fixed string").
const std::string kPhrase = "ECHO is ready to help you.";

double rms(const WavClip& c) {
    if (c.samples.empty()) return 0.0;
    double acc = 0.0;
    for (std::int16_t s : c.samples) acc += static_cast<double>(s) * s;
    return std::sqrt(acc / static_cast<double>(c.samples.size()));
}

// Assert a synthesized clip is a valid, non-silent WAV of plausible length.
void expect_valid_speech_wav(const WavClip& c, const char* label) {
    CHECK(c.ok);                       // decoded as 16-bit PCM, sr>0, non-empty
    CHECK(c.channels >= 1);
    CHECK(c.sample_rate >= 8000);      // a real voice model rate (amy medium = 22050)
    const double r = rms(c);
    const double seconds =
        c.sample_rate > 0 ? static_cast<double>(c.samples.size()) /
                                (c.sample_rate * (c.channels > 0 ? c.channels : 1))
                          : 0.0;
    std::printf("  %s: %d Hz, %d ch, %.2fs, rms=%.0f\n", label, c.sample_rate,
                c.channels, seconds, r);
    CHECK(r > 50.0);                   // non-silent (speech RMS is thousands; 50 is a floor)
    CHECK(seconds > 0.3 && seconds < 20.0);  // a short phrase, not empty and not runaway
}

// The tone -> Piper --length_scale mapping IS voice-ui's prosody policy; assert
// the exact values the shell uses (reassuring is slower/warmer than neutral).
void test_tone_length_scale_mapping() {
    CHECK(length_scale_for(Tone::Neutral) == 1.0);
    CHECK(length_scale_for(Tone::Reassuring) == 1.15);
    CHECK(length_scale_for(Tone::Alert) == 0.95);
    // Reassuring must be strictly slower than neutral for the pacing test below.
    CHECK(length_scale_for(Tone::Reassuring) > length_scale_for(Tone::Neutral));
}

}  // namespace

int main() {
    // Built with Piper ON but the voice model isn't downloaded (a local build
    // without setup) -> skip loudly. CI always provides binary + voice.
    const std::string voice = config::piper_voice();
    if (!std::filesystem::exists(voice)) {
        std::printf("[real-tts] SKIP: Piper voice model not found at \"%s\". "
                    "Download a voice or set $ECHO_PIPER_VOICE (see README).\n",
                    voice.c_str());
        // Still verify the pure mapping — it needs no engine.
        test_tone_length_scale_mapping();
        return echo::test::report("real-tts");
    }

    test_tone_length_scale_mapping();

    const std::string tmp = std::filesystem::temp_directory_path().string();
    const std::string neutral_wav    = tmp + "/echo_real_tts_neutral.wav";
    const std::string reassuring_wav = tmp + "/echo_real_tts_reassuring.wav";

    // Real synthesis for BOTH tones (voice-ui's neutral + reassuring settings).
    CHECK(piper_synthesize(kPhrase, Tone::Neutral, neutral_wav) == Status::Ok);
    CHECK(piper_synthesize(kPhrase, Tone::Reassuring, reassuring_wav) == Status::Ok);

    WavClip neutral    = load_wav_path(neutral_wav);
    WavClip reassuring = load_wav_path(reassuring_wav);

    expect_valid_speech_wav(neutral, "neutral");
    expect_valid_speech_wav(reassuring, "reassuring");

    // The reassuring tone uses a larger length_scale (1.15 vs 1.0), so the SAME
    // text must synthesize to a longer clip — proving the tone setting changes the
    // real audio, not just a label. Same rate/channels, so compare sample counts.
    if (neutral.ok && reassuring.ok) {
        std::printf("  pacing: neutral=%zu samples, reassuring=%zu samples\n",
                    neutral.samples.size(), reassuring.samples.size());
        CHECK(reassuring.samples.size() > neutral.samples.size());
    }

    return echo::test::report("real-tts");
}
