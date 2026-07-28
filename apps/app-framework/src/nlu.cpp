#include "echo/apps/framework/nlu.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace echo::apps::nlu {

std::string normalize(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // trim leading/trailing whitespace
    auto not_space = [](char c) { return !std::isspace(static_cast<unsigned char>(c)); };
    auto begin = std::find_if(out.begin(), out.end(), not_space);
    auto end   = std::find_if(out.rbegin(), out.rend(), not_space).base();
    if (begin >= end) return {};
    return std::string(begin, end);
}

VoiceCommand parse(std::string_view utterance, const std::vector<std::string>& vocabulary) {
    VoiceCommand cmd;
    cmd.text = std::string(utterance);

    const std::string norm = normalize(utterance);

    // Tokenize on whitespace.
    std::vector<std::string> words;
    {
        std::stringstream ss(norm);
        std::string w;
        while (ss >> w) words.push_back(w);
    }

    // First token that is a known intent wins; everything after it is the query.
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (std::find(vocabulary.begin(), vocabulary.end(), words[i]) != vocabulary.end()) {
            cmd.intent = words[i];
            std::string query;
            for (std::size_t j = i + 1; j < words.size(); ++j) {
                if (!query.empty()) query += ' ';
                query += words[j];
            }
            if (!query.empty()) cmd.slots["query"] = query;
            break;
        }
    }
    return cmd;
}

}  // namespace echo::apps::nlu
