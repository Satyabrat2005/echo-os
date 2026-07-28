// ECHO OS apps — the command router (the host-side broker).
//
// The router is the hub of the apps layer: it knows every registered app, maps
// an incoming intent to the one app that claims it, checks that app's
// permissions, invokes it, and fans the result out to the two output surfaces —
// the HUD compositor (visual) and the voice bridge (audio). Apps never see each
// other and never see the compositor or voice-ui; the router is the only thing
// that does.
//
// This is the in-process broker used by the laptop host and the smoke tests. The
// same routing logic sits behind the supervised-process path: there, route()'s
// app invocation is a message over a pipe instead of a direct call, but the
// intent-matching, permission gate, and output fan-out are identical.
#pragma once

#include "echo/apps/framework/app.hpp"
#include "echo/apps/framework/permission.hpp"
#include "echo/apps/framework/voice_bridge.hpp"
#include "echo/apps/hud/hud.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace echo::apps {

// Outcome of routing one command.
struct RouteResult {
    bool        handled = false;   // did any app claim this intent?
    bool        permitted = true;  // was it allowed to run?
    std::string app_id;            // which app handled it (if any)
    AppResponse response;          // what it produced (valid iff handled && permitted)
};

class Router {
public:
    // Outputs are borrowed, not owned. Either may be null in a headless test
    // that only cares about the returned AppResponse.
    Router(hud::IHudCompositor* hud, IVoiceBridge* voice, const PermissionModel* permissions);

    // Register an app (borrowed). Builds the intent -> app index and records the
    // app's declared intents into the router vocabulary.
    void register_app(IApp* app);

    // The full intent vocabulary across registered apps — feed to the NLU parser.
    const std::vector<std::string>& vocabulary() const noexcept { return vocabulary_; }

    // Route a parsed command: match -> permission-gate -> invoke -> present/speak.
    RouteResult route(const VoiceCommand& command);

    // Look up the app that claims an intent (or by app id), or nullptr.
    IApp* find(std::string_view intent) const;

private:
    hud::IHudCompositor*   hud_;
    IVoiceBridge*          voice_;
    const PermissionModel* permissions_;

    std::vector<IApp*>                        apps_;
    std::unordered_map<std::string, IApp*>    by_intent_;  // intent  -> app
    std::unordered_map<std::string, IApp*>    by_id_;      // app id  -> app
    std::vector<std::string>                  vocabulary_;
};

}  // namespace echo::apps
