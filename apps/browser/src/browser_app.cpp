#include "echo/apps/browser/browser_app.hpp"
#include "echo/apps/browser/browser_backend.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/apps/text/readable.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::browser {

namespace text = echo::apps::text;

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
        if (mock_) {
            backend_ = make_mock_browser_backend();
        } else {
            http_    = net::make_default_http_client();
            backend_ = make_fetch_browser_backend(http_.get());
        }
        Status s = backend_->initialize();
        if (s != Status::Ok) { log_warn("browser", "backend unavailable"); return s; }
        log_info("browser", mock_ ? "browser app ready (mock fetch)"
                                   : "browser app ready (live fetch + readability)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        if (cmd.intent == "read") {
            if (title_.empty())
                return AppResponse::say("There's no page open yet.", SpeechTone::Reassuring);
            std::string spoken = "From " + title_ + ": " + text::summarize(body_, 500);
            return AppResponse::say(spoken)
                .show(hud::HudFrame{}.with_subtitle(title_).with_icon(hud::Glyph::Globe));
        }

        // open / browse
        const std::string query = cmd.slot("query", "the web");
        Page p = backend_->open(query);
        if (p.status != Status::Ok)
            return AppResponse::say("I can't open that right now.", SpeechTone::Reassuring)
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Globe).with_status(hud::StatusKind::Idle));

        title_ = p.title;
        body_  = p.text;
        return AppResponse::say("Opened " + title_ + ". Say \"read it\" to hear the page.")
            .show(hud::HudFrame{}
                      .with_subtitle(title_)
                      .with_icon(hud::Glyph::Globe)
                      .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("browser", "browser app stopped"); }

private:
    AppMetadata                       meta_;
    bool                              mock_;
    std::unique_ptr<net::IHttpClient> http_;
    std::unique_ptr<IBrowserBackend>  backend_;
    std::string                       title_;
    std::string                       body_;
};

}  // namespace

std::unique_ptr<IApp> make_browser_app(bool mock) {
    return std::make_unique<BrowserApp>(mock);
}

}  // namespace echo::apps::browser
