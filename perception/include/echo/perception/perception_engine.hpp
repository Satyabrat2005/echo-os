// ECHO OS — perception engine.
//
// Turns raw sensor frames into structured, confidence-scored observations that
// the cognitive core can reason over. All models are quantized and run locally;
// nothing here reaches the network. This stage owns the largest slice of the
// latency budget (45 ms) because it runs the heaviest models.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace echo::perception {

// A recognized face, anchored to a caregiver-provided identity when known.
struct FaceObservation {
    std::string identity;   // "" when unknown / not yet anchored
    Confidence  confidence;
    // Bounding box in normalized [0,1] frame coordinates.
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// A recognized object/scene label (lightweight quantized CNN).
struct ObjectObservation {
    std::string label;
    Confidence  confidence;
};

// Wake-word detection result.
struct WakeWord {
    bool       detected = false;
    Confidence confidence;
};

// Automatic speech recognition result (Whisper-class, quantized).
struct Transcript {
    std::string text;
    Confidence  confidence;
    bool        endpointed = false;  // true once the utterance is complete
};

// The aggregate output of one perception pass over the current sensor window.
struct Perception {
    std::optional<WakeWord>        wake;
    std::optional<Transcript>      speech;
    std::vector<FaceObservation>   faces;
    std::vector<ObjectObservation> objects;

    // The single confidence the cognitive core gates on: the weakest link across
    // whatever modalities actually contributed to this observation.
    Confidence aggregate_confidence() const noexcept;
};

class IPerceptionEngine {
public:
    virtual ~IPerceptionEngine() = default;

    // Load and warm quantized models. Called once during boot.
    virtual Status initialize() = 0;

    // Run a perception pass over a single frame. Returns the incremental
    // observation; the engine internally fuses across frames/modalities.
    virtual Result<Perception> process(const SensorFrame& frame) = 0;

    // Release accelerator resources.
    virtual void shutdown() = 0;
};

std::unique_ptr<IPerceptionEngine> make_perception_engine();

}  // namespace echo::perception
