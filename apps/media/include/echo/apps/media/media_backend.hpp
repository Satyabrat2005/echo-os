// ECHO OS media — the backend seam.
//
// The MediaApp (voice + HUD formatting) is unchanged in shape; only its data
// source varies. In --mock it plays a built-in track list; in --real a Spotify
// backend drives the same struct. The IApp never sees Spotify types — swapping the
// backend changes not one caller (constraint #4).
#pragma once

#include "echo/result.hpp"

#include <map>
#include <memory>
#include <string>

// Forward-declare the HTTP boundary so this header pulls in no appkit transport.
namespace echo::apps::net { class IHttpClient; }

namespace echo::apps::media {

// What the app needs to speak/show for a playback state. Backend-neutral.
struct NowPlaying {
    echo::Status status = echo::Status::Ok;
    bool         playing = false;
    std::string  title;
    std::string  artist;
};

// The narrow contract a media source must satisfy. Methods never throw; on failure
// they return a NowPlaying with a non-Ok status and the app degrades calmly.
class IMediaBackend {
public:
    virtual ~IMediaBackend() = default;
    virtual echo::Status initialize() = 0;
    virtual NowPlaying   play(const std::string& query) = 0;
    virtual NowPlaying   pause() = 0;
    virtual NowPlaying   resume() = 0;
    virtual NowPlaying   skip() = 0;
    virtual NowPlaying   current() = 0;
};

std::unique_ptr<IMediaBackend> make_mock_media_backend();

// Real Spotify backend. `http` is borrowed (owned by the app) and may be null when
// the build has no network transport — initialize() then returns Unavailable.
std::unique_ptr<IMediaBackend> make_spotify_backend(net::IHttpClient* http);

// --- Pure helpers, unit-tested without a network -----------------------------
// The one-time consent URL the user opens in a browser to authorize ECHO. Auth
// Code flow: response_type=code, the requested scopes, and the loopback redirect.
std::string spotify_authorize_url(const std::string& client_id,
                                  const std::string& redirect_uri,
                                  const std::string& scopes);

// application/x-www-form-urlencoded body to exchange a one-time code for tokens.
std::string spotify_token_exchange_body(const std::string& code,
                                        const std::string& redirect_uri);

// Body to refresh an access token from a stored refresh token.
std::string spotify_refresh_body(const std::string& refresh_token);

// Parse the JSON from GET /v1/me/player/currently-playing into NowPlaying.
NowPlaying spotify_parse_now_playing(const std::string& json_body);

}  // namespace echo::apps::media
