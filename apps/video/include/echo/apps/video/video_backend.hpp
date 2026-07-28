// ECHO OS video — the backend seam.
//
// Audio-first as before: --real queries the YouTube Data API v3 for matching
// videos and reports title/channel; --mock returns a canned entry. No video is
// rendered on the glasses — the HUD carries only a title and progress readout.
#pragma once

#include "echo/result.hpp"

#include <memory>
#include <string>
#include <vector>

namespace echo::apps::net { class IHttpClient; }

namespace echo::apps::video {

struct VideoResult {
    std::string title;
    std::string channel;
    std::string video_id;
};

struct VideoResults {
    echo::Status             status = echo::Status::Ok;
    std::vector<VideoResult> items;
};

class IVideoBackend {
public:
    virtual ~IVideoBackend() = default;
    virtual echo::Status initialize() = 0;
    virtual VideoResults search(const std::string& query) = 0;
};

std::unique_ptr<IVideoBackend> make_mock_video_backend();
std::unique_ptr<IVideoBackend> make_youtube_backend(net::IHttpClient* http);

// --- Pure helpers (unit-tested) ----------------------------------------------
std::string youtube_search_url(const std::string& api_key, const std::string& query,
                               int max_results = 5);
VideoResults youtube_parse(const std::string& json_body);

}  // namespace echo::apps::video
