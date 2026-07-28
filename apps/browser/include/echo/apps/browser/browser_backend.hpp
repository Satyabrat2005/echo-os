// ECHO OS browser — the backend seam.
//
// "open the top result" / "read this page": --real resolves the query through the
// SAME Custom Search integration the search app uses (reusing its result set), then
// fetches the top link and extracts readable text on-device (constraint #4 — no raw
// page, no tracking cruft retained). --mock returns a canned page so the flow works
// without credentials.
#pragma once

#include "echo/result.hpp"

#include <memory>
#include <string>

namespace echo::apps::net { class IHttpClient; }

namespace echo::apps::browser {

struct Page {
    echo::Status status = echo::Status::Ok;
    std::string  url;
    std::string  title;
    std::string  text;   // readable, whitespace-collapsed
};

class IBrowserBackend {
public:
    virtual ~IBrowserBackend() = default;
    virtual echo::Status initialize() = 0;
    // Resolve `query` to a top result and fetch its readable content.
    virtual Page open(const std::string& query) = 0;
};

std::unique_ptr<IBrowserBackend> make_mock_browser_backend();
std::unique_ptr<IBrowserBackend> make_fetch_browser_backend(net::IHttpClient* http);

}  // namespace echo::apps::browser
