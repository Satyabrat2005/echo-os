// ECHO OS — voice-ui fixture tests.
//
// Phase 10, deliverable #4. Feeds synthetic cognitive::Response objects through
// the REAL voice-ui shell logic — the to_utterance() tone/text mapping and the
// StubVoiceUi shell (initialize/speak/play_earcon/barge_in/shutdown) — minus the
// actual SDL audio playback (which the Piper backend needs and CI doesn't have).
// Asserts the right text + tone is chosen for each response kind. voice-ui was at
// 0 % coverage before this phase.
#include "echo/voice/voice_ui.hpp"
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/result.hpp"

#include "check.hpp"

#include <string>

using namespace echo;
using echo::voice::Tone;

namespace {

cognitive::Response make_response(cognitive::ResponseKind kind, std::string text,
                                  float conf) {
    cognitive::Response r;
    r.kind           = kind;
    r.text           = std::move(text);
    r.confidence     = Confidence{conf};
    r.flag_caregiver = (kind == cognitive::ResponseKind::SafeMode);
    return r;
}

// A normal (confident) response speaks its text in a neutral tone.
void test_normal_response_is_neutral() {
    auto u = voice::to_utterance(
        make_response(cognitive::ResponseKind::Normal, "Now playing Kind of Blue.", 0.88f));
    CHECK(u.tone == Tone::Neutral);
    CHECK(u.text == "Now playing Kind of Blue.");
}

// A safe-mode response is spoken in the reassuring tone — the tone IS part of the
// reassurance for a distressed/low-confidence context (voice_ui.hpp rationale).
void test_safe_mode_response_is_reassuring() {
    const std::string safe = "I'm not quite sure right now. Let's take a moment.";
    auto u = voice::to_utterance(
        make_response(cognitive::ResponseKind::SafeMode, safe, 0.20f));
    CHECK(u.tone == Tone::Reassuring);
    CHECK(u.text == safe);
}

// The mapping keys strictly off ResponseKind, not confidence or text: a low
// confidence value on a Normal response is still Neutral (the gate already made
// the safe/normal call upstream; voice-ui only renders it).
void test_tone_keys_off_kind_not_confidence() {
    auto low_conf_normal = voice::to_utterance(
        make_response(cognitive::ResponseKind::Normal, "Two messages.", 0.05f));
    CHECK(low_conf_normal.tone == Tone::Neutral);

    auto high_conf_safe = voice::to_utterance(
        make_response(cognitive::ResponseKind::SafeMode, "Let's take a moment.", 0.99f));
    CHECK(high_conf_safe.tone == Tone::Reassuring);
}

// The real shell logic runs without audio hardware: the stub voice UI accepts an
// utterance, an empty utterance, an earcon, a barge-in, and shuts down cleanly.
void test_voice_ui_shell_runs_without_audio() {
    auto ui = voice::make_voice_ui();
    CHECK(ui->initialize() == Status::Ok);

    CHECK(ui->speak(voice::to_utterance(
              make_response(cognitive::ResponseKind::SafeMode,
                            "Let's take a moment.", 0.2f))) == Status::Ok);
    CHECK(ui->speak(voice::Utterance{}) == Status::Ok);   // empty text is a no-op, not an error
    CHECK(ui->play_earcon("wake") == Status::Ok);
    ui->barge_in();                                       // interrupt path
    ui->shutdown();
}

}  // namespace

int main() {
    test_normal_response_is_neutral();
    test_safe_mode_response_is_reassuring();
    test_tone_keys_off_kind_not_confidence();
    test_voice_ui_shell_runs_without_audio();
    return echo::test::report("voice-ui");
}
