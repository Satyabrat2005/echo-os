#include "echo/apps/browser/browser_app.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::browser {

namespace {

class BrowserApp final : public IApp {
public:
    explicit BrowserApp(bool mock) : mock_(mock) {
        meta_.id            = "browser";
        meta_.name          = "Browser";
        meta_.required_caps = PermissionSet::of({Capability::Network});
        meta_.intents       = {"open", "read", "browse"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(browser): stand up the headless fetch + readability extractor.
            log_warn("browser", "real fetch backend not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("browser", "browser app ready (mock fetch)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        if (cmd.intent == "read") {
            if (title_.empty())
                return AppResponse::say("There's no page open yet.", SpeechTone::Reassuring);
            return AppResponse::say("From " + title_ + ": " + body_)
                .show(hud::HudFrame{}.with_subtitle(title_).with_icon(hud::Glyph::Globe));
        }
        // open / browse
        const std::string query = cmd.slot("query", "the web");
        title_ = "Result for \"" + query + "\"";
        body_  = "This is a mock page about " + query +
                 ". Real mode would fetch the page and extract its readable text.";
        return AppResponse::say("Opened " + title_ + ".")
            .show(hud::HudFrame{}
                      .with_subtitle(title_)
                      .with_icon(hud::Glyph::Globe)
                      .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("browser", "browser app stopped"); }

private:
    AppMetadata meta_;
    bool        mock_;
    std::string title_;
    std::string body_;
};

}  // namespace

std::unique_ptr<IApp> make_browser_app(bool mock) {
    return std::make_unique<BrowserApp>(mock);
}

}  // namespace echo::apps::browser
