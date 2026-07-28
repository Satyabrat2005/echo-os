// ECHO OS perception — speech-to-text (private component).
//
// Transcribes a captured utterance locally. Real path is whisper.cpp with a
// small quantized model (base.en / small.en); fallback is a stub that returns an
// empty, low-confidence transcript. The engine feeds it a complete utterance
// (batch-on-silence) — see perception_engine.cpp for the endpointing.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace echo::perception {

struct AsrResult {
    std::string text;
    float       confidence = 0.0f;
};

class IAsr {
public:
    virtual ~IAsr() = default;
    virtual Status initialize() = 0;
    // Transcribe mono int16 PCM at `sample_rate` (resampled internally to 16 kHz
    // if needed). Returns the recognized text and a [0,1] confidence.
    virtual AsrResult transcribe(const std::int16_t* pcm, std::size_t n, int sample_rate) = 0;
    virtual void   shutdown() = 0;
};

std::unique_ptr<IAsr> make_asr();

}  // namespace echo::perception
