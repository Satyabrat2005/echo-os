// ECHO OS tests — a scripted ILlm for exercising the answer-side gate (Phase 21).
//
// Companion to fake_engines.hpp, which fakes whole ENGINES at the runtime boundary.
// This fakes one level deeper: the LLM inside the cognitive core, so a test can put
// the core in the exact situation the answer gate exists for and that neither real
// backend can produce on demand —
//
//   * the stub LLM always returns empty text, which routes to safe mode, so it can
//     never exercise the "model answered, and the answer is the problem" path;
//   * a real llama.cpp model answers fluently and confidently for EVERY prompt
//     (the Phase 12 finding), so it can never be made to fail on cue.
//
// A scripted fake is the only way to write "the model said something wrong, in a
// confident voice" as a test. That gap — not a lack of awareness — is why the
// ungated answer path survived twenty phases.
//
// Framework-free and deterministic, like the rest of tests/.
#pragma once

#include "llm.hpp"  // cognitive-core/src (private); see tests/CMakeLists.txt

#include <string>
#include <utility>
#include <vector>

namespace echo::test {

// An ILlm that replays whatever the test tells it to say.
//
// Default behaviour is a plain, confident, conversational answer — so a test that
// only cares about one property sets that one property and inherits a realistic
// baseline for the rest.
class FakeLlm final : public cognitive::ILlm {
public:
    // --- script ------------------------------------------------------------
    std::string text   = "It's a quiet afternoon.";  // what the "model" says
    std::string intent;                              // route tag, "" = conversational
    float       confidence = 0.90f;                  // mean token probability
    bool        scored     = true;                   // false => backend cannot score
    Status      init_status = Status::Ok;

    // --- observations ------------------------------------------------------
    // Every prompt the core actually sent us. The single most important assertion
    // in the whole answer-gate suite is that this stays EMPTY for a question about
    // the wearer's own life with no stored record: not "the model answered and we
    // suppressed it", but "the model was never asked".
    std::vector<std::string> prompts;
    int  init_calls     = 0;
    int  shutdown_calls = 0;

    bool consulted() const noexcept { return !prompts.empty(); }

    Status initialize() override {
        ++init_calls;
        return init_status;
    }

    cognitive::LlmReply generate(const std::string& user_text) override {
        prompts.push_back(user_text);
        cognitive::LlmReply r;
        r.text       = text;
        r.intent     = intent;
        r.confidence = Confidence{confidence};
        r.scored     = scored;
        return r;
    }

    void shutdown() override { ++shutdown_calls; }
};

}  // namespace echo::test
