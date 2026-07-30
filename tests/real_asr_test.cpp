// ECHO OS — REAL ASR engine verification (Phase 11).
//
// Unlike perception_test.cpp (which runs the perception engine in STUB mode,
// where ASR always returns empty), this target is built ONLY with
// -DECHO_WITH_WHISPER=ON and drives the real whisper.cpp adapter
// (perception/src/asr.cpp -> WhisperAsr) against real model weights.
//
// What this proves: the real whisper.cpp path compiles, links, loads a real GGML
// model (tiny.en), and transcribes audio without crashing — the slice of the
// "real engines untested" gap that needs no account and is small enough for CI.
//
// What this does NOT prove (kept honest per STATE.md): real-world robustness. The
// silence/garble clips are the clean SYNTHETIC Phase 10 fixtures, and the speech
// clip is TTS-synthesized, not a human in a noisy room. We assert the engine runs
// and behaves sanely (silence -> near-empty, garble -> no crash, clear speech ->
// non-empty with expected words), not that tiny.en is accurate under field noise.
//
// Assertions are deliberately lenient on content: exact speech-recognition string
// matches are fragile, so we assert on presence/rough content, never brittle
// exact strings.

#if !defined(ECHO_WITH_WHISPER)
#error "real_asr_test requires ECHO_WITH_WHISPER=ON — it verifies the real whisper.cpp engine"
#endif

#include "asr.hpp"  // perception/src (private): the real make_asr()/WhisperAsr

#include "echo/config.hpp"
#include "echo/result.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace echo;
using echo::perception::AsrResult;
using echo::perception::IAsr;
using echo::perception::make_asr;
using echo::test::load_wav;
using echo::test::WavClip;

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Count [A-Za-z0-9] characters — used to gauge "near-empty" without depending on
// whitespace/marker formatting (whisper emits "[BLANK_AUDIO]", "(silence)", etc.
// on non-speech; those are not recognized words).
std::size_t word_char_count(const std::string& s) {
    return static_cast<std::size_t>(
        std::count_if(s.begin(), s.end(),
                      [](unsigned char c) { return std::isalnum(c) != 0; }));
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

const char* env_or_null(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

// The real whisper.cpp adapter, initialized once against the real tiny.en model.
// initialize() returns Ok only when the model file actually loads.
std::unique_ptr<IAsr> g_asr;

// --- Silence: a silent window must not produce a spoken sentence -------------
// whisper can occasionally hallucinate a short token on pure silence; that is a
// known limitation, so we assert "near-empty" (no full utterance), not strictly
// "". A generous cap catches a real mis-fire (a whole sentence) while tolerating
// a stray marker/token.
void test_silence_is_near_empty() {
    WavClip clip = load_wav("silence.wav");
    CHECK(clip.ok);
    AsrResult r = g_asr->transcribe(clip.samples.data(), clip.samples.size(), clip.sample_rate);
    CHECK(r.confidence >= 0.0f && r.confidence <= 1.0f);
    const std::size_t wc = word_char_count(r.text);
    if (wc > 24) {
        std::fprintf(stderr, "  silence transcript unexpectedly wordy (%zu chars): \"%s\"\n",
                     wc, r.text.c_str());
    }
    CHECK(wc <= 24);  // near-empty: not a transcribed sentence
}

// --- Garble: loud broadband noise must not crash the parser ------------------
// Extends Phase 10's robustness focus (odd input shouldn't break anything) to the
// REAL engine. We make no claim about the text; only that it ran and returned a
// well-formed, in-range result.
void test_garble_does_not_crash() {
    WavClip clip = load_wav("noisy_garble.wav");
    CHECK(clip.ok);
    AsrResult r = g_asr->transcribe(clip.samples.data(), clip.samples.size(), clip.sample_rate);
    CHECK(r.confidence >= 0.0f && r.confidence <= 1.0f);  // finite, in [0,1]
    // Reaching here without a crash/hang is the assertion.
    std::printf("  garble -> %zu word-char(s), conf=%.3f (no crash)\n",
                word_char_count(r.text), static_cast<double>(r.confidence));
}

// --- Clear speech: the engine actually recognizes words ----------------------
// The clip is TTS-synthesized in CI (Piper), path in $ECHO_ASR_SPEECH_WAV. Since
// it is clean, intelligible English of a known phrase, tiny.en should return a
// non-empty transcript containing at least one expected word. If the clip isn't
// provided (e.g. a local build without Piper), this asserts nothing and says so —
// the silence/garble robustness checks above still run against the real engine.
void test_clear_speech_is_recognized() {
    const char* wav = env_or_null("ECHO_ASR_SPEECH_WAV");
    if (!wav || !std::filesystem::exists(wav)) {
        std::printf("  [skip] clear-speech content check: set $ECHO_ASR_SPEECH_WAV to a "
                    "clear speech WAV (CI synthesizes one with Piper)\n");
        return;
    }
    // The CI clip is an absolute path (not under the fixtures dir), so read it
    // with the absolute-path loader rather than the fixtures-relative helper.
    WavClip clip = echo::test::load_wav_path(wav);
    CHECK(clip.ok);
    if (!clip.ok) return;

    AsrResult r = g_asr->transcribe(clip.samples.data(), clip.samples.size(), clip.sample_rate);
    const std::string text = to_lower(r.text);
    std::printf("  clear-speech transcript: \"%s\" (conf=%.3f)\n",
                r.text.c_str(), static_cast<double>(r.confidence));

    CHECK(!text.empty());
    CHECK(word_char_count(text) >= 2);
    // The CI phrase is "the quick brown fox jumps over the lazy dog". Assert rough
    // content: at least one expected token present (case-insensitive substring).
    // "the" is an almost-certain safety net; the content words make it meaningful.
    const bool any_expected =
        contains(text, "the") || contains(text, "quick") || contains(text, "brown") ||
        contains(text, "fox") || contains(text, "jump") || contains(text, "over") ||
        contains(text, "lazy") || contains(text, "dog");
    CHECK(any_expected);
    CHECK(r.confidence > 0.0f);  // a real recognition carries some token probability
}

}  // namespace

int main() {
    g_asr = make_asr();

    const std::string model = config::whisper_model();
    if (!std::filesystem::exists(model)) {
        // Built with the flag ON but the model isn't downloaded (a local build
        // without setup). Skip loudly rather than fail — CI always provides it.
        std::printf("[real-asr] SKIP: whisper model not found at \"%s\". "
                    "Download tiny.en or set $ECHO_WHISPER_MODEL (see README).\n",
                    model.c_str());
        return echo::test::report("real-asr");
    }

    // Model present -> it must load. A load failure here IS a real failure.
    CHECK(g_asr->initialize() == Status::Ok);

    test_silence_is_near_empty();
    test_garble_does_not_crash();
    test_clear_speech_is_recognized();

    g_asr->shutdown();
    return echo::test::report("real-asr");
}
