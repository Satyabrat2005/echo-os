// ECHO OS search — real Google Custom Search JSON API backend.
#include "echo/apps/search/search_backend.hpp"

#include "echo/apps/config/credentials.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/apps/net/json.hpp"
#include "echo/apps/net/url.hpp"
#include "echo/log.hpp"

namespace echo::apps::search {

using echo::Status;
namespace net = echo::apps::net;
namespace cfg = echo::apps::config;

std::string custom_search_url(const std::string& key, const std::string& cx,
                              const std::string& query, int num) {
    std::map<std::string, std::string> q = {
        {"key", key}, {"cx", cx}, {"q", query}, {"num", std::to_string(num)},
    };
    return "https://www.googleapis.com/customsearch/v1?" + net::encode_query(q);
}

SearchResults custom_search_parse(const std::string& json_body) {
    SearchResults out;
    auto j = net::Json::parse(json_body);
    const auto& items = j["items"];
    if (!items.is_array()) return out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        SearchResult r;
        r.title   = items[i].str_or("title");
        r.link    = items[i].str_or("link");
        r.snippet = items[i].str_or("snippet");
        if (!r.title.empty()) out.items.push_back(std::move(r));
    }
    return out;
}

namespace {

class MockSearchBackend final : public ISearchBackend {
public:
    Status initialize() override { return Status::Ok; }
    SearchResults search(const std::string& query) override {
        SearchResults out;
        out.items.push_back({"Top result for \"" + query + "\"",
                             "https://example.com/" + query,
                             "A mock snippet summarizing the top result."});
        return out;
    }
};

class GoogleSearchBackend final : public ISearchBackend {
public:
    explicit GoogleSearchBackend(net::IHttpClient* http) : http_(http) {}

    Status initialize() override {
        if (!http_) { log_warn("search", "no network transport; search unavailable"); return Status::Unavailable; }
        if (!cfg::search_configured()) {
            log_warn("search", "Custom Search key/cx not set (.env); use --mock");
            return Status::Unavailable;
        }
        return Status::Ok;
    }

    SearchResults search(const std::string& query) override {
        net::HttpRequest req;
        req.method = net::Method::Get;
        req.url = custom_search_url(cfg::google_search_key(), cfg::google_search_cx(), query);
        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) { SearchResults e; e.status = Status::HardwareError; return e; }
        if (!resp.ok())              { SearchResults e; e.status = Status::Unavailable;  return e; }
        return custom_search_parse(resp.body);
    }

private:
    net::IHttpClient* http_ = nullptr;
};

}  // namespace

std::unique_ptr<ISearchBackend> make_mock_search_backend() {
    return std::make_unique<MockSearchBackend>();
}
std::unique_ptr<ISearchBackend> make_google_search_backend(net::IHttpClient* http) {
    return std::make_unique<GoogleSearchBackend>(http);
}

}  // namespace echo::apps::search
