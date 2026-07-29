// ECHO OS apps — the app contract.
//
// Every app is voice-first (constraint #1): its entire interface is a voice
// command in, and a spoken line + a minimal HUD frame out. There is no GUI to
// navigate, no view hierarchy, no event loop the app owns. An app implements
// IApp and does exactly one thing per turn: turn a VoiceCommand into an
// AppResponse.
//
// An app depends only on this SDK (this header, permission.hpp, hud.hpp). It
// never includes a core header (voice-ui, cognitive-core, perception). The host
// framework is what bridges an AppResponse to the core's voice output — see
// voice_bridge.hpp — so the isolation boundary (constraint #2) is visible in the
// dependency graph, not just asserted in comments.
#pragma once

#include "echo/apps/framework/permission.hpp"
#include "echo/apps/hud/hud.hpp"
#include "echo/types.hpp"
#include "echo/result.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace echo::apps {

// Static description of an app, read by the host to register and gate it.
struct AppMetadata {
    std::string              id;             // stable slug: "media", "mail", ...
    std::string              name;           // human label: "Media"
    PermissionSet            required_caps;  // what the app needs to function
    std::vector<std::string> intents;        // verbs/keywords it claims to handle
};

// A parsed voice command. `intent` is the normalized verb the router matched on;
// `text` is the raw utterance; `slots` carries extracted arguments (e.g.
// {"query": "kind of blue"}). Kept as plain strings — no core NLU types leak in.
struct VoiceCommand {
    std::string                         intent;
    std::string                         text;
    std::map<std::string, std::string>  slots;

    std::string slot(const std::string& key, const std::string& fallback = {}) const {
        auto it = slots.find(key);
        return it == slots.end() ? fallback : it->second;
    }
};

// Tone for the spoken reply. Deliberately the app-side vocabulary — the voice
// bridge maps this onto the core's voice::Tone so apps never touch voice-ui.
enum class SpeechTone : std::uint8_t {
    Neutral = 0,
    Reassuring,  // gentle, e.g. "there are no new messages"
    Alert,       // used sparingly, e.g. an incoming call
};

// What an app produces for one command: a line to speak and a frame to show.
// Both are optional; a purely spoken reply leaves the HUD blank, and a glanceable
// status update may leave `speech` empty.
struct AppResponse {
    Status              status = Status::Ok;
    std::string         speech;              // spoken by the core via the bridge
    SpeechTone          tone   = SpeechTone::Neutral;
    hud::HudFrame       hud;                 // drawn via the compositor primitives

    static AppResponse say(std::string text, SpeechTone tone = SpeechTone::Neutral) {
        AppResponse r;
        r.speech = std::move(text);
        r.tone   = tone;
        return r;
    }
    AppResponse& show(hud::HudFrame frame) {
        hud = std::move(frame);
        return *this;
    }
};

// The app itself. Implementations are created by a per-app factory (e.g.
// media::make_media_app(mock)). Everything here is single-threaded from the
// app's point of view: the host serializes commands to it.
class IApp {
public:
    virtual ~IApp() = default;

    virtual const AppMetadata& metadata() const = 0;

    // Warm up whatever the app needs (open mock inbox, load track list, ...).
    virtual Status initialize() = 0;

    // The one hot method: handle a command, return what to say and show. Must
    // never throw — a failure degrades to an AppResponse carrying a non-Ok
    // status and a calm spoken line, mirroring the core's fail-safe rule.
    virtual AppResponse on_command(const VoiceCommand& command) = 0;

    virtual void shutdown() = 0;
};

}  // namespace echo::apps
