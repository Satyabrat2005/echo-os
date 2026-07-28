// ECHO OS — voice UI: TTS output and the audio-first interaction shell.
//
// This is the "face" of the OS. For a device with no screen worn by someone with
// memory loss, the voice IS the interface — so its tone, pacing, and smoothness
// get the same care a consumer OS gives its animations. Zero jank (principle #3)
// means audio must never stutter: synthesis is double-buffered and starts
// speaking within the 18 ms voice-output budget.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"
#include "echo/cognitive/cognitive_core.hpp"

#include <memory>
#include <string>

namespace echo::voice {

// Prosody hints. Safe-mode responses are spoken more slowly and warmly than
// normal ones — the tone is part of the reassurance.
enum class Tone : std::uint8_t {
    Neutral,
    Reassuring,  // used for safe-mode and distress contexts
    Alert,       // used sparingly, e.g. caregiver-directed prompts
};

struct Utterance {
    std::string text;
    Tone        tone = Tone::Neutral;
};

class IVoiceUi {
public:
    virtual ~IVoiceUi() = default;

    // Load the TTS voice and prime the audio output path (PAM8403 amp).
    virtual Status initialize() = 0;

    // Speak an utterance. Returns once playback has *started* (not finished) so
    // the core loop stays responsive; audio streams on the output thread.
    virtual Status speak(const Utterance& utterance) = 0;

    // Play a short non-speech audio cue (chime/earcon) — large, clear cues per
    // principle #1.
    virtual Status play_earcon(std::string_view name) = 0;

    // Stop any in-flight speech immediately (e.g. user interrupts).
    virtual void barge_in() = 0;

    virtual void shutdown() = 0;
};

// Map a cognitive response to a spoken utterance, choosing tone from its kind.
Utterance to_utterance(const cognitive::Response& response);

std::unique_ptr<IVoiceUi> make_voice_ui();

}  // namespace echo::voice
