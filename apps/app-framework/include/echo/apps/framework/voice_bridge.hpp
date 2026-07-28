// ECHO OS apps — the voice bridge (apps -> core voice-ui, one-way, mediated).
//
// This is the ONLY place the apps layer touches the safety-critical core's audio
// output, and it does so through voice-ui's public interface — never its
// internals (constraint #2). Apps hand the host an AppResponse with a line to
// speak; the host asks the bridge to voice it; the bridge maps the app-side
// SpeechTone onto the core's voice::Tone and calls voice-ui.
//
// Crucially, the bridge is best-effort and subordinate: it must never block or
// preempt the core's own speech. If the core is mid-utterance on a safety path,
// an app's chatter waits or is dropped — the core always wins the audio channel.
#pragma once

#include "echo/apps/framework/app.hpp"
#include "echo/voice/voice_ui.hpp"

#include <memory>
#include <string_view>

namespace echo::apps {

class IVoiceBridge {
public:
    virtual ~IVoiceBridge() = default;

    // Speak an app's line. Non-blocking with respect to the core loop; returns
    // whether the request was accepted (it may be declined if the core holds the
    // channel). `app_id` is for attribution/earcon selection only.
    virtual bool speak(std::string_view app_id, std::string_view text, SpeechTone tone) = 0;
};

// Wrap a core voice-ui instance (owned by the host/runtime) as an app-facing
// bridge. The bridge borrows `ui`; it does not own or shut it down.
std::unique_ptr<IVoiceBridge> make_voice_bridge(voice::IVoiceUi* ui);

}  // namespace echo::apps
