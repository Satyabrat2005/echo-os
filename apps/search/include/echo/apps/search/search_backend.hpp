// ECHO OS search — the backend seam.
//
// --mock returns a canned top result; --real calls the Google Custom Search JSON
// API and, per the Phase 2 discipline, summarizes rather than dumping raw results
// (constraint #4). The IApp formats speech/HUD identically for both.
#pragma once

#include "echo/result.hpp"

#include <memory>
#include <string>
#include <vector>

namespace echo::apps::net { class IHttpClient; }

namespace echo::apps::search {

struct SearchResult {
    std::string title;
    std::string link;
    std::string snippet;
};

struct SearchResults {
    echo::Status              status = echo::Status::Ok;
    std::vector<SearchResult> items;
};

class ISearchBackend {
public:
    virtual ~ISearchBackend() = default;
    virtual echo::Status  initialize() = 0;
    virtual SearchResults search(const std::string& query) = 0;
};

std::unique_ptr<ISearchBackend> make_mock_search_backend();
std::unique_ptr<ISearchBackend> make_google_search_backend(net::IHttpClient* http);

// --- Pure helpers (unit-tested) ----------------------------------------------
std::string custom_search_url(const std::string& key, const std::string& cx,
                              const std::string& query, int num = 5);
SearchResults custom_search_parse(const std::string& json_body);

}  // namespace echo::apps::search
