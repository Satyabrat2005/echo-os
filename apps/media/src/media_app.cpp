#include "echo/apps/media/media_app.hpp"
#include "echo/log.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace echo::apps::media {

namespace {

struct Track {
    const char* title;
    const char* artist;
};

// A tiny built-in library for mock mode. Real mode resolves these from Spotify.
constexpr std::array<Track, 4> kTracks = {{
    {"Kind of Blue",     "Miles Davis"},
    {"Take Five",        "The Dave Brubeck Quartet"},
    {"So What",          "Miles Davis"},
    {"My Favorite Things","John Coltrane"},
}};

class MediaApp final : public IApp {
public:
    explicit MediaApp(bool mock) : mock_(mock) {
        meta_.id            = "media";
        meta_.name          = "Media";
        meta_.required_caps = PermissionSet::of({Capability::Network, Capability::Storage});
        meta_.intents       = {"play", "pause", "resume", "skip", "music"};
    }

    const AppMetadata& metadata() const override { return meta_; }

    Status initialize() override {
        if (!mock_) {
            // TODO(media): authenticate with Spotify (OAuth), open the playback
            // SDK session. Until credentials exist, real mode is unavailable.
            log_warn("media", "real Spotify backend not configured; use --mock");
            return Status::Unavailable;
        }
        log_info("media", "media app ready (mock playback)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        if (cmd.intent == "pause") {
            playing_ = false;
            return AppResponse::say("Paused.")
                .show(hud::HudFrame{}.with_icon(hud::Glyph::Pause).with_status(hud::StatusKind::Idle));
        }
        if (cmd.intent == "resume") {
            playing_ = true;
            return now_playing("Resuming");
        }
        if (cmd.intent == "skip") {
            index_   = (index_ + 1) % kTracks.size();
            playing_ = true;
            return now_playing("Skipping to");
        }
        // "play" / "music": if a query names something, pretend we matched it to
        // the first track; otherwise resume the current one.
        if (!cmd.slot("query").empty()) index_ = 0;
        playing_ = true;
        return now_playing("Now playing");
    }

    void shutdown() override { log_info("media", "media app stopped"); }

private:
    AppResponse now_playing(const char* verb) {
        const Track& t = kTracks[index_];
        std::string spoken = std::string(verb) + " " + t.title + " by " + t.artist + ".";
        std::string band   = std::string(t.title) + " — " + t.artist;
        AppResponse r = AppResponse::say(spoken);
        r.show(hud::HudFrame{}
                   .with_subtitle(band)
                   .with_icon(hud::Glyph::Play)
                   .with_status(hud::StatusKind::Success));
        return r;
    }

    AppMetadata meta_;
    bool        mock_;
    std::size_t index_   = 0;
    bool        playing_ = false;
};

}  // namespace

std::unique_ptr<IApp> make_media_app(bool mock) {
    return std::make_unique<MediaApp>(mock);
}

}  // namespace echo::apps::media
