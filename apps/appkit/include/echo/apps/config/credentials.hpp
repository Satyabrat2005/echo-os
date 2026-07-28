// ECHO OS appkit — third-party API credentials (Phase 5).
//
// Every real backend reads its keys/secrets from here, and ONLY from here. The
// values come from the process environment, optionally seeded from a local,
// gitignored `.env` file. Nothing in this module ever writes a secret to a log or
// commits one to the repo (constraint #3): the getters return values, callers pass
// them straight into an Authorization header, and that's it.
//
// Resolution order for every variable:
//   1. the real process environment (what an operator or systemd unit sets);
//   2. otherwise the value loaded from `.env` (dev convenience) if load_dotenv()
//      was called;
//   3. otherwise "" — and configured_*() returns false, so the app degrades to a
//      calm "I can't do that right now" instead of making a doomed request.
//
// See `.env.example` for the full variable list and the README for how to obtain
// each credential and under which account it is registered.
#pragma once

#include <string>

namespace echo::apps::config {

// Seed the in-process overlay from a `.env` file (KEY=VALUE lines, `#` comments,
// blank lines ignored, surrounding quotes stripped). Real environment variables
// are NOT overridden — .env only fills gaps. Missing file is fine (returns false).
// Call once at startup, before constructing real backends.
bool load_dotenv(const std::string& path = ".env");

// Raw accessor: env var, else .env overlay, else "".
std::string get(const std::string& name);

// --- Spotify (OAuth Authorization Code flow) --------------------------------
std::string spotify_client_id();      // ECHO_SPOTIFY_CLIENT_ID
std::string spotify_client_secret();  // ECHO_SPOTIFY_CLIENT_SECRET
std::string spotify_redirect_uri();   // ECHO_SPOTIFY_REDIRECT_URI (local loopback)
bool        spotify_configured();     // id + secret + redirect all present

// --- Gmail (OAuth, read + send scopes only) ---------------------------------
std::string gmail_client_id();        // ECHO_GMAIL_CLIENT_ID
std::string gmail_client_secret();    // ECHO_GMAIL_CLIENT_SECRET
bool        gmail_configured();

// --- Google Custom Search JSON API ------------------------------------------
std::string google_search_key();     // ECHO_GOOGLE_SEARCH_KEY (API key)
std::string google_search_cx();      // ECHO_GOOGLE_SEARCH_CX  (search engine id)
bool        search_configured();

// --- YouTube Data API v3 (same Cloud project; may reuse the search key) ------
std::string youtube_api_key();       // ECHO_YOUTUBE_API_KEY
bool        youtube_configured();

// Directory for cached OAuth tokens (access/refresh). Gitignored. Defaults to
// $ECHO_TOKEN_DIR or ".echo-tokens". Never commit this directory.
std::string token_dir();

}  // namespace echo::apps::config
