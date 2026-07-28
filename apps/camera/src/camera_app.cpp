#include "echo/apps/camera/camera_app.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::camera {

namespace {

class CameraApp final : public IApp {
public:
    explicit CameraApp(bool mock) : mock_(mock) {
        meta_.id            = "camera";
        meta_.name          = "Camera";
        meta_.required_caps = PermissionSet::of({Capability::Camera, Capability::Storage});
        meta_.intents       = {"photo", "capture", "record", "snapshot"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(camera): open the laptop webcam (dev) / glasses capture path
            // (device) on a channel independent of perception's feed.
            log_warn("camera", "real capture backend not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("camera", "camera app ready (mock capture)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        const bool is_video = (cmd.intent == "record");
        const std::string id = (is_video ? "vid_" : "img_") + std::to_string(++counter_);

        if (is_video) {
            return AppResponse::say("Recording started.")
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Video).with_status(hud::StatusKind::Working));
        }
        return AppResponse::say("Photo captured.")
            .show(hud::HudFrame{}
                      .with_subtitle("Saved " + id)
                      .with_icon(hud::Glyph::Camera)
                      .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("camera", "camera app stopped"); }

private:
    AppMetadata meta_;
    bool        mock_;
    int         counter_ = 0;
};

}  // namespace

std::unique_ptr<IApp> make_camera_app(bool mock) {
    return std::make_unique<CameraApp>(mock);
}

}  // namespace echo::apps::camera
