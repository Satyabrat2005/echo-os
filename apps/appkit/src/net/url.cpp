#include "echo/apps/net/url.hpp"

#include <cctype>
#include <cstdio>

namespace echo::apps::net {

std::string url_encode(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out.append(buf);
        }
    }
    return out;
}

std::string encode_query(const std::map<std::string, std::string>& params) {
    std::string out;
    bool        first = true;
    for (const auto& [k, v] : params) {
        if (!first) out.push_back('&');
        first = false;
        out += url_encode(k);
        out.push_back('=');
        out += url_encode(v);
    }
    return out;
}

}  // namespace echo::apps::net
