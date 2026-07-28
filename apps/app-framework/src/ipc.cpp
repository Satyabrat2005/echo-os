#include "echo/apps/framework/ipc.hpp"

#include <sstream>

namespace echo::apps::ipc {

namespace {

// Escape a value so it can live in a tab-separated line: backslash, tab, and
// newline become \\ \t \n. decode() reverses it. Keys are constrained to plain
// identifiers by construction, so only values need escaping.
std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string unescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            char n = in[++i];
            switch (n) {
                case '\\': out += '\\'; break;
                case 't':  out += '\t'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                default:   out += n;    break;
            }
        } else {
            out += in[i];
        }
    }
    return out;
}

}  // namespace

std::string encode(const Message& msg) {
    std::string line = "kind=";
    line += (msg.kind == MessageKind::Cmd ? "cmd" : "rsp");
    for (const auto& [k, v] : msg.fields) {
        line += '\t';
        line += k;
        line += '=';
        line += escape(v);
    }
    return line;
}

std::optional<Message> decode(const std::string& line) {
    Message msg;
    bool have_kind = false;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '\t')) {
        auto eq = field.find('=');
        if (eq == std::string::npos) continue;
        std::string key = field.substr(0, eq);
        std::string val = unescape(field.substr(eq + 1));
        if (key == "kind") {
            msg.kind  = (val == "rsp") ? MessageKind::Rsp : MessageKind::Cmd;
            have_kind = true;
        } else {
            msg.fields[key] = std::move(val);
        }
    }
    if (!have_kind) return std::nullopt;
    return msg;
}

Message to_message(const VoiceCommand& command) {
    Message m;
    m.kind = MessageKind::Cmd;
    m.fields["intent"] = command.intent;
    m.fields["text"]   = command.text;
    for (const auto& [k, v] : command.slots) {
        m.fields["slot." + k] = v;
    }
    return m;
}

VoiceCommand to_command(const Message& msg) {
    VoiceCommand c;
    for (const auto& [k, v] : msg.fields) {
        if (k == "intent")      c.intent = v;
        else if (k == "text")   c.text   = v;
        else if (k.rfind("slot.", 0) == 0) c.slots[k.substr(5)] = v;
    }
    return c;
}

Message to_message(const AppResponse& response) {
    Message m;
    m.kind = MessageKind::Rsp;
    m.fields["status"] = std::to_string(static_cast<int>(response.status));
    m.fields["tone"]   = std::to_string(static_cast<int>(response.tone));
    m.fields["speech"] = response.speech;
    if (response.hud.has_subtitle) {
        m.fields["hud.subtitle"] = response.hud.subtitle.text;
        m.fields["hud.ttl"]      = std::to_string(response.hud.subtitle.ttl_ms);
    }
    if (response.hud.has_icon)
        m.fields["hud.icon"] = std::to_string(static_cast<int>(response.hud.icon.glyph));
    if (response.hud.has_status)
        m.fields["hud.status"] = std::to_string(static_cast<int>(response.hud.status.kind));
    return m;
}

AppResponse to_response(const Message& msg) {
    AppResponse r;
    auto get = [&](const char* k) -> const std::string* {
        auto it = msg.fields.find(k);
        return it == msg.fields.end() ? nullptr : &it->second;
    };
    if (auto* s = get("status")) r.status = static_cast<Status>(std::stoi(*s));
    if (auto* t = get("tone"))   r.tone   = static_cast<SpeechTone>(std::stoi(*t));
    if (auto* sp = get("speech")) r.speech = *sp;
    if (auto* sub = get("hud.subtitle")) {
        std::uint16_t ttl = 4000;
        if (auto* t = get("hud.ttl")) ttl = static_cast<std::uint16_t>(std::stoi(*t));
        r.hud.with_subtitle(*sub, ttl);
    }
    if (auto* ic = get("hud.icon"))
        r.hud.with_icon(static_cast<hud::Glyph>(std::stoi(*ic)));
    if (auto* st = get("hud.status"))
        r.hud.with_status(static_cast<hud::StatusKind>(std::stoi(*st)));
    return r;
}

}  // namespace echo::apps::ipc
