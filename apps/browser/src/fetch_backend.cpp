// ECHO OS browser — real fetch + readability backend.
#include "echo/apps/browser/browser_backend.hpp"

#include "echo/apps/search/search_backend.hpp"  // reuse the search integration
#include "echo/apps/net/http.hpp"
#include "echo/apps/text/readable.hpp"
#include "echo/log.hpp"

namespace echo::apps::browser {

using echo::Status;
namespace net    = echo::apps::net;
namespace text   = echo::apps::text;
namespace search = echo::apps::search;

namespace {

class MockBrowserBackend final : public IBrowserBackend {
public:
    Status initialize() override { return Status::Ok; }
    Page open(const std::string& query) override {
        Page p;
        p.url   = "https://example.com/" + query;
        p.title = "Result for \"" + query + "\"";
        p.text  = "This is a mock page about " + query +
                  ". Real mode fetches the page and extracts its readable text.";
        return p;
    }
};

class FetchBrowserBackend final : public IBrowserBackend {
public:
    explicit FetchBrowserBackend(net::IHttpClient* http) : http_(http) {}

    Status initialize() override {
        if (!http_) { log_warn("browser", "no network transport; fetch unavailable"); return Status::Unavailable; }
        // Reuse the search integration to turn a query into a top link.
        search_ = search::make_google_search_backend(http_);
        Status s = search_->initialize();
        if (s != Status::Ok) log_warn("browser", "search integration unavailable for open");
        return s;
    }

    Page open(const std::string& query) override {
        Page p;
        search::SearchResults r = search_->search(query);
        if (r.status != Status::Ok) { p.status = r.status; return p; }
        if (r.items.empty())        { p.status = Status::Unavailable; return p; }

        p.url = r.items.front().link;
        net::HttpRequest req;
        req.method = net::Method::Get;
        req.url    = p.url;
        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) { p.status = Status::HardwareError; return p; }
        if (!resp.ok())              { p.status = Status::Unavailable;  return p; }

        text::Readable readable = text::extract(resp.body);
        p.title = readable.title.empty() ? r.items.front().title : readable.title;
        p.text  = readable.text;
        return p;
    }

private:
    net::IHttpClient*                       http_ = nullptr;
    std::unique_ptr<search::ISearchBackend> search_;
};

}  // namespace

std::unique_ptr<IBrowserBackend> make_mock_browser_backend() {
    return std::make_unique<MockBrowserBackend>();
}
std::unique_ptr<IBrowserBackend> make_fetch_browser_backend(net::IHttpClient* http) {
    return std::make_unique<FetchBrowserBackend>(http);
}

}  // namespace echo::apps::browser
