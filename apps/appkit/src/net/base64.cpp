#include "echo/apps/net/base64.hpp"

namespace echo::apps::net {

namespace {

std::string encode(const std::string& data, const char* alphabet, bool pad) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    const std::size_t n = data.size();
    while (i + 3 <= n) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                     (static_cast<unsigned char>(data[i + 1]) << 8) |
                     (static_cast<unsigned char>(data[i + 2]));
        out.push_back(alphabet[(v >> 18) & 0x3F]);
        out.push_back(alphabet[(v >> 12) & 0x3F]);
        out.push_back(alphabet[(v >> 6) & 0x3F]);
        out.push_back(alphabet[v & 0x3F]);
        i += 3;
    }
    const std::size_t rem = n - i;
    if (rem == 1) {
        unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out.push_back(alphabet[(v >> 18) & 0x3F]);
        out.push_back(alphabet[(v >> 12) & 0x3F]);
        if (pad) { out.push_back('='); out.push_back('='); }
    } else if (rem == 2) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                     (static_cast<unsigned char>(data[i + 1]) << 8);
        out.push_back(alphabet[(v >> 18) & 0x3F]);
        out.push_back(alphabet[(v >> 12) & 0x3F]);
        out.push_back(alphabet[(v >> 6) & 0x3F]);
        if (pad) out.push_back('=');
    }
    return out;
}

}  // namespace

std::string base64_encode(const std::string& data) {
    static const char kStd[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    return encode(data, kStd, /*pad=*/true);
}

std::string base64url_encode(const std::string& data, bool pad) {
    static const char kUrl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    return encode(data, kUrl, pad);
}

}  // namespace echo::apps::net
