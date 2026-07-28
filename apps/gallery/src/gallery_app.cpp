#include "echo/apps/gallery/gallery_app.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::gallery {

namespace {

class GalleryApp final : public IApp {
public:
    explicit GalleryApp(bool mock) : mock_(mock) {
        meta_.id            = "gallery";
        meta_.name          = "Gallery";
        meta_.required_caps = PermissionSet::of({Capability::Storage});
        meta_.intents       = {"gallery", "photos", "album", "thumbnails"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(gallery): read the on-device media index.
            log_warn("gallery", "real media store not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("gallery", "gallery app ready (mock library)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        // A canned result set; a real query would filter by the spoken timeframe.
        const std::string when  = cmd.slot("query", "recent");
        const int         count = 3;
        std::string spoken = "Showing " + std::to_string(count) + " " + when +
                             " photos. Say \"next\" to move through them.";
        return AppResponse::say(spoken)
            .show(hud::HudFrame{}
                      .with_subtitle(std::to_string(count) + " photos — " + when)
                      .with_icon(hud::Glyph::Photo)
                      .with_status(hud::StatusKind::Success));
    }

    void shutdown() override { log_info("gallery", "gallery app stopped"); }

private:
    AppMetadata meta_;
    bool        mock_;
};

}  // namespace

std::unique_ptr<IApp> make_gallery_app(bool mock) {
    return std::make_unique<GalleryApp>(mock);
}

}  // namespace echo::apps::gallery
