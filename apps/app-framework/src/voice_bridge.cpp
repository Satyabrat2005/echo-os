#include "echo/apps/framework/voice_bridge.hpp"
#include "echo/log.hpp"

namespace echo::apps {

namespace {

// Map the app-side tone vocabulary onto the core's voice::Tone. This is the
// entire surface where the two vocabularies meet — apps stay ignorant of
// voice-ui, and voice-ui stays ignorant of apps.
voice::Tone to_voice_tone(SpeechTone t) noexcept {
    switch (t) {
        case SpeechTone::Neutral:    return voice::Tone::Neutral;
        case SpeechTone::Reassuring: return voice::Tone::Reassuring;
        case SpeechTone::Alert:      return voice::Tone::Alert;
    }
    return voice::Tone::Neutral;
}

class VoiceBridge final : public IVoiceBridge {
public:
    explicit VoiceBridge(voice::IVoiceUi* ui) : ui_(ui) {}

    bool speak(std::string_view app_id, std::string_view text, SpeechTone tone) override {
        if (!ui_ || text.empty()) return false;
        // The core owns the audio channel; the bridge is best-effort. Here the
        // stub voice-ui always accepts, but the seam is where a real
        // implementation would defer to an in-flight safety utterance.
        (void)app_id;
        voice::Utterance u;
        u.text = std::string(text);
        u.tone = to_voice_tone(tone);
        return ui_->speak(u) == Status::Ok;
    }

private:
    voice::IVoiceUi* ui_;  // borrowed
};

}  // namespace

std::unique_ptr<IVoiceBridge> make_voice_bridge(voice::IVoiceUi* ui) {
    return std::make_unique<VoiceBridge>(ui);
}

}  // namespace echo::apps
