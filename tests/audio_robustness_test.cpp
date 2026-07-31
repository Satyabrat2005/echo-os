// ECHO OS — REAL audio-robustness verification under noise (Phase 19).
//
// Every ASR / wake-word test before this one fed the real engines CLEAN audio: a
// Piper-synthesized phrase over synthetic silence. That proved the engines work at
// all, but never asked what a real room does to them — background hum, a door
// clattering, a second person talking. This target measures exactly that: it mixes
// three representative noise beds into the clean speech/wake clips at several
// DEFINED SNR levels and records the real engines' ACTUAL behaviour at each level.
//
// This is deliberately a MEASUREMENT, not a pass/fail on accuracy. Asserting that a
// real ASR/wake-word system stays accurate under −5 dB noise would be dishonest, so
// this file does not. What it DOES assert is bounded and honest:
//   * nothing crashes and every confidence/score stays finite and in-range;
//   * the real engines still work on the CLEAN clip (regression floor);
//   * the SNR estimator tracks the injected noise (monotone as SNR drops);
//   * the noise-driven confidence gate FIRES when it should — heavily-degraded
//     audio lands below the 0.72 safe-mode threshold, so the system asks again
//     calmly instead of surfacing a confident wrong answer (Phase 19 constraint #4);
//   * the basic pre-processing pass is applied and its effect is measured and
//     printed — whether it helps or not, honestly (not tuned to look good).
//
// Built only with -DECHO_WITH_WHISPER and/or -DECHO_WITH_OPENWAKEWORD; runs in the
// real-engines CI job. Self-skips (exit 0, loud) when a flag is on but the model or
// the clean clip / noise beds aren't present.

#if !defined(ECHO_WITH_WHISPER) && !defined(ECHO_WITH_OPENWAKEWORD)
#error "audio_robustness_test requires ECHO_WITH_WHISPER and/or ECHO_WITH_OPENWAKEWORD"
#endif

#include "echo/audio_dsp.hpp"
#include "echo/config.hpp"
#include "echo/result.hpp"

#include "audio_mix.hpp"
#include "check.hpp"
#include "fixture_io.hpp"

#if defined(ECHO_WITH_WHISPER)
#include "asr.hpp"
#endif
#if defined(ECHO_WITH_OPENWAKEWORD)
#include "wake_word.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using echo::test::load_wav_path;
using echo::test::mix_at_snr;
using echo::test::WavClip;

namespace {

// The cognitive core's safe-mode confidence gate (cognitive-core.hpp). Duplicated as
// a constant here so this test's claim — "noisy audio drops below the gate" — is
// explicit and self-contained.
constexpr float kSafeModeGate = 0.72f;

// openWakeWord's documented example detection threshold (see real_wakeword_test.cpp).
constexpr float kWakeThreshold = 0.5f;

// A "very large SNR" sentinel meaning "mix no noise": the clean baseline row.
constexpr float kCleanLevel = 1000.0f;

struct SnrLevel { const char* name; float snr_db; };
// The defined noise levels this phase measures at.
const SnrLevel kLevels[] = {
    {"clean", kCleanLevel},
    {"+10dB", 10.0f},
    {" 0dB", 0.0f},
    {"-5dB", -5.0f},
};

const char* env_or_null(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::size_t word_char_count(const std::string& s) {
    return static_cast<std::size_t>(
        std::count_if(s.begin(), s.end(),
                      [](unsigned char c) { return std::isalnum(c) != 0; }));
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Linear resample to 16 kHz mono (Piper voices are 22.05 kHz). Adequate for these
// checks on clean/near-clean speech; matches real_wakeword_test.cpp's resampler.
std::vector<std::int16_t> to_16k(const std::vector<std::int16_t>& in, int sr) {
    if (sr == 16000 || in.empty()) return in;
    const double ratio = 16000.0 / static_cast<double>(sr);
    const std::size_t out_n = static_cast<std::size_t>(static_cast<double>(in.size()) * ratio);
    std::vector<std::int16_t> out(out_n);
    const std::size_t last = in.size() - 1;
    for (std::size_t i = 0; i < out_n; ++i) {
        const double src = static_cast<double>(i) / ratio;
        const std::size_t i0 = static_cast<std::size_t>(src);
        const double frac = src - static_cast<double>(i0);
        const double a = in[std::min(i0, last)];
        const double b = in[std::min(i0 + 1, last)];
        out[i] = static_cast<std::int16_t>(std::lround(a + (b - a) * frac));
    }
    return out;
}

struct Bed { std::string name; std::vector<std::int16_t> samples; };

// Load the three noise beds from ECHO_NOISE_BEDS_DIR (all resampled to 16 kHz). An
// empty result means the beds weren't provided; the caller self-skips.
std::vector<Bed> load_beds() {
    std::vector<Bed> beds;
    const char* dir = env_or_null("ECHO_NOISE_BEDS_DIR");
    if (!dir) return beds;
    for (const char* nm : {"hum", "transient", "babble"}) {
        const std::string path = std::string(dir) + "/" + nm + ".wav";
        if (!std::filesystem::exists(path)) continue;
        WavClip c = load_wav_path(path);
        if (c.ok) beds.push_back(Bed{nm, to_16k(c.samples, c.sample_rate)});
    }
    return beds;
}

// ---------------------------------------------------------------------------
// ASR degradation (real whisper.cpp)
// ---------------------------------------------------------------------------
#if defined(ECHO_WITH_WHISPER)
void measure_asr(const std::vector<std::int16_t>& clean16, const std::vector<Bed>& beds) {
    using echo::perception::AsrResult;
    using echo::perception::make_asr;

    const std::string model = echo::config::whisper_model();
    if (!std::filesystem::exists(model)) {
        std::printf("[audio-robustness] SKIP ASR: whisper model not found at \"%s\".\n",
                    model.c_str());
        return;
    }
    auto asr = make_asr();
    CHECK(asr->initialize() == echo::Status::Ok);

    // Expected content words in the CI phrase ("the quick brown fox ...").
    auto expected_hit = [](const std::string& t) {
        return contains(t, "quick") || contains(t, "brown") || contains(t, "fox") ||
               contains(t, "jump") || contains(t, "over") || contains(t, "lazy") ||
               contains(t, "dog") || contains(t, "the");
    };

    std::printf("\n=== ASR (whisper tiny.en) accuracy vs. SNR ===\n");
    std::printf("  %-10s %-10s | %-32s | %s\n", "bed", "snr", "raw (words,hit,conf,snrEst)",
                "preprocessed (words,hit,conf,snrEst)");

    // Regression floor: the clean clip must still be recognized.
    {
        AsrResult r = asr->transcribe(clean16.data(), clean16.size(), 16000);
        const std::string t = to_lower(r.text);
        std::printf("  clean baseline transcript: \"%s\" (conf=%.3f)\n", r.text.c_str(),
                    static_cast<double>(r.confidence));
        CHECK(!t.empty());
        CHECK(word_char_count(t) >= 2);
        CHECK(expected_hit(t));
        CHECK(r.confidence >= 0.0f && r.confidence <= 1.0f);
    }

    float worst_case_gate_conf = 1.0f;  // min folded confidence seen at -5dB
    for (const auto& bed : beds) {
        float prev_snr = echo::audio::kCleanSnrDb + 1.0f;
        for (const auto& lvl : kLevels) {
            std::vector<std::int16_t> noisy =
                (lvl.snr_db >= kCleanLevel) ? clean16 : mix_at_snr(clean16, bed.samples, lvl.snr_db);

            // Raw (no pre-processing).
            AsrResult r = asr->transcribe(noisy.data(), noisy.size(), 16000);
            const std::string t = to_lower(r.text);
            const float snr = echo::audio::estimate_snr_db(noisy.data(), noisy.size(), 16000);
            const float folded = std::min(r.confidence, echo::audio::noise_confidence(snr));

            // Pre-processed (the capture-path stage, applied offline here to compare).
            echo::audio::AudioPreprocessor pp;
            auto ppr = pp.process(noisy.data(), noisy.size(), 16000);
            AsrResult rp = asr->transcribe(ppr.samples.data(), ppr.samples.size(), 16000);
            const std::string tp = to_lower(rp.text);
            const float snrp = ppr.out_snr_db;

            std::printf("  %-10s %-10s | %2zu wc  hit=%d  c=%.2f  snr=%5.1f | "
                        "%2zu wc  hit=%d  c=%.2f  snr=%5.1f\n",
                        bed.name.c_str(), lvl.name,
                        word_char_count(t), expected_hit(t) ? 1 : 0,
                        static_cast<double>(r.confidence), static_cast<double>(snr),
                        word_char_count(tp), expected_hit(tp) ? 1 : 0,
                        static_cast<double>(rp.confidence), static_cast<double>(snrp));

            // Honest, bounded assertions:
            CHECK(r.confidence >= 0.0f && r.confidence <= 1.0f);
            CHECK(rp.confidence >= 0.0f && rp.confidence <= 1.0f);
            // The SNR estimator must track the injected noise: never estimate a noisier
            // mix as CLEANER than the level above it (small slack for coarseness).
            if (lvl.snr_db < kCleanLevel) CHECK(snr <= prev_snr + 3.0f);
            prev_snr = snr;

            if (lvl.snr_db <= -5.0f) worst_case_gate_conf = std::min(worst_case_gate_conf, folded);
        }
    }

    // The safety property (constraint #4): at the worst measured noise level the
    // noise-driven confidence has dropped BELOW the safe-mode gate — so the system
    // routes to a calm "ask again", it does not surface a confident wrong answer.
    std::printf("  -> worst-case (-5dB) folded confidence = %.3f (safe-mode gate %.2f)\n",
                static_cast<double>(worst_case_gate_conf), static_cast<double>(kSafeModeGate));
    CHECK(worst_case_gate_conf < kSafeModeGate);

    asr->shutdown();
}
#endif  // ECHO_WITH_WHISPER

// ---------------------------------------------------------------------------
// Wake-word degradation (real openWakeWord)
// ---------------------------------------------------------------------------
#if defined(ECHO_WITH_OPENWAKEWORD)
float wake_peak(const std::vector<std::int16_t>& s16) {
    using echo::perception::make_wake_word;
    using echo::perception::WakeResult;
    auto w = make_wake_word();
    if (w->initialize() != echo::Status::Ok) return -1.0f;
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

void measure_wakeword(const std::vector<std::int16_t>& wake16, const std::vector<Bed>& beds) {
    const std::string mel = echo::config::openwakeword_melspec();
    const std::string emb = echo::config::openwakeword_embedding();
    const std::string cls = echo::config::openwakeword_model();
    if (!std::filesystem::exists(mel) || !std::filesystem::exists(emb) ||
        !std::filesystem::exists(cls)) {
        std::printf("[audio-robustness] SKIP wake-word: openWakeWord model(s) not found.\n");
        return;
    }

    std::printf("\n=== Wake-word (openWakeWord 'hey jarvis') peak score vs. SNR ===\n");
    std::printf("  %-10s %-8s | %-10s | %s\n", "bed", "snr", "raw peak", "preprocessed peak");

    // Regression floor: the clean wake phrase must still fire.
    const float clean_peak = wake_peak(wake16);
    std::printf("  clean baseline peak = %.3f (threshold %.2f)\n",
                static_cast<double>(clean_peak), static_cast<double>(kWakeThreshold));
    CHECK(clean_peak >= 0.0f);
    CHECK(clean_peak >= kWakeThreshold);

    for (const auto& bed : beds) {
        for (const auto& lvl : kLevels) {
            std::vector<std::int16_t> noisy =
                (lvl.snr_db >= kCleanLevel) ? wake16 : mix_at_snr(wake16, bed.samples, lvl.snr_db);
            const float raw = wake_peak(noisy);
            echo::audio::AudioPreprocessor pp;
            auto ppr = pp.process(noisy.data(), noisy.size(), 16000);
            const float pre = wake_peak(ppr.samples);
            std::printf("  %-10s %-8s | %.3f      | %.3f\n", bed.name.c_str(), lvl.name,
                        static_cast<double>(raw), static_cast<double>(pre));
            // Bounded, honest: scores stay valid; we do NOT assert it fires under noise
            // (a real wake-word system legitimately misses under heavy noise).
            CHECK(raw >= -1.0f && raw <= 1.0f);
            CHECK(pre >= -1.0f && pre <= 1.0f);
        }
    }
}
#endif  // ECHO_WITH_OPENWAKEWORD

}  // namespace

int main() {
    const std::vector<Bed> beds = load_beds();
    if (beds.empty()) {
        std::printf("[audio-robustness] SKIP: no noise beds. Set ECHO_NOISE_BEDS_DIR to a dir "
                    "with hum.wav/transient.wav/babble.wav (CI generates + checksum-verifies "
                    "them; see tests/fixtures/make_noise_fixtures.py + MANIFEST.md).\n");
        return echo::test::report("audio-robustness");
    }

#if defined(ECHO_WITH_WHISPER)
    if (const char* wav = env_or_null("ECHO_ASR_SPEECH_WAV"); wav && std::filesystem::exists(wav)) {
        WavClip c = load_wav_path(wav);
        CHECK(c.ok);
        if (c.ok) measure_asr(to_16k(c.samples, c.sample_rate), beds);
    } else {
        std::printf("[audio-robustness] SKIP ASR: set ECHO_ASR_SPEECH_WAV to a clean speech "
                    "clip (CI synthesizes one with Piper).\n");
    }
#endif

#if defined(ECHO_WITH_OPENWAKEWORD)
    if (const char* wav = env_or_null("ECHO_WAKE_CLIP"); wav && std::filesystem::exists(wav)) {
        WavClip c = load_wav_path(wav);
        CHECK(c.ok);
        if (c.ok) measure_wakeword(to_16k(c.samples, c.sample_rate), beds);
    } else {
        std::printf("[audio-robustness] SKIP wake-word: set ECHO_WAKE_CLIP to a synthesized "
                    "\"hey jarvis\" clip (CI synthesizes one with Piper).\n");
    }
#endif

    return echo::test::report("audio-robustness");
}
