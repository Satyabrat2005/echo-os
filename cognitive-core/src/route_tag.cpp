#include "echo/cognitive/route_tag.hpp"

#include <algorithm>
#include <cctype>

namespace echo::cognitive {

std::string parse_route_tag(std::string& text) {
    const std::string open = "[route:";
    auto p = text.find(open);
    if (p == std::string::npos) return {};
    auto close = text.find(']', p);
    if (close == std::string::npos) return {};

    std::string intent = text.substr(p + open.size(), close - (p + open.size()));
    text.erase(p, close - p + 1);

    // Trim whitespace the tag removal left behind at the front of the reply.
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));

    // Real models are less tidy than a stub: trim stray spaces the model may
    // leave inside the tag (e.g. "[route: media ]") before lowercasing.
    intent.erase(intent.begin(), std::find_if(intent.begin(), intent.end(), not_space));
    intent.erase(std::find_if(intent.rbegin(), intent.rend(), not_space).base(), intent.end());
    for (auto& c : intent) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return intent;
}

}  // namespace echo::cognitive
