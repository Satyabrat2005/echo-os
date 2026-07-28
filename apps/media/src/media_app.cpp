#include "echo/apps/media/media_app.hpp"
#include "echo/apps/media/media_backend.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/log.hpp"

#include <string>

namespace echo::apps::media {

namespace {

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
        if (mock_) {
            backend_ = make_mock_media_backend();
        } else {
            // Own the one HTTP client; hand the backend a borrowed pointer. A null
            // client (no network compiled) makes initialize() report Unavailable.
            http_    = net::make_default_http_client();
            backend_ = make_spotify_backend(http_.get());
        }
        Status s = backend_->initialize();
        if (s != Status::Ok) {
            log_warn("media", "backend unavailable; media in degraded state");
            return s;
        }
        log_info("media", mock_ ? "media app ready (mock playback)"
                                 : "media app ready (Spotify)");
        return Status::Ok;
    }

    AppResponse on_command(const VoiceCommand& cmd) override {
        NowPlaying np;
        if (cmd.intent == "pause")       np = backend_->pause();
        else if (cmd.intent == "resume") np = backend_->resume();
        else if (cmd.intent == "skip")   np = backend_->skip();
        else                             np = backend_->play(cmd.slot("query"));

        if (np.status != Status::Ok) return degraded();

        if (cmd.intent == "pause")
            return AppResponse::say("Paused.").show(
                hud::HudFrame{}.with_icon(hud::Glyph::Pause).with_status(hud::StatusKind::Idle));

        const char* verb = cmd.intent == "skip"     ? "Skipping to"
                           : cmd.intent == "resume" ? "Resuming"
                                                    : "Now playing";
        return now_playing(verb, np);
    }

    void shutdown() override { log_info("media", "media app stopped"); }

private:
    // The one calm degraded line — never a stack trace, never silence (constraint #2).
    AppResponse degraded() {
        return AppResponse::say("I can't play music right now.", SpeechTone::Reassuring)
            .show(hud::HudFrame{}.with_icon(hud::Glyph::Pause).with_status(hud::StatusKind::Idle));
    }

    AppResponse now_playing(const char* verb, const NowPlaying& np) {
        std::string spoken = std::string(verb) + " " + np.title + " by " + np.artist + ".";
        std::string band   = np.title + " — " + np.artist;
        return AppResponse::say(spoken).show(
            hud::HudFrame{}
                .with_subtitle(band)
                .with_icon(hud::Glyph::Play)
                .with_status(hud::StatusKind::Success));
    }

    AppMetadata                       meta_;
    bool                              mock_;
    std::unique_ptr<net::IHttpClient> http_;
    std::unique_ptr<IMediaBackend>    backend_;
};

}  // namespace

std::unique_ptr<IApp> make_media_app(bool mock) {
    return std::make_unique<MediaApp>(mock);
}

}  // namespace echo::apps::media
