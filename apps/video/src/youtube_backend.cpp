// ECHO OS video — real YouTube Data API v3 backend.
#include "echo/apps/video/video_backend.hpp"

#include "echo/apps/config/credentials.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/apps/net/json.hpp"
#include "echo/apps/net/url.hpp"
#include "echo/log.hpp"

namespace echo::apps::video {

using echo::Status;
namespace net = echo::apps::net;
namespace cfg = echo::apps::config;

std::string youtube_search_url(const std::string& api_key, const std::string& query,
                               int max_results) {
    std::map<std::string, std::string> q = {
        {"part", "snippet"}, {"type", "video"}, {"q", query},
        {"maxResults", std::to_string(max_results)}, {"key", api_key},
    };
    return "https://www.googleapis.com/youtube/v3/search?" + net::encode_query(q);
}

VideoResults youtube_parse(const std::string& json_body) {
    VideoResults out;
    auto j = net::Json::parse(json_body);
    const auto& items = j["items"];
    if (!items.is_array()) return out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        VideoResult r;
        r.title    = items[i]["snippet"].str_or("title");
        r.channel  = items[i]["snippet"].str_or("channelTitle");
        r.video_id = items[i]["id"].str_or("videoId");
        if (!r.title.empty()) out.items.push_back(std::move(r));
    }
    return out;
}

namespace {

class MockVideoBackend final : public IVideoBackend {
public:
    Status initialize() override { return Status::Ok; }
    VideoResults search(const std::string& query) override {
        VideoResults out;
        out.items.push_back({"Mock video: " + query, "ECHO Test Channel", "mock123"});
        return out;
    }
};

class YouTubeBackend final : public IVideoBackend {
public:
    explicit YouTubeBackend(net::IHttpClient* http) : http_(http) {}

    Status initialize() override {
        if (!http_) { log_warn("video", "no network transport; YouTube unavailable"); return Status::Unavailable; }
        if (!cfg::youtube_configured()) {
            log_warn("video", "YouTube API key not set (.env); use --mock");
            return Status::Unavailable;
        }
        return Status::Ok;
    }

    VideoResults search(const std::string& query) override {
        net::HttpRequest req;
        req.method = net::Method::Get;
        req.url    = youtube_search_url(cfg::youtube_api_key(), query);
        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) { VideoResults e; e.status = Status::HardwareError; return e; }
        if (!resp.ok())              { VideoResults e; e.status = Status::Unavailable;  return e; }
        return youtube_parse(resp.body);
    }

private:
    net::IHttpClient* http_ = nullptr;
};

}  // namespace

std::unique_ptr<IVideoBackend> make_mock_video_backend() {
    return std::make_unique<MockVideoBackend>();
}
std::unique_ptr<IVideoBackend> make_youtube_backend(net::IHttpClient* http) {
    return std::make_unique<YouTubeBackend>(http);
}

}  // namespace echo::apps::video
