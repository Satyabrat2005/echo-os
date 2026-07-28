// ECHO OS appkit — OAuth token cache.
//
// The one-time browser consent yields a refresh token; from then on the device
// refreshes silently. Those tokens are secrets: they live only in a local,
// gitignored directory (config::token_dir()), one small JSON file per service,
// never in the repo and never in a log (constraint #3). This module is the only
// thing that reads or writes them.
#pragma once

#include <cstdint>
#include <string>

namespace echo::apps::config {

struct OAuthTokens {
    std::string   access_token;
    std::string   refresh_token;
    std::int64_t  expires_at_unix = 0;  // absolute expiry; 0 == unknown/expired

    bool has_refresh() const { return !refresh_token.empty(); }
    // True if the access token is missing or within `skew` seconds of expiry.
    bool needs_refresh(std::int64_t now_unix, std::int64_t skew = 60) const {
        return access_token.empty() || expires_at_unix == 0 ||
               now_unix + skew >= expires_at_unix;
    }
};

// Load tokens for `service` (e.g. "spotify", "gmail") from token_dir()/<service>.json.
// Returns an empty OAuthTokens (has_refresh() == false) if the file is absent or
// unreadable — a first-run device simply has no tokens yet.
OAuthTokens load_tokens(const std::string& service);

// Persist tokens for `service`. Creates token_dir() if needed. Returns false on
// I/O failure (the caller keeps working with the in-memory tokens for this run).
bool save_tokens(const std::string& service, const OAuthTokens& tokens);

}  // namespace echo::apps::config
