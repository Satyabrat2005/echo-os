#include "echo/apps/config/token_store.hpp"

#include "echo/apps/config/credentials.hpp"
#include "echo/apps/net/json.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace echo::apps::config {

namespace {

std::string token_path(const std::string& service) {
    return token_dir() + "/" + service + ".json";
}

// Minimal JSON string escaping for the two token fields (which are opaque URL-safe
// strings in practice, but escape defensively).
std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c);
        }
    }
    return out;
}

}  // namespace

OAuthTokens load_tokens(const std::string& service) {
    OAuthTokens t;
    std::ifstream in(token_path(service), std::ios::binary);
    if (!in) return t;
    std::stringstream ss;
    ss << in.rdbuf();
    auto j = net::Json::parse(ss.str());
    if (!j.is_object()) return t;
    t.access_token    = j.str_or("access_token");
    t.refresh_token   = j.str_or("refresh_token");
    t.expires_at_unix = static_cast<std::int64_t>(j["expires_at_unix"].as_number(0));
    return t;
}

bool save_tokens(const std::string& service, const OAuthTokens& t) {
    std::error_code ec;
    std::filesystem::create_directories(token_dir(), ec);  // ignore if exists

    std::ofstream out(token_path(service), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\n"
        << "  \"access_token\": \""  << esc(t.access_token)  << "\",\n"
        << "  \"refresh_token\": \"" << esc(t.refresh_token) << "\",\n"
        << "  \"expires_at_unix\": " << t.expires_at_unix    << "\n"
        << "}\n";
    return static_cast<bool>(out);
}

}  // namespace echo::apps::config
