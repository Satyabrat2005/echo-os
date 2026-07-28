#include "echo/apps/mail/mail_app.hpp"
#include "echo/log.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace echo::apps::mail {

namespace {

struct Message {
    const char* from;
    const char* subject;
};

// A small simulated inbox for mock mode.
constexpr std::array<Message, 3> kInbox = {{
    {"Dr. Alvarez",  "Appointment reminder for Thursday"},
    {"Sam",          "Lunch this weekend?"},
    {"Pharmacy",     "Your prescription is ready"},
}};

class MailApp final : public IApp {
public:
    explicit MailApp(bool mock) : mock_(mock) {
        meta_.id            = "mail";
        meta_.name          = "Mail";
        meta_.required_caps = PermissionSet::of({Capability::Network, Capability::Contacts});
        meta_.intents       = {"mail", "email", "inbox", "unread", "reply"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(mail): OAuth to Gmail, open a read-only + send scope session.
            log_warn("mail", "real Gmail backend not configured; use --mock");
            return Status::Unavailable;
        }
        unread_ = kInbox.size();
        log_info("mail", "mail app ready (mock inbox)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        if (cmd.intent == "reply") {
            const std::string body = cmd.slot("query", "your message");
            const char* to = unread_ > 0 ? kInbox[0].from : "the sender";
            // In mock mode we don't actually send; we report what would be sent.
            return AppResponse::say(std::string("Ready to reply to ") + to + ": \"" +
                                    body + "\". Say \"send\" to confirm.")
                .show(hud::HudFrame{}.with_subtitle(std::string("Reply to ") + to)
                          .with_icon(hud::Glyph::Mail));
        }

        // mail / email / inbox / unread: summarize the unread messages.
        if (unread_ == 0)
            return AppResponse::say("You have no new messages.", SpeechTone::Reassuring)
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Mail).with_status(hud::StatusKind::Idle));

        const Message& top = kInbox[0];
        std::string spoken = "You have " + std::to_string(unread_) +
                             " unread messages. The latest is from " + top.from +
                             ": " + top.subject + ".";
        std::string band = std::to_string(unread_) + " unread — " + top.from;
        return AppResponse::say(spoken)
            .show(hud::HudFrame{}
                      .with_subtitle(band)
                      .with_icon(hud::Glyph::Mail)
                      .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("mail", "mail app stopped"); }

private:
    AppMetadata meta_;
    bool        mock_;
    std::size_t unread_ = 0;
};

}  // namespace

std::unique_ptr<IApp> make_mail_app(bool mock) {
    return std::make_unique<MailApp>(mock);
}

}  // namespace echo::apps::mail
