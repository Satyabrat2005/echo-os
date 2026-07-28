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

VoiceCommand parse(std::string_view utterance,
                   const std::vector<std::string>& vocabulary,
                   const std::vector<std::string>& generic_intents) {
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

    auto in = [](const std::vector<std::string>& v, const std::string& w) {
        return std::find(v.begin(), v.end(), w) != v.end();
    };

    // Two-tier, specificity-aware match. Pass 1 takes the earliest token that is a
    // known intent AND is not a generic catch-all verb, so a domain-specific intent
    // ("unread") beats a leading generic one ("read"). Pass 2 is the fallback: if
    // the utterance carried nothing specific, accept the earliest generic verb so a
    // plain "read this page" / "open wikipedia" still routes. Position order is the
    // tie-breaker inside each pass.
    std::size_t match = words.size();  // index of the chosen intent token, or "none"
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (in(vocabulary, words[i]) && !in(generic_intents, words[i])) { match = i; break; }
    }
    if (match == words.size()) {
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (in(vocabulary, words[i])) { match = i; break; }
        }
    }

    if (match != words.size()) {
        cmd.intent = words[match];
        std::string query;
        for (std::size_t j = match + 1; j < words.size(); ++j) {
            if (!query.empty()) query += ' ';
            query += words[j];
        }
        if (!query.empty()) cmd.slots["query"] = query;
    }
    return cmd;
}

}  // namespace echo::apps::nlu
