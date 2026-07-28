#include "echo/apps/config/credentials.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>

namespace echo::apps::config {

namespace {

// The .env overlay: values that fill in only where the real environment is silent.
std::map<std::string, std::string>& overlay() {
    static std::map<std::string, std::string> m;
    return m;
}
std::mutex& overlay_mutex() {
    static std::mutex m;
    return m;
}

std::string env(const char* name) {
    if (!name) return {};
#if defined(_MSC_VER)
    char*  buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) return {};
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string{};
#endif
}

void trim(std::string& s) {
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    auto end = s.find_last_not_of(ws);
    if (end != std::string::npos) s.erase(end + 1);
    else s.clear();
}

// Strip a single pair of matching surrounding quotes, if present.
void unquote(std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\''))) {
        s = s.substr(1, s.size() - 2);
    }
}

}  // namespace

bool load_dotenv(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::lock_guard<std::mutex> lock(overlay_mutex());
    std::string line;
    while (std::getline(in, line)) {
        // Drop comments and blanks.
        std::string trimmed = line;
        trim(trimmed);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        // Optional leading "export ".
        if (trimmed.rfind("export ", 0) == 0) trimmed = trimmed.substr(7);

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trimmed.substr(0, eq);
        std::string val = trimmed.substr(eq + 1);
        trim(key);
        trim(val);
        unquote(val);
        if (!key.empty()) overlay()[key] = val;
    }
    return true;
}

std::string get(const std::string& name) {
    // 1. real environment wins.
    std::string e = env(name.c_str());
    if (!e.empty()) return e;
    // 2. .env overlay fills gaps.
    std::lock_guard<std::mutex> lock(overlay_mutex());
    auto it = overlay().find(name);
    return it == overlay().end() ? std::string{} : it->second;
}

// --- Spotify ----------------------------------------------------------------
std::string spotify_client_id()     { return get("ECHO_SPOTIFY_CLIENT_ID"); }
std::string spotify_client_secret() { return get("ECHO_SPOTIFY_CLIENT_SECRET"); }
std::string spotify_redirect_uri() {
    std::string u = get("ECHO_SPOTIFY_REDIRECT_URI");
    return u.empty() ? std::string("http://127.0.0.1:8888/callback") : u;
}
bool spotify_configured() {
    return !spotify_client_id().empty() && !spotify_client_secret().empty();
}

// --- Gmail ------------------------------------------------------------------
std::string gmail_client_id()     { return get("ECHO_GMAIL_CLIENT_ID"); }
std::string gmail_client_secret() { return get("ECHO_GMAIL_CLIENT_SECRET"); }
bool gmail_configured() {
    return !gmail_client_id().empty() && !gmail_client_secret().empty();
}

// --- Google Custom Search ----------------------------------------------------
std::string google_search_key() { return get("ECHO_GOOGLE_SEARCH_KEY"); }
std::string google_search_cx()  { return get("ECHO_GOOGLE_SEARCH_CX"); }
bool search_configured() {
    return !google_search_key().empty() && !google_search_cx().empty();
}

// --- YouTube ----------------------------------------------------------------
std::string youtube_api_key() {
    std::string k = get("ECHO_YOUTUBE_API_KEY");
    // Same Cloud project — fall back to the search key if only one was set.
    return k.empty() ? google_search_key() : k;
}
bool youtube_configured() { return !youtube_api_key().empty(); }

std::string token_dir() {
    std::string d = get("ECHO_TOKEN_DIR");
    return d.empty() ? std::string(".echo-tokens") : d;
}

}  // namespace echo::apps::config
