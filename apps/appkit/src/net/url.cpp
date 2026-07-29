#include "echo/apps/net/url.hpp"

#include <cctype>

namespace echo::apps::net {

std::string url_encode(const std::string& raw) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            // Percent-encode directly — cheaper than snprintf in this hot path,
            // and no ignored return value to reason about (c is one byte, so the
            // output is always exactly "%HH").
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
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
