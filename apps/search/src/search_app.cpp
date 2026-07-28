#include "echo/apps/search/search_app.hpp"
#include "echo/apps/search/search_backend.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/apps/text/readable.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::search {

namespace text = echo::apps::text;

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
        if (mock_) {
            backend_ = make_mock_search_backend();
        } else {
            http_    = net::make_default_http_client();
            backend_ = make_google_search_backend(http_.get());
        }
        Status s = backend_->initialize();
        if (s != Status::Ok) { log_warn("search", "backend unavailable"); return s; }
        log_info("search", mock_ ? "search app ready (mock results)"
                                  : "search app ready (Google Custom Search)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        const std::string query = cmd.slot("query");
        if (query.empty())
            return AppResponse::say("What would you like me to search for?",
                                    SpeechTone::Reassuring);

        SearchResults r = backend_->search(query);
        if (r.status != Status::Ok || r.items.empty()) {
            if (r.status != Status::Ok)
                return AppResponse::say("I can't search right now.", SpeechTone::Reassuring);
            return AppResponse::say("I didn't find anything for " + query + ".",
                                    SpeechTone::Reassuring);
        }

        // Summarize the top result's snippet rather than dumping a list (Phase 2
        // discipline). "read it" is handled by the browser app against the link.
        const SearchResult& top = r.items.front();
        std::string summary = text::summarize(top.snippet, 220);
        std::string spoken = top.title + ". " + summary + " Say \"read it\" to hear more.";
        return AppResponse::say(spoken).show(
            hud::HudFrame{}
                .with_subtitle(top.title)
                .with_icon(hud::Glyph::Search)
                .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("search", "search app stopped"); }

private:
    AppMetadata                       meta_;
    bool                              mock_;
    std::unique_ptr<net::IHttpClient> http_;
    std::unique_ptr<ISearchBackend>   backend_;
};

}  // namespace

std::unique_ptr<IApp> make_search_app(bool mock) {
    return std::make_unique<SearchApp>(mock);
}

}  // namespace echo::apps::search
