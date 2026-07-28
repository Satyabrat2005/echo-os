#include "echo/apps/text/readable.hpp"

#include <algorithm>
#include <cctype>

namespace echo::apps::text {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Remove every <tagname ...>...</tagname> block (case-insensitive), including its
// contents. Used to delete <script>, <style>, <head> wholesale.
void strip_block(std::string& html, const std::string& tag) {
    const std::string lo = lower(html);
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    std::string out;
    std::size_t pos = 0;
    while (pos < html.size()) {
        std::size_t start = lo.find(open, pos);
        if (start == std::string::npos) {
            out.append(html, pos, std::string::npos);
            break;
        }
        out.append(html, pos, start - pos);
        std::size_t end = lo.find(close, start);
        if (end == std::string::npos) break;  // unterminated: drop the rest
        pos = end + close.size();
    }
    html.swap(out);
}

// Decode the handful of entities that actually show up in prose.
std::string decode_entities(const std::string& s) {
    struct E { const char* name; const char* rep; };
    static const E kEntities[] = {
        {"&amp;", "&"},   {"&lt;", "<"},    {"&gt;", ">"},
        {"&quot;", "\""}, {"&#39;", "'"},   {"&apos;", "'"},
        {"&nbsp;", " "},  {"&mdash;", "—"}, {"&ndash;", "–"},
        {"&hellip;", "…"},
    };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            bool matched = false;
            for (const auto& e : kEntities) {
                std::size_t len = std::char_traits<char>::length(e.name);
                if (s.compare(i, len, e.name) == 0) {
                    out += e.rep;
                    i += len;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

// Replace tags with spaces and collapse runs of whitespace to single spaces.
std::string strip_tags_and_collapse(const std::string& html) {
    std::string no_tags;
    no_tags.reserve(html.size());
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') { in_tag = true; no_tags.push_back(' '); }
        else if (c == '>') { in_tag = false; no_tags.push_back(' '); }
        else if (!in_tag) no_tags.push_back(c);
    }
    std::string decoded = decode_entities(no_tags);

    std::string out;
    out.reserve(decoded.size());
    bool prev_space = false;
    for (char c : decoded) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_space) out.push_back(' ');
            prev_space = true;
        } else {
            out.push_back(c);
            prev_space = false;
        }
    }
    // trim ends
    if (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string extract_title(const std::string& html) {
    const std::string lo = lower(html);
    std::size_t start = lo.find("<title");
    if (start == std::string::npos) return {};
    std::size_t gt = lo.find('>', start);
    if (gt == std::string::npos) return {};
    std::size_t end = lo.find("</title>", gt);
    if (end == std::string::npos) return {};
    std::string raw = html.substr(gt + 1, end - gt - 1);
    return strip_tags_and_collapse(decode_entities(raw));
}

}  // namespace

Readable extract(const std::string& html) {
    Readable r;
    r.title = extract_title(html);

    std::string body = html;
    strip_block(body, "script");
    strip_block(body, "style");
    strip_block(body, "head");
    strip_block(body, "noscript");
    strip_block(body, "svg");
    r.text = strip_tags_and_collapse(body);
    return r;
}

std::string summarize(const std::string& text, std::size_t max_chars) {
    if (text.size() <= max_chars) return text;
    std::size_t cut = text.rfind(' ', max_chars);
    if (cut == std::string::npos || cut == 0) cut = max_chars;
    std::string out = text.substr(0, cut);
    // drop trailing punctuation/space before the ellipsis
    while (!out.empty() && (out.back() == ' ' || out.back() == ',' || out.back() == '.'))
        out.pop_back();
    out += "…";
    return out;
}

}  // namespace echo::apps::text
