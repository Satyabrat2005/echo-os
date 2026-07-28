#include "echo/voice/voice_ui.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include <cstdio>

namespace echo::voice {

Utterance to_utterance(const cognitive::Response& response) {
    Utterance u;
    u.text = response.text;
    u.tone = (response.kind == cognitive::ResponseKind::SafeMode)
                 ? Tone::Reassuring
                 : Tone::Neutral;
    return u;
}

namespace {

const char* to_string(Tone t) noexcept {
    switch (t) {
        case Tone::Neutral:    return "neutral";
        case Tone::Reassuring: return "reassuring";
        case Tone::Alert:      return "alert";
    }
    return "neutral";
}

class StubVoiceUi final : public IVoiceUi {
public:
    Status initialize() override {
        // TODO(voice): load quantized TTS voice, allocate double audio buffers,
        // unmute PAM8403 amp, prime the output DMA.
        log_info("voice", "voice UI initialized (stub TTS)");
        return Status::Ok;
    }

    Status speak(const Utterance& utterance) override {
        StageTimer timer(Stage::VoiceOutput);
        (void)timer;
        // TODO(voice): synthesize into the back buffer and swap; here we just log.
        char buf[256];
        std::snprintf(buf, sizeof(buf), "speak[%s]: %.*s",
                      to_string(utterance.tone),
                      static_cast<int>(utterance.text.size()), utterance.text.data());
        log_info("voice", buf);
        return Status::Ok;
    }

    Status play_earcon(std::string_view name) override {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "earcon: %.*s",
                      static_cast<int>(name.size()), name.data());
        log_info("voice", buf);
        return Status::Ok;
    }

    void barge_in() override { log_debug("voice", "barge-in: stopping playback"); }

    void shutdown() override { log_info("voice", "voice UI shut down"); }
};

}  // namespace

std::unique_ptr<IVoiceUi> make_voice_ui() {
    return std::make_unique<StubVoiceUi>();
}

}  // namespace echo::voice
