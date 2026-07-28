#include "echo/apps/framework/router.hpp"
#include "echo/log.hpp"

#include <algorithm>

namespace echo::apps {

Router::Router(hud::IHudCompositor* hud, IVoiceBridge* voice, const PermissionModel* permissions)
    : hud_(hud), voice_(voice), permissions_(permissions) {}

void Router::register_app(IApp* app) {
    if (!app) return;
    const AppMetadata& meta = app->metadata();
    apps_.push_back(app);
    by_id_[meta.id] = app;
    for (const auto& intent : meta.intents) {
        // First app to claim an intent owns it; a later collision is logged and
        // ignored so registration order is deterministic policy, not a surprise.
        auto [it, inserted] = by_intent_.emplace(intent, app);
        if (!inserted) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "intent '%s' claimed by '%s', ignoring '%s'",
                          intent.c_str(), it->second->metadata().id.c_str(), meta.id.c_str());
            log_warn("router", buf);
            continue;
        }
        if (std::find(vocabulary_.begin(), vocabulary_.end(), intent) == vocabulary_.end())
            vocabulary_.push_back(intent);
    }
}

IApp* Router::find(std::string_view intent) const {
    std::string key(intent);
    if (auto it = by_intent_.find(key); it != by_intent_.end()) return it->second;
    if (auto it = by_id_.find(key);     it != by_id_.end())     return it->second;
    return nullptr;
}

RouteResult Router::route(const VoiceCommand& command) {
    RouteResult result;

    IApp* app = find(command.intent);
    if (!app) {
        result.handled = false;
        return result;  // nothing claims this intent; caller decides what to say
    }

    result.handled = true;
    result.app_id  = app->metadata().id;

    // Permission gate: an app runs a command only if it holds every capability it
    // declared. This is the enforcement point for privacy-by-default.
    if (permissions_ && !permissions_->satisfies(app->metadata().id, app->metadata().required_caps)) {
        result.permitted = false;
        AppResponse denied = AppResponse::say(
            "I can't do that right now — that app doesn't have permission.",
            SpeechTone::Reassuring);
        denied.status = Status::Unavailable;
        denied.hud.with_icon(hud::Glyph::Warning).with_status(hud::StatusKind::Error);
        result.response = denied;
        if (hud_)   hud_->present(result.app_id, result.response.hud);
        if (voice_) voice_->speak(result.app_id, result.response.speech, result.response.tone);
        return result;
    }

    // Invoke the app and fan the result out to the two output surfaces.
    result.response = app->on_command(command);
    if (hud_ && !result.response.hud.empty())
        hud_->present(result.app_id, result.response.hud);
    if (voice_ && !result.response.speech.empty())
        voice_->speak(result.app_id, result.response.speech, result.response.tone);

    return result;
}

}  // namespace echo::apps
