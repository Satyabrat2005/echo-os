// ECHO OS — cognitive core.
//
// The orchestration layer around the on-device LLM. It takes structured
// perception, decides whether the system is confident enough to answer, and
// either produces a real response or falls back to a minimal, reassuring
// safe-mode response while flagging the caregiver app.
//
// Design principle #5 — "fail safe, not smart" — lives here. When confidence is
// low, we DO NOT let the LLM guess. We say something calm and known-good, and we
// tell the caregiver.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"
#include "echo/perception/perception_engine.hpp"
#include "echo/memory/memory_engine.hpp"

#include <memory>
#include <string>

namespace echo::cognitive {

// How the response was produced — the runtime and caregiver app both care.
enum class ResponseKind : std::uint8_t {
    Normal,    // LLM answered with sufficient confidence
    SafeMode,  // low confidence: reassuring fallback, caregiver flagged
};

struct Response {
    ResponseKind kind = ResponseKind::SafeMode;
    std::string  text;          // what voice-ui will speak
    Confidence   confidence;    // the confidence this decision was made at
    bool         flag_caregiver = false;  // true whenever we couldn't be sure
    // Optional app-intent routing hint from the LLM ("media", "mail", ... or ""
    // for a plain conversational answer). The runtime/apps layer may act on it;
    // it never affects the safe-mode decision above.
    std::string  intent;
};

// Tunable thresholds. Deliberately conservative: this device is worn by people
// with memory loss, so the cost of a confident wrong answer is high.
struct SafeModeConfig {
    // Below this aggregate confidence, we never surface an LLM answer.
    float min_confidence = 0.72f;
    // The fallback line spoken in safe mode. Short, warm, non-committal.
    std::string safe_response = "I'm not quite sure right now. Let's take a moment.";
};

class ICognitiveCore {
public:
    virtual ~ICognitiveCore() = default;

    // Load the quantized LLM (ONNX Runtime / TensorRT-equivalent) and warm it.
    virtual Status initialize() = 0;

    // Core decision: given perception, produce a response. Never throws; a model
    // failure degrades to safe mode rather than propagating. When a memory engine
    // is attached (see make_cognitive_core), a confidently-recognized known person
    // yields an enriched recall response, and a "[route:memory]" LLM reply is
    // answered from the real record instead of a guess.
    virtual Result<Response> respond(const perception::Perception& observation) = 0;

    virtual void shutdown() = 0;
};

// `memory` is a non-owning pointer to the on-device store (owned by the runtime),
// or nullptr to run without memory (the pre-Phase-15 behavior, used by tests and
// any core-only build). The core never takes ownership and never persists anything
// itself — all storage goes through the engine.
std::unique_ptr<ICognitiveCore> make_cognitive_core(SafeModeConfig config = {},
                                                    memory::IMemoryEngine* memory = nullptr);

}  // namespace echo::cognitive
