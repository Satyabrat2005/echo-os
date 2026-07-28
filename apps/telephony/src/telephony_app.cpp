#include "echo/apps/telephony/telephony_app.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::telephony {

namespace {

enum class CallState { Idle, Ringing, Connected, Ended };

const char* to_string(CallState s) {
    switch (s) {
        case CallState::Idle:      return "idle";
        case CallState::Ringing:   return "ringing";
        case CallState::Connected: return "connected";
        case CallState::Ended:     return "ended";
    }
    return "idle";
}

class TelephonyApp final : public IApp {
public:
    explicit TelephonyApp(bool mock) : mock_(mock) {
        meta_.id            = "telephony";
        meta_.name          = "Phone";
        meta_.required_caps = PermissionSet::of({Capability::Contacts});
        meta_.intents       = {"call", "answer", "hangup", "dial"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(telephony): connect to the paired phone over Bluetooth HFP.
            log_warn("telephony", "real Bluetooth phone bridge not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("telephony", "phone bridge ready (mock, no SIM — paired-phone model)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        if (cmd.intent == "call" || cmd.intent == "dial") {
            peer_  = cmd.slot("query", "your contact");
            state_ = CallState::Ringing;
            return frame("Calling " + peer_ + "…", hud::Glyph::Phone, hud::StatusKind::Working,
                         SpeechTone::Alert);
        }
        if (cmd.intent == "answer") {
            state_ = CallState::Connected;
            return frame(std::string("Connected") + (peer_.empty() ? "." : " to " + peer_ + "."),
                         hud::Glyph::Phone, hud::StatusKind::Success);
        }
        // hangup
        state_ = CallState::Ended;
        std::string who = peer_;
        peer_.clear();
        return frame(who.empty() ? "Call ended." : "Call with " + who + " ended.",
                     hud::Glyph::PhoneEnd, hud::StatusKind::Idle);
    }

    void shutdown() override { log_info("telephony", "phone bridge stopped"); }

private:
    AppResponse frame(std::string spoken, hud::Glyph glyph, hud::StatusKind status,
                      SpeechTone tone = SpeechTone::Neutral) {
        log_debug("telephony", to_string(state_));
        return AppResponse::say(std::move(spoken), tone)
            .show(hud::HudFrame{}.with_icon(glyph).with_status(status));
    }

    AppMetadata meta_;
    bool        mock_;
    CallState   state_ = CallState::Idle;
    std::string peer_;
};

}  // namespace

std::unique_ptr<IApp> make_telephony_app(bool mock) {
    return std::make_unique<TelephonyApp>(mock);
}

}  // namespace echo::apps::telephony
