#include "echo/apps/net/json.hpp"

#include <cstdlib>

namespace echo::apps::net {

namespace {
const Json& null_value() {
    static const Json kNull;  // default-constructed == Type::Null
    return kNull;
}
}  // namespace

bool Json::as_bool(bool def) const noexcept {
    return type_ == Type::Bool ? bool_ : def;
}

double Json::as_number(double def) const noexcept {
    return type_ == Type::Number ? number_ : def;
}

const Json& Json::operator[](const std::string& key) const noexcept {
    if (type_ == Type::Object)
        for (const auto& m : object_)
            if (m.first == key) return m.second;
    return null_value();
}

bool Json::contains(const std::string& key) const noexcept {
    if (type_ != Type::Object) return false;
    for (const auto& m : object_)
        if (m.first == key) return true;
    return false;
}

const Json& Json::operator[](std::size_t index) const noexcept {
    if (type_ == Type::Array && index < array_.size()) return array_[index];
    return null_value();
}

std::size_t Json::size() const noexcept {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

std::string Json::str_or(const std::string& key, const std::string& def) const {
    const Json& v = (*this)[key];
    return v.is_string() ? v.as_string() : def;
}

// --- Recursive-descent parser ------------------------------------------------
// A single-pass parser over the input string. Every parse_* returns false on
// malformed input and leaves the cursor wherever it failed; the top-level parse()
// turns any failure into a Null value.
class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool parse_document(Json& out) {
        skip_ws();
        if (!parse_value(out, 0)) return false;
        skip_ws();
        return pos_ == s_.size();  // reject trailing garbage
    }

private:
    const std::string& s_;
    std::size_t        pos_ = 0;

    // Cap on object/array nesting. Real API responses nest only a handful deep;
    // this exists solely so a hostile or corrupt payload of thousands of nested
    // brackets fails as a Null value instead of overflowing the stack. Honoring
    // the module contract: "get garbage back from the network, degrade like any
    // other failure" — never crash.
    static constexpr int kMaxDepth = 200;

    bool eof() const { return pos_ >= s_.size(); }
    char peek() const { return s_[pos_]; }

    void skip_ws() {
        while (!eof()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool parse_value(Json& out, int depth) {
        skip_ws();
        if (eof()) return false;
        char c = peek();
        switch (c) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string str;
                if (!parse_string(str)) return false;
                out.type_   = Json::Type::String;
                out.string_ = std::move(str);
                return true;
            }
            case 't': case 'f': return parse_bool(out);
            case 'n': return parse_null(out);
            default:  return parse_number(out);
        }
    }

    bool parse_object(Json& out, int depth) {
        if (depth >= kMaxDepth) return false;  // too deeply nested; fail, don't recurse
        ++pos_;  // consume '{'
        out.type_ = Json::Type::Object;
        skip_ws();
        if (!eof() && peek() == '}') { ++pos_; return true; }
        while (true) {
            skip_ws();
            if (eof() || peek() != '"') return false;
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (eof() || peek() != ':') return false;
            ++pos_;  // consume ':'
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.object_.emplace_back(std::move(key), std::move(value));
            skip_ws();
            if (eof()) return false;
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == '}') { ++pos_; return true; }
            return false;
        }
    }

    bool parse_array(Json& out, int depth) {
        if (depth >= kMaxDepth) return false;  // too deeply nested; fail, don't recurse
        ++pos_;  // consume '['
        out.type_ = Json::Type::Array;
        skip_ws();
        if (!eof() && peek() == ']') { ++pos_; return true; }
        while (true) {
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.array_.push_back(std::move(value));
            skip_ws();
            if (eof()) return false;
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == ']') { ++pos_; return true; }
            return false;
        }
    }

    // Append the UTF-8 encoding of a Unicode code point to out.
    static void append_utf8(unsigned cp, std::string& out) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_hex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return false;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // consume opening quote
        while (!eof()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (eof()) return false;
                char esc = s_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!parse_hex4(cp)) return false;
                        // Combine a high/low surrogate pair if present.
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() &&
                            s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            unsigned lo = 0;
                            if (!parse_hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        append_utf8(cp, out);
                        break;
                    }
                    default: return false;  // invalid escape
                }
            } else {
                out.push_back(c);
            }
        }
        return false;  // unterminated string
    }

    bool parse_bool(Json& out) {
        if (s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out.type_ = Json::Type::Bool;
            out.bool_ = true;
            return true;
        }
        if (s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out.type_ = Json::Type::Bool;
            out.bool_ = false;
            return true;
        }
        return false;
    }

    bool parse_null(Json& out) {
        if (s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out.type_ = Json::Type::Null;
            return true;
        }
        return false;
    }

    bool parse_number(Json& out) {
        std::size_t start = pos_;
        if (!eof() && peek() == '-') ++pos_;
        bool any = false;
        while (!eof()) {
            char c = peek();
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') {
                ++pos_;
                any = true;
            } else {
                break;
            }
        }
        if (!any) return false;
        const std::string tok = s_.substr(start, pos_ - start);
        char*       end = nullptr;
        const double v  = std::strtod(tok.c_str(), &end);
        if (end != tok.c_str() + tok.size()) return false;
        out.type_   = Json::Type::Number;
        out.number_ = v;
        return true;
    }
};

Json Json::parse(const std::string& text) {
    Json       out;
    JsonParser parser(text);
    if (!parser.parse_document(out)) return Json{};  // Null on any error
    return out;
}

}  // namespace echo::apps::net
