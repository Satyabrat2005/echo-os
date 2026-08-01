// ECHO OS cognitive-core — the on-device LLM (private component).
//
// Real path is llama.cpp with a small quantized instruct GGUF (see README
// "Phase 3" for the exact model + quantization). Fallback is a stub that returns
// an empty string, which makes the core degrade to safe mode rather than guess.
//
// IMPORTANT: this component is *only ever called after* the cognitive core's
// confidence-threshold safe-mode gate has passed (principle #5). It never sees a
// low-confidence observation, and an empty return here also routes to safe mode.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <memory>
#include <string>

namespace echo::cognitive {

// A structured LLM decision: a short spoken reply plus an optional app-intent
// hint the runtime can route on ("media", "mail", ... or "" for a plain answer).
struct LlmReply {
    std::string text;    // what ECHO says (empty => generation failed => safe mode)
    std::string intent;  // routing hint, or "" for a conversational answer

    // --- Answer-side confidence (Phase 21) -----------------------------------
    // The mean probability the model assigned to the tokens it actually emitted,
    // derived from the real logits (see llm.cpp). This is the LLM analogue of the
    // mean-token-probability confidence perception already derives for ASR.
    //
    // WHAT IT MEASURES: fluency — how sure the model was of *those words*. It falls
    // for degenerate, collapsed, or near-random generation.
    // WHAT IT DOES NOT MEASURE: truth. A small model states a wrong fact just as
    // confidently as a right one, which is exactly what Phase 12 observed. Do not
    // read a high value here as "the answer is correct".
    //
    // `scored` is false when the backend cannot produce a confidence at all (the
    // stub, or a llama.cpp build whose logits are unavailable). An unknown score is
    // NOT the same as a zero score, so the caller must skip the floor rather than
    // treat unscored output as maximally suspect.
    Confidence confidence{0.0f};
    bool       scored = false;
};

class ILlm {
public:
    virtual ~ILlm() = default;
    virtual Status   initialize() = 0;
    // Produce a reply for a user utterance. Must never throw; returns an empty
    // LlmReply.text on any failure so the caller falls back to safe mode.
    virtual LlmReply generate(const std::string& user_text) = 0;
    virtual void     shutdown() = 0;
};

std::unique_ptr<ILlm> make_llm();

}  // namespace echo::cognitive
