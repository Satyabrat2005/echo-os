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
//
// There are three distinct reasons ECHO can decline, and they are kept SEPARATE on
// purpose (the same discipline Phase 17 used to keep "engine didn't respond" apart
// from "engine responded with low confidence"). Collapsing them would destroy the
// only signal a caregiver has for telling a broken device from a careful one:
//
//   SafeMode   — we did not understand the INPUT well enough to act on it.
//   Unverified — we understood the input fine, the engine answered, but the answer
//                could not be grounded in the record (or scored well enough) to
//                assert to someone who cannot fact-check it.
//   (engine fault — the engine did not respond at all — is not a ResponseKind; the
//    runtime handles it with its own known-good line, see boot/.../runtime.hpp.)
enum class ResponseKind : std::uint8_t {
    Normal,      // answered with sufficient confidence, or recalled from the record
    SafeMode,    // low INPUT confidence: reassuring fallback, caregiver flagged
    Unverified,  // clear input, but the ANSWER isn't grounded enough to assert (Phase 21)
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
    // The answer-side score (Phase 21), when the LLM backend produced one; 0 when
    // the answer was retrieved rather than generated, or the backend cannot score.
    // Distinct from `confidence` above, which is always the PERCEPTION aggregate.
    Confidence   answer_confidence;
};

// Tunable thresholds. Deliberately conservative: this device is worn by people
// with memory loss, so the cost of a confident wrong answer is high.
struct SafeModeConfig {
    // Below this aggregate confidence, we never surface an LLM answer.
    float min_confidence = 0.72f;
    // The fallback line spoken in safe mode. Short, warm, non-committal.
    std::string safe_response = "I'm not quite sure right now. Let's take a moment.";

    // --- Answer-side gate (Phase 21) ----------------------------------------
    // A DEGENERACY FLOOR on the LLM's own mean token probability, not a truth test.
    // Read the honest limit before tuning this: mean token probability measures how
    // confidently the model produced *those words*, which catches near-random or
    // collapsed generation and does NOT catch a fluent, well-formed falsehood. It is
    // set low on purpose — a small greedy-decoded instruct model sits around 0.7–0.95
    // on ordinary output, so anything near the top of that range would reject good
    // answers while still admitting confident wrong ones. The real protection against
    // a wrong answer about the wearer's own life is grounding (see is_self_referential
    // _query below), not this number. The `real-llm` CI job prints the measured
    // distribution so this can be revised from data rather than from taste.
    float min_answer_confidence = 0.35f;

    // The line spoken when ECHO understood the question but will not assert an answer.
    // Deliberately different from `safe_response`: this says "I don't know", where
    // safe mode says "I didn't follow". A wearer, and a caregiver reading the log,
    // must be able to tell those apart.
    std::string unverified_response =
        "I don't have a record of that, so I'd rather not guess.";
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
    //
    // Phase 21 adds the ANSWER-side half of that guarantee: a question about the
    // wearer's own life is answered from the store or not at all, and a generated
    // answer that scores below the degeneracy floor is declined. Both produce
    // ResponseKind::Unverified rather than a sentence the wearer cannot check.
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
