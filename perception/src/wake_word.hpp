// ECHO OS perception — wake-word detector (private component).
//
// Runs continuously on the laptop mic with negligible idle CPU and fires on the
// custom "Hey ECHO" phrase. The real path is Picovoice Porcupine (a tiny, fully
// on-device keyword-spotting DNN); the fallback is a stub that never fires (so
// the demo host falls back to its keyboard "wake" affordance). Either way the
// engine above it only ever sees IWakeWord.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace echo::perception {

struct WakeResult {
    bool  detected = false;
    float confidence = 0.0f;
};

class IWakeWord {
public:
    virtual ~IWakeWord() = default;
    virtual Status initialize() = 0;
    // Native sample rate the detector expects (16 kHz for Porcupine).
    virtual int    sample_rate() const noexcept = 0;
    // Number of int16 samples per process() call (Porcupine frame length).
    virtual int    frame_length() const noexcept = 0;
    // Feed exactly frame_length() mono int16 samples; reports a detection.
    virtual WakeResult process(const std::int16_t* pcm, std::size_t n) = 0;
    virtual void   shutdown() = 0;
};

std::unique_ptr<IWakeWord> make_wake_word();

}  // namespace echo::perception
