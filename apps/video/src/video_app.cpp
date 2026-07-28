#include "echo/apps/video/video_app.hpp"
#include "echo/apps/video/video_backend.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::video {

namespace {

class VideoApp final : public IApp {
public:
    explicit VideoApp(bool mock) : mock_(mock) {
        meta_.id            = "video";
        meta_.name          = "Video";
        meta_.required_caps = PermissionSet::of({Capability::Network});
        meta_.intents       = {"watch", "youtube", "clip"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (mock_) {
            backend_ = make_mock_video_backend();
        } else {
            http_    = net::make_default_http_client();
            backend_ = make_youtube_backend(http_.get());
        }
        Status s = backend_->initialize();
        if (s != Status::Ok) { log_warn("video", "backend unavailable"); return s; }
        log_info("video", mock_ ? "video app ready (mock, audio-first)"
                                 : "video app ready (YouTube, audio-first)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        const std::string query = cmd.slot("query", "your video");
        VideoResults r = backend_->search(query);
        if (r.status != Status::Ok)
            return AppResponse::say("I can't play video right now.", SpeechTone::Reassuring)
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Video).with_status(hud::StatusKind::Idle));
        if (r.items.empty())
            return AppResponse::say("I didn't find a video for " + query + ".",
                                    SpeechTone::Reassuring);

        const VideoResult& v = r.items.front();
        // Audio-first: the HUD carries the title, channel, and a progress readout.
        std::string band = v.title + " — " + v.channel + "  0:00";
        return AppResponse::say("Playing " + v.title + " by " + v.channel + ", audio only.")
            .show(hud::HudFrame{}
                      .with_subtitle(band)
                      .with_icon(hud::Glyph::Video)
                      .with_status(hud::StatusKind::Working));
    }

    void shutdown() override { log_info("video", "video app stopped"); }

private:
    AppMetadata                       meta_;
    bool                              mock_;
    std::unique_ptr<net::IHttpClient> http_;
    std::unique_ptr<IVideoBackend>    backend_;
};

}  // namespace

std::unique_ptr<IApp> make_video_app(bool mock) {
    return std::make_unique<VideoApp>(mock);
}

}  // namespace echo::apps::video
