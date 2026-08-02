#include "echo/memory/utterance.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace echo::memory {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    auto ns = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) {
        // Strip trailing punctuation the way a transcript might carry it.
        while (!w.empty() && std::ispunct(static_cast<unsigned char>(w.back()))) w.pop_back();
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

std::string capitalize(std::string w) {
    if (!w.empty()) w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
    return w;
}

bool is_alpha_word(const std::string& w) {
    if (w.empty()) return false;
    return std::all_of(w.begin(), w.end(),
                       [](unsigned char c) { return std::isalpha(c) != 0; });
}

}  // namespace

std::optional<Naming> parse_naming(const std::string& transcript) {
    const std::string t = trim(transcript);
    const std::string lt = lower(t);

    // Require a clear naming lead-in so ordinary speech never creates a person.
    static const char* kLeads[] = {"this is ", "that is ", "this is my ",
                                   "that's ", "this is called "};
    std::size_t start = std::string::npos;
    std::size_t lead_len = 0;
    for (const char* lead : kLeads) {
        if (lt.rfind(lead, 0) == 0) {  // starts with
            const std::size_t len = std::string(lead).size();
            if (len > lead_len) { start = 0; lead_len = len; }
        }
    }
    if (start == std::string::npos) return std::nullopt;

    // Take the words after the lead-in (from the ORIGINAL text so casing survives).
    auto ws = words(t.substr(lead_len));
    // Drop a leading article/possessive the lead-in may not have absorbed.
    static const char* kArticles[] = {"my", "your", "a", "an", "the"};
    while (!ws.empty()) {
        std::string lw = lower(ws.front());
        bool is_article = false;
        for (const char* a : kArticles) if (lw == a) { is_article = true; break; }
        if (!is_article) break;
        ws.erase(ws.begin());
    }
    if (ws.empty()) return std::nullopt;

    // Whatever remains: the LAST alphabetic token is the name; anything before it
    // is the relationship ("daughter", "close friend").
    if (!is_alpha_word(ws.back())) return std::nullopt;
    Naming n;
    n.name = capitalize(ws.back());
    ws.pop_back();
    if (!ws.empty()) {
        std::string rel;
        for (const auto& w : ws) { if (!rel.empty()) rel += ' '; rel += lower(w); }
        // Store from the wearer's point of view: "my daughter" is spoken to ECHO,
        // but ECHO says "your daughter" back.
        n.relation = "your " + rel;
    }
    return n;
}

bool is_identity_query(const std::string& transcript) {
    const std::string t = lower(trim(transcript));
    static const char* kPhrases[] = {
        "who is this", "who's this", "who is that", "who's that",
        "who is he", "who is she", "who are they", "do you know them",
        "do you know him", "do you know her", "who am i looking at",
        "remind me who", "what's their name", "what is their name",
    };
    for (const char* p : kPhrases)
        if (t.find(p) != std::string::npos) return true;
    return false;
}

bool is_self_referential_query(const std::string& transcript) {
    const std::string t = lower(trim(transcript));

    // Identity queries belong to the face-recall path, not here. Checking this
    // first keeps the two classifiers disjoint by construction rather than by
    // careful phrase-list curation, which would drift.
    if (is_identity_query(t)) return false;

    // A phrase list, in the same style as is_identity_query, rather than a clever
    // grammatical rule. It is easy to audit, easy for a reviewer to argue with, and
    // it fails toward "not self-referential" on anything unusual — which is the
    // direction we want to fail in. Every entry pins BOTH a first-person reference
    // and a recall-shaped question, so "what did you say" and "who is the prime
    // minister" do not match.
    static const char* kPhrases[] = {
        // Past actions of the wearer.
        "did i", "have i", "had i", "when did i", "where did i", "what did i",
        "who did i", "why did i", "how did i", "what have i", "when was i",
        // Possessions and people belonging to the wearer's life.
        "did my", "has my", "have my", "when did my", "where is my", "where's my",
        "when was my", "where are my", "when is my",
        // Visits — asked about others, but a fact about the wearer's own history.
        "who visited", "who came by", "who came to see me", "who came over",
        "did anyone visit", "did anyone come", "has anyone visited",
        // Obligations the store holds as reminders.
        "am i supposed to", "what am i supposed to", "do i have to",
        "do i need to", "what do i need to", "what's on my", "what is on my",
        "am i meant to",
    };
    for (const char* p : kPhrases)
        if (t.find(p) != std::string::npos) return true;
    return false;
}

}  // namespace echo::memory
