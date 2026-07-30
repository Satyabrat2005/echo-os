// ECHO OS — REAL wake-word engine verification (Phase 13).
//
// Unlike perception_test.cpp (which runs perception in STUB mode, where the
// wake-word never fires), this target is built ONLY with
// -DECHO_WITH_OPENWAKEWORD=ON and drives the real openWakeWord backend
// (perception/src/wake_word.cpp -> OpenWakeWordDetector) — a three-model ONNX
// pipeline on ONNX Runtime — against real model weights and real speech.
//
// What this proves: the real openWakeWord path compiles, links, loads the three
// pinned ONNX models, and behaves sanely on fixtures — the wake phrase clears the
// detection threshold; silence, garble, and ordinary non-wake speech stay below
// it. This closes the last "real engine untested in CI" gap that needs no account
// (openWakeWord is Apache-2.0; ONNX Runtime is MIT), unlike account-gated Porcupine.
//
// What this does NOT prove (kept honest, see docs/STATE.md): field robustness. The
// wake / non-wake clips are Piper-synthesized (openWakeWord's own training data IS
// Piper TTS, so a Piper "hey jarvis" is a fair, in-distribution positive — but it is
// still not a human in a noisy room), and silence/garble are the synthetic Phase 10
// fixtures. Wake-word detection is probabilistic with real, nonzero error rates; we
// assert on the model's ACTUAL behavior against these clips and print every score,
// rather than tuning the test to fake a perfect separation.

#if !defined(ECHO_WITH_OPENWAKEWORD)
#error "real_wakeword_test requires ECHO_WITH_OPENWAKEWORD=ON — it verifies the real openWakeWord engine"
#endif

#include "wake_word.hpp"  // perception/src (private): make_wake_word()/IWakeWord

#include "echo/config.hpp"
#include "echo/result.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace echo;
using echo::perception::IWakeWord;
using echo::perception::make_wake_word;
using echo::perception::WakeResult;
using echo::test::load_wav;
using echo::test::load_wav_path;
using echo::test::WavClip;

namespace {

// openWakeWord's documented example detection threshold. The detector's own
// `detected` flag uses the same default (overridable via $ECHO_OWW_THRESHOLD); we
// assert against it directly so the test's pass/fail policy is explicit here.
constexpr float kThreshold = 0.5f;

const char* env_or_null(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

// openWakeWord expects 16 kHz mono. Piper voices synthesize at 22.05 kHz, so any
// clip that isn't already 16 kHz is linearly resampled here. Linear interpolation
// is more than adequate for a wake-word check on clean TTS speech.
std::vector<std::int16_t> to_16k_mono(const WavClip& clip) {
    if (clip.sample_rate == 16000) return clip.samples;
    const double ratio = 16000.0 / static_cast<double>(clip.sample_rate);
    const std::size_t out_n =
        static_cast<std::size_t>(static_cast<double>(clip.samples.size()) * ratio);
    std::vector<std::int16_t> out(out_n);
    const std::size_t last = clip.samples.empty() ? 0 : clip.samples.size() - 1;
    for (std::size_t i = 0; i < out_n; ++i) {
        const double src = static_cast<double>(i) / ratio;
        const std::size_t i0 = static_cast<std::size_t>(src);
        const double frac = src - static_cast<double>(i0);
        const double a = clip.samples[std::min(i0, last)];
        const double b = clip.samples[std::min(i0 + 1, last)];
        out[i] = static_cast<std::int16_t>(std::lround(a + (b - a) * frac));
    }
    return out;
}

// Feed a clip through a FRESH detector (its own rolling buffer, so clips never
// bleed into each other) and return the peak wake score observed. A fresh detector
// per clip also re-checks that the models load. Returns -1 on init failure.
float peak_score(const std::vector<std::int16_t>& s16) {
    auto w = make_wake_word();
    if (w->initialize() != Status::Ok) return -1.0f;
    const int fl = w->frame_length();
    float peak = 0.0f;
    for (std::size_t off = 0; off + static_cast<std::size_t>(fl) <= s16.size();
         off += static_cast<std::size_t>(fl)) {
        WakeResult r = w->process(s16.data() + off, static_cast<std::size_t>(fl));
        if (r.confidence >= 0.0f && r.confidence <= 1.0f) peak = std::max(peak, r.confidence);
    }
    w->shutdown();
    return peak;
}

// --- Positive: the wake phrase must clear the threshold ----------------------
// The clip is Piper-synthesized "hey jarvis" in CI (path in $ECHO_WAKE_CLIP). If it
// isn't provided (a local build without Piper), this check self-skips and says so —
// the negative checks below still exercise the real model.
void test_wake_phrase_triggers() {
    const char* wav = env_or_null("ECHO_WAKE_CLIP");
    if (!wav || !std::filesystem::exists(wav)) {
        std::printf("  [skip] wake-phrase check: set $ECHO_WAKE_CLIP to a synthesized "
                    "\"hey jarvis\" WAV (CI synthesizes one with Piper)\n");
        return;
    }
    WavClip clip = load_wav_path(wav);
    CHECK(clip.ok);
    if (!clip.ok) return;
    const float peak = peak_score(to_16k_mono(clip));
    std::printf("  wake  \"hey jarvis\" -> peak score = %.3f (threshold %.2f)\n",
                static_cast<double>(peak), static_cast<double>(kThreshold));
    CHECK(peak >= 0.0f);            // the model initialized and ran
    CHECK(peak >= kThreshold);      // ...and actually fired on the wake phrase
}

// --- Negative: ordinary non-wake speech must NOT trigger ---------------------
// Tests for false accepts on REAL speech, not just on silence. Clip is Piper-
// synthesized in CI (path in $ECHO_NOTWAKE_CLIP); self-skips if not provided.
void test_ordinary_speech_does_not_trigger() {
    const char* wav = env_or_null("ECHO_NOTWAKE_CLIP");
    if (!wav || !std::filesystem::exists(wav)) {
        std::printf("  [skip] non-wake-speech check: set $ECHO_NOTWAKE_CLIP to a "
                    "synthesized non-wake sentence WAV (CI synthesizes one with Piper)\n");
        return;
    }
    WavClip clip = load_wav_path(wav);
    CHECK(clip.ok);
    if (!clip.ok) return;
    const float peak = peak_score(to_16k_mono(clip));
    std::printf("  speech (non-wake) -> peak score = %.3f (must stay < %.2f)\n",
                static_cast<double>(peak), static_cast<double>(kThreshold));
    CHECK(peak >= 0.0f);
    CHECK(peak < kThreshold);       // no false accept on ordinary speech
}

// --- Negative: silence must NOT trigger (committed synthetic fixture) --------
void test_silence_does_not_trigger() {
    WavClip clip = load_wav("silence.wav");
    CHECK(clip.ok);
    const float peak = peak_score(to_16k_mono(clip));
    std::printf("  silence           -> peak score = %.3f (must stay < %.2f)\n",
                static_cast<double>(peak), static_cast<double>(kThreshold));
    CHECK(peak >= 0.0f);
    CHECK(peak < kThreshold);
}

// --- Negative: loud garble must NOT trigger (committed synthetic fixture) -----
void test_garble_does_not_trigger() {
    WavClip clip = load_wav("noisy_garble.wav");
    CHECK(clip.ok);
    const float peak = peak_score(to_16k_mono(clip));
    std::printf("  garble            -> peak score = %.3f (must stay < %.2f)\n",
                static_cast<double>(peak), static_cast<double>(kThreshold));
    CHECK(peak >= 0.0f);
    CHECK(peak < kThreshold);
}

}  // namespace

int main() {
    // Built with the flag ON but the models aren't downloaded (a local build without
    // setup): skip loudly rather than fail — CI always provides them.
    const std::string mel = config::openwakeword_melspec();
    const std::string emb = config::openwakeword_embedding();
    const std::string cls = config::openwakeword_model();
    if (!std::filesystem::exists(mel) || !std::filesystem::exists(emb) ||
        !std::filesystem::exists(cls)) {
        std::printf("[real-wakeword] SKIP: openWakeWord model(s) not found "
                    "(melspec=\"%s\", embedding=\"%s\", classifier=\"%s\"). "
                    "Fetch them or set $ECHO_OWW_* (see MANIFEST.md).\n",
                    mel.c_str(), emb.c_str(), cls.c_str());
        return echo::test::report("real-wakeword");
    }

    test_wake_phrase_triggers();
    test_ordinary_speech_does_not_trigger();
    test_silence_does_not_trigger();
    test_garble_does_not_trigger();

    return echo::test::report("real-wakeword");
}
