#include "echo/apps/mail/mail_app.hpp"
#include "echo/apps/mail/mail_backend.hpp"
#include "echo/apps/confirm/confirmation.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::mail {

namespace confirm = echo::apps::confirm;

namespace {

// "Dr. Alvarez <a@x.com>" -> "Dr. Alvarez"; a bare address is returned as-is.
std::string display_name(const std::string& from) {
    auto lt = from.find('<');
    if (lt == std::string::npos) return from;
    std::string name = from.substr(0, lt);
    while (!name.empty() && (name.back() == ' ' || name.back() == '"')) name.pop_back();
    std::size_t start = name.find_first_not_of(" \"");
    return start == std::string::npos ? from : name.substr(start);
}

// Drop leading filler so "reply saying I'll be there" -> "I'll be there".
std::string strip_reply_filler(const std::string& q) {
    static const char* kFiller[] = {"saying ", "that ", "with ", "to say "};
    for (const char* f : kFiller) {
        std::size_t len = std::char_traits<char>::length(f);
        if (q.compare(0, len, f) == 0) return q.substr(len);
    }
    return q;
}

class MailApp final : public IApp {
public:
    explicit MailApp(bool mock) : mock_(mock) {
        meta_.id            = "mail";
        meta_.name          = "Mail";
        meta_.required_caps = PermissionSet::of({Capability::Network, Capability::Contacts});
        // "send"/"confirm"/"cancel" are claimed so the confirmation reply reaches
        // this app; they are meaningful only while a reply is armed.
        meta_.intents = {"mail", "email", "inbox", "unread", "reply",
                         "send", "confirm", "cancel"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (mock_) {
            backend_ = make_mock_mail_backend();
        } else {
            http_    = net::make_default_http_client();
            backend_ = make_gmail_backend(http_.get());
        }
        Status s = backend_->initialize();
        if (s != Status::Ok) {
            log_warn("mail", "backend unavailable; mail in degraded state");
            return s;
        }
        log_info("mail", mock_ ? "mail app ready (mock inbox)" : "mail app ready (Gmail)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        // A confirmation response (only relevant while a send is armed).
        if (cmd.intent == "send" || cmd.intent == "confirm" || cmd.intent == "cancel" ||
            gate_.armed()) {
            if (gate_.armed()) return resolve_confirmation(cmd.text);
            // "send"/"cancel" with nothing composed.
            return AppResponse::say("There's nothing to send.", SpeechTone::Reassuring);
        }

        if (cmd.intent == "reply") return compose_reply(cmd);

        // mail / email / inbox / unread: summarize.
        return summarize_inbox();
    }

    void shutdown() override { log_info("mail", "mail app stopped"); }

private:
    AppResponse degraded(const char* what) {
        return AppResponse::say(std::string("I can't ") + what + " right now.",
                                SpeechTone::Reassuring)
            .show(hud::HudFrame{}.with_icon(hud::Glyph::Mail).with_status(hud::StatusKind::Idle));
    }

    AppResponse summarize_inbox() {
        Inbox in = backend_->list_unread();
        if (in.status != Status::Ok) return degraded("check your email");
        top_ = in.top;  // cache for a subsequent "reply"

        if (in.unread_count == 0 || in.top.empty())
            return AppResponse::say("You have no new messages.", SpeechTone::Reassuring)
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Mail).with_status(hud::StatusKind::Idle));

        const MailSummary& t = in.top.front();
        std::string spoken = "You have " + std::to_string(in.unread_count) +
                             " unread messages. The latest is from " + display_name(t.from) +
                             ": " + t.subject + ".";
        std::string band = std::to_string(in.unread_count) + " unread — " + display_name(t.from);
        return AppResponse::say(spoken).show(
            hud::HudFrame{}
                .with_subtitle(band)
                .with_icon(hud::Glyph::Mail)
                .with_status(hud::StatusKind::Success));
    }

    AppResponse compose_reply(const VoiceCommand& cmd) {
        // Need a target. Use the cached top unread, fetching once if needed.
        if (top_.empty()) {
            Inbox in = backend_->list_unread();
            if (in.status != Status::Ok) return degraded("reply to that");
            top_ = in.top;
        }
        if (top_.empty())
            return AppResponse::say("There's no message to reply to.", SpeechTone::Reassuring);

        const MailSummary& t = top_.front();
        std::string body = strip_reply_filler(cmd.slot("query", "your message"));

        // Stage — do NOT send. Speak the summary and wait for explicit confirmation.
        confirm::PendingAction act;
        act.kind    = "send_email";
        act.summary = "reply to " + display_name(t.from) + ": \"" + body + "\"";
        act.params["to"]        = t.from;
        act.params["subject"]   = t.subject.rfind("Re:", 0) == 0 ? t.subject : "Re: " + t.subject;
        act.params["body"]      = body;
        act.params["thread_id"] = t.thread_id;
        gate_.arm(std::move(act));

        return AppResponse::say(
                   "Ready to reply to " + display_name(t.from) + ": \"" + body +
                   "\". Say \"send\" to confirm, or \"cancel\".")
            .show(hud::HudFrame{}
                      .with_subtitle("Reply to " + display_name(t.from))
                      .with_icon(hud::Glyph::Mail));
    }

    AppResponse resolve_confirmation(const std::string& utterance) {
        const confirm::PendingAction act = gate_.pending();  // copy before decide clears
        confirm::Decision d = gate_.decide(utterance);

        if (d != confirm::Decision::Confirmed) {
            // Declined or unrecognized — both fail safe: nothing is sent.
            const char* line = d == confirm::Decision::Declined
                                   ? "Okay, I won't send it."
                                   : "I didn't catch that, so I won't send it.";
            return AppResponse::say(line, SpeechTone::Reassuring)
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Mail).with_status(hud::StatusKind::Idle));
        }

        // Confirmed: this is the ONE place send() is ever reached.
        Status s = backend_->send(act.params.at("to"), act.params.at("subject"),
                                  act.params.at("body"), act.params.at("thread_id"));
        if (s != Status::Ok) return degraded("send that email");
        return AppResponse::say("Sent.")
            .show(hud::HudFrame{}.with_icon(hud::Glyph::Mail).with_status(hud::StatusKind::Success));
    }

    AppMetadata                       meta_;
    bool                              mock_;
    std::unique_ptr<net::IHttpClient> http_;
    std::unique_ptr<IMailBackend>     backend_;
    confirm::ConfirmationGate         gate_;
    std::vector<MailSummary>          top_;
};

}  // namespace

std::unique_ptr<IApp> make_mail_app(bool mock) {
    return std::make_unique<MailApp>(mock);
}

}  // namespace echo::apps::mail
