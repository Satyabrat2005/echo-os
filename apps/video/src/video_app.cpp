#include "echo/apps/video/video_app.hpp"
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
        if (!mock_) {
            // TODO(video): YouTube Data API key + audio stream extraction.
            log_warn("video", "real YouTube backend not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("video", "video app ready (mock, audio-first)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        const std::string query = cmd.slot("query", "your video");
        const std::string title = "Mock video: " + query;
        // Audio-first: the HUD carries the title and a progress readout, nothing
        // to actually watch.
        return AppResponse::say("Playing " + title + ", audio only.")
            .show(hud::HudFrame{}
                      .with_subtitle(title + "  0:00 / 4:12")
                      .with_icon(hud::Glyph::Video)
                      .with_status(hud::StatusKind::Working));
    }

    void shutdown() override { log_info("video", "video app stopped"); }

private:
    AppMetadata meta_;
    bool        mock_;
};

}  // namespace

std::unique_ptr<IApp> make_video_app(bool mock) {
    return std::make_unique<VideoApp>(mock);
}

}  // namespace echo::apps::video
