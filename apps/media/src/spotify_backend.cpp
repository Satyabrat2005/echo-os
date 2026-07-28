// ECHO OS media — real Spotify Web API backend.
//
// OAuth choice: Authorization Code flow with a loopback redirect
// (http://127.0.0.1:8888/callback). Rationale, documented for the headless-glasses
// context: Spotify does not offer device-code grant, and the loopback redirect is
// the flow Spotify recommends for apps that can pop a browser once. On the glasses,
// that one-time consent happens during pairing on the companion phone/laptop, not
// on the device — after that the cached refresh token drives silent renewals, so
// the glasses never show a browser. The interactive consent is therefore a manual,
// documented setup step (see README); this backend implements everything after it:
// token exchange, silent refresh, and playback control.
//
// Everything network-facing goes through IHttpClient, so the request-building and
// response-parsing below are covered by unit tests with a fake client. The live
// calls themselves can only be exercised with real credentials on real hardware —
// documented, not claimed.
#include "echo/apps/media/media_backend.hpp"

#include "echo/apps/config/credentials.hpp"
#include "echo/apps/config/token_store.hpp"
#include "echo/apps/net/base64.hpp"
#include "echo/apps/net/http.hpp"
#include "echo/apps/net/json.hpp"
#include "echo/apps/net/url.hpp"
#include "echo/log.hpp"

#include <chrono>

namespace echo::apps::media {

using echo::Status;
namespace net = echo::apps::net;
namespace cfg = echo::apps::config;

// --- Pure helpers (unit-tested) ----------------------------------------------

std::string spotify_authorize_url(const std::string& client_id,
                                  const std::string& redirect_uri,
                                  const std::string& scopes) {
    std::map<std::string, std::string> q = {
        {"client_id", client_id},
        {"response_type", "code"},
        {"redirect_uri", redirect_uri},
        {"scope", scopes},
    };
    return "https://accounts.spotify.com/authorize?" + net::encode_query(q);
}

std::string spotify_token_exchange_body(const std::string& code,
                                        const std::string& redirect_uri) {
    std::map<std::string, std::string> f = {
        {"grant_type", "authorization_code"},
        {"code", code},
        {"redirect_uri", redirect_uri},
    };
    return net::encode_query(f);
}

std::string spotify_refresh_body(const std::string& refresh_token) {
    std::map<std::string, std::string> f = {
        {"grant_type", "refresh_token"},
        {"refresh_token", refresh_token},
    };
    return net::encode_query(f);
}

NowPlaying spotify_parse_now_playing(const std::string& json_body) {
    NowPlaying np;
    auto j = net::Json::parse(json_body);
    if (!j.is_object()) {  // 204 No Content or empty -> nothing playing
        np.playing = false;
        return np;
    }
    np.playing = j["is_playing"].as_bool(false);
    const auto& item = j["item"];
    np.title = item.str_or("name");
    const auto& artists = item["artists"];
    if (artists.is_array() && artists.size() > 0)
        np.artist = artists[0].str_or("name");
    return np;
}

namespace {

constexpr const char* kScopes =
    "user-read-playback-state user-modify-playback-state user-read-currently-playing";

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// --- Mock backend ------------------------------------------------------------
class MockMediaBackend final : public IMediaBackend {
public:
    Status initialize() override { return Status::Ok; }
    NowPlaying play(const std::string& query) override {
        if (!query.empty()) index_ = 0;
        playing_ = true;
        return state();
    }
    NowPlaying pause() override  { playing_ = false; return state(); }
    NowPlaying resume() override { playing_ = true;  return state(); }
    NowPlaying skip() override   { index_ = (index_ + 1) % kN; playing_ = true; return state(); }
    NowPlaying current() override { return state(); }

private:
    static constexpr std::size_t kN = 4;
    struct T { const char* title; const char* artist; };
    NowPlaying state() {
        static const T kTracks[kN] = {
            {"Kind of Blue", "Miles Davis"},
            {"Take Five", "The Dave Brubeck Quartet"},
            {"So What", "Miles Davis"},
            {"My Favorite Things", "John Coltrane"},
        };
        NowPlaying np;
        np.playing = playing_;
        np.title   = kTracks[index_].title;
        np.artist  = kTracks[index_].artist;
        return np;
    }
    std::size_t index_ = 0;
    bool        playing_ = false;
};

// --- Real Spotify backend ----------------------------------------------------
class SpotifyBackend final : public IMediaBackend {
public:
    explicit SpotifyBackend(net::IHttpClient* http) : http_(http) {}

    Status initialize() override {
        if (!http_) {
            log_warn("media", "no network transport compiled; Spotify unavailable");
            return Status::Unavailable;
        }
        if (!cfg::spotify_configured()) {
            log_warn("media", "Spotify credentials not set (.env); use --mock");
            return Status::Unavailable;
        }
        tokens_ = cfg::load_tokens("spotify");
        if (!tokens_.has_refresh()) {
            log_warn("media",
                     "no Spotify refresh token cached; run the one-time authorize "
                     "step (see README). Real playback unavailable until then.");
            return Status::Unavailable;
        }
        return refresh_if_needed();
    }

    NowPlaying play(const std::string& query) override {
        // A spoken query maps to search+play; without one, resume playback. The
        // search->context handoff is documented as a follow-up; here we resume and
        // report the current track, which is the safe, data-cheap default.
        (void)query;
        if (Status s = ensure_token(); s != Status::Ok) return fail(s);
        put("https://api.spotify.com/v1/me/player/play");
        return current();
    }
    NowPlaying pause() override {
        if (Status s = ensure_token(); s != Status::Ok) return fail(s);
        put("https://api.spotify.com/v1/me/player/pause");
        NowPlaying np = current();
        np.playing = false;
        return np;
    }
    NowPlaying resume() override { return play({}); }
    NowPlaying skip() override {
        if (Status s = ensure_token(); s != Status::Ok) return fail(s);
        post("https://api.spotify.com/v1/me/player/next");
        return current();
    }
    NowPlaying current() override {
        if (Status s = ensure_token(); s != Status::Ok) return fail(s);
        net::HttpRequest req;
        req.method = net::Method::Get;
        req.url    = "https://api.spotify.com/v1/me/player/currently-playing";
        req.headers["Authorization"] = "Bearer " + tokens_.access_token;
        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) return fail(Status::HardwareError);
        if (!resp.ok() && resp.status != 204) return fail(Status::Unavailable);
        return spotify_parse_now_playing(resp.body);
    }

private:
    NowPlaying fail(Status s) { NowPlaying np; np.status = s; return np; }

    Status ensure_token() { return refresh_if_needed(); }

    Status refresh_if_needed() {
        if (!tokens_.needs_refresh(now_unix())) return Status::Ok;
        if (!tokens_.has_refresh()) return Status::Unavailable;

        net::HttpRequest req;
        req.method = net::Method::Post;
        req.url    = "https://accounts.spotify.com/api/token";
        req.headers["Content-Type"]  = "application/x-www-form-urlencoded";
        req.headers["Authorization"] = "Basic " + net::base64_encode(
            cfg::spotify_client_id() + ":" + cfg::spotify_client_secret());
        req.body = spotify_refresh_body(tokens_.refresh_token);

        net::HttpResponse resp = http_->send(req);
        if (resp.transport_failed()) return Status::HardwareError;
        if (!resp.ok()) return Status::Unavailable;

        auto j = net::Json::parse(resp.body);
        std::string at = j.str_or("access_token");
        if (at.empty()) return Status::Unavailable;
        tokens_.access_token    = at;
        tokens_.expires_at_unix = now_unix() +
            static_cast<std::int64_t>(j["expires_in"].as_number(3600));
        // Spotify may or may not return a new refresh token; keep the old if not.
        std::string rt = j.str_or("refresh_token");
        if (!rt.empty()) tokens_.refresh_token = rt;
        cfg::save_tokens("spotify", tokens_);
        return Status::Ok;
    }

    void put(const std::string& url)  { send_control(net::Method::Put, url); }
    void post(const std::string& url) { send_control(net::Method::Post, url); }
    void send_control(net::Method m, const std::string& url) {
        net::HttpRequest req;
        req.method = m;
        req.url    = url;
        req.headers["Authorization"]  = "Bearer " + tokens_.access_token;
        req.headers["Content-Length"] = "0";
        http_->send(req);  // best-effort; current() reports true state after
    }

    net::IHttpClient* http_ = nullptr;
    cfg::OAuthTokens  tokens_;
};

}  // namespace

std::unique_ptr<IMediaBackend> make_mock_media_backend() {
    return std::make_unique<MockMediaBackend>();
}

std::unique_ptr<IMediaBackend> make_spotify_backend(net::IHttpClient* http) {
    return std::make_unique<SpotifyBackend>(http);
}

}  // namespace echo::apps::media
