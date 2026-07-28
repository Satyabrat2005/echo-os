#include "echo/apps/search/search_app.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::search {

namespace {

class SearchApp final : public IApp {
public:
    explicit SearchApp(bool mock) : mock_(mock) {
        meta_.id            = "search";
        meta_.name          = "Search";
        meta_.required_caps = PermissionSet::of({Capability::Network});
        meta_.intents       = {"search", "google", "lookup"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(search): configure the Programmable Search Engine id + API key.
            log_warn("search", "real search backend not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("search", "search app ready (mock results)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        const std::string query = cmd.slot("query");
        if (query.empty())
            return AppResponse::say("What would you like me to search for?",
                                    SpeechTone::Reassuring);

        // Canned top result. Real mode returns the actual first organic result.
        const std::string top = "Top result for \"" + query + "\"";
        return AppResponse::say(top + ". Say \"read it\" to hear more.")
            .show(hud::HudFrame{}
                      .with_subtitle(top)
                      .with_icon(hud::Glyph::Search)
                      .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("search", "search app stopped"); }

private:
    AppMetadata meta_;
    bool        mock_;
};

}  // namespace

std::unique_ptr<IApp> make_search_app(bool mock) {
    return std::make_unique<SearchApp>(mock);
}

}  // namespace echo::apps::search
