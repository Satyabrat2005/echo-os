// ECHO OS — REAL reasoning-LLM verification (Phase 12).
//
// Phase 11 deliberately left the LLM path out of real-engine CI, judging a
// full-size 3B–4B model impractical to run per-commit. Phase 12 narrows the
// ambition rather than abandoning it: this target is built ONLY with
// -DECHO_WITH_LLAMA=ON and drives the REAL llama.cpp adapter
// (cognitive-core/src/llm.cpp -> LlamaLlm) against a genuinely tiny quantized
// instruct model (Qwen2.5-0.5B-Instruct Q5_K_M, see MANIFEST.md).
//
// What this proves — against a real model's REAL output, not the stub's canned
// empty reply:
//   1. Route-tag parsing (route_tag.cpp, hardened in Phase 6) correctly extracts
//      the app from the model's actual output format.
//   2. The safe-mode confidence gate (cognitive_core.cpp, principle #5) still
//      fires correctly with a real LLM wired in behind it.
//   3. The real llama.cpp integration loads, generates, and returns non-empty,
//      non-crashing text for a simple prompt.
//
// What this does NOT prove (kept honest, see docs/STATE.md): production reasoning
// quality. A 0.5B model exists here only to exercise the real *integration*; the
// larger model used in the real deployment is not validated by this test.
//
// IMPORTANT (constraint #4): we do NOT weaken or bypass the safe-mode gate to make
// a test pass. A real model produces confident text for *every* prompt — it never
// returns the empty string the stub does — so the tiny model's output does NOT
// itself trip the LLM-failure fallback the way the stub does. That is a real
// finding, documented in docs/STATE.md, not routed around: ECHO's safe-mode gate
// keys on PERCEPTION confidence and short-circuits before the LLM, which is what
// the low-confidence test below asserts, with the real model present.
//
// Assertions are deliberately lenient on content: exact model wording is not
// deterministic across environments even at greedy temperature, so we assert on
// the route decision, on non-emptiness, and on the gate's behavior — never on a
// brittle exact string. (Greedy decoding does make the route decision itself
// stable, which is what makes assertion #1 safe.)

#if !defined(ECHO_WITH_LLAMA)
#error "real_llm_test requires ECHO_WITH_LLAMA=ON — it verifies the real llama.cpp engine"
#endif

#include "llm.hpp"  // cognitive-core/src (private): make_llm()/ILlm/LlmReply

#include "echo/cognitive/cognitive_core.hpp"
#include "echo/perception/perception_engine.hpp"
#include "echo/config.hpp"
#include "echo/result.hpp"
#include "echo/types.hpp"

#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace echo;
using echo::cognitive::ICognitiveCore;
using echo::cognitive::ILlm;
using echo::cognitive::LlmReply;
using echo::cognitive::make_cognitive_core;
using echo::cognitive::make_llm;
using echo::cognitive::ResponseKind;
using echo::cognitive::SafeModeConfig;

namespace {

// A perception observation carrying a real ASR-style transcript at a chosen
// confidence — the same OUTPUT CONTRACT the Phase 10 e2e fixture test builds (a
// real wake+ASR pass would hand this up). aggregate_confidence() is the weakest
// link, so with a strong wake the transcript confidence sets the aggregate.
perception::Perception observation(const char* transcript, float conf) {
    perception::Perception p;
    p.wake   = perception::WakeWord{true, Confidence{0.95f}};
    p.speech = perception::Transcript{transcript, Confidence{conf}, true};
    return p;
}

// --- 1. Route-tag parsing against the real model's real output ---------------
// A clear, unambiguous device command. With ECHO's real system prompt and greedy
// (deterministic) decoding, the tiny model prefixes its reply with the routing
// tag (verified: "play some jazz music" -> "[route:media] [track:jazz]"). We
// assert the parser pulls "media" out of the model's ACTUAL output — not an
// idealized stub "[route:media]" — and that the tag was stripped from the spoken
// text, exercising the Phase-6-hardened trimming/lowercasing on real output.
void test_route_tag_extracted_from_real_output(ILlm& llm) {
    LlmReply reply = llm.generate("play some jazz music");
    std::printf("  route prompt   -> intent=\"%s\"  text=\"%s\"\n",
                reply.intent.c_str(), reply.text.c_str());
    CHECK(reply.intent == "media");                          // the app the tag routed to
    CHECK(reply.text.find("[route:") == std::string::npos);  // tag stripped from spoken reply
}

// --- 3. Basic sanity generation ----------------------------------------------
// A simple, unambiguous question. We assert the real model produced *some*
// non-empty text without crashing — never the exact wording. A plain factual
// answer is conversational, so we also expect no route tag, but we print that
// rather than hard-asserting the model's phrasing.
void test_sanity_generation_is_nonempty(ILlm& llm) {
    LlmReply reply = llm.generate("What is two plus two?");
    std::printf("  sanity prompt  -> intent=\"%s\"  text=\"%s\"\n",
                reply.intent.c_str(), reply.text.c_str());
    CHECK(!reply.text.empty());  // real generation returned something, no crash
}

// --- 2a. Confident path: the real LLM is consulted and routes end-to-end ------
// Above threshold, the gate PASSES and the real model is consulted through the
// full cognitive core. The same clear command routes end-to-end: a Normal
// response carrying intent "media", proving route-tag parsing in the real
// pipeline (not just the direct-adapter call above).
void test_confident_media_routes_through_core(ICognitiveCore& core) {
    const SafeModeConfig cfg;
    auto p = observation("play some jazz music", 0.90f);
    CHECK(p.aggregate_confidence().value >= cfg.min_confidence);  // this turn PASSES the gate

    auto r = core.respond(p).value();
    std::printf("  confident turn -> kind=%d intent=\"%s\" text=\"%s\"\n",
                static_cast<int>(r.kind), r.intent.c_str(), r.text.c_str());
    CHECK(r.kind == ResponseKind::Normal);  // real model produced text; gate passed
    CHECK(r.intent == "media");             // route-tag parsed in the real pipeline
    CHECK(!r.text.empty());
}

// --- 2b. Low-confidence path: the safe-mode gate fires, real LLM NOT consulted -
// Below threshold. The transcript is the SAME clear media command that, above
// threshold, routes to media — so if anything routed here, the gate leaked. It
// must not: ECHO's safe-mode gate keys on perception confidence and short-circuits
// BEFORE the LLM, so the real model is never consulted. The response is the
// known-good safe line, carrying the low confidence through — exactly as it does
// against the stub today, now demonstrated with a real LLM wired in.
void test_low_confidence_gate_fires_with_real_llm(ICognitiveCore& core) {
    const SafeModeConfig cfg;
    auto p = observation("play some jazz music", 0.30f);
    CHECK(p.aggregate_confidence().value < cfg.min_confidence);  // this turn is REFUSED

    auto r = core.respond(p).value();
    std::printf("  low-conf turn  -> kind=%d text=\"%s\" (LLM not consulted)\n",
                static_cast<int>(r.kind), r.text.c_str());
    CHECK(r.kind == ResponseKind::SafeMode);                    // the gate fired
    CHECK(r.text == cfg.safe_response);                         // known-good line, not a model reply
    CHECK(r.flag_caregiver);
    CHECK(std::abs(r.confidence.value - 0.30f) < 1e-6f);        // carried the low conf through
}

// --- 2c. The answer-side gate, against the REAL model (Phase 21) --------------
// The stub-build suite (tests/answer_gate_test.cpp) proves this with a scripted LLM.
// This proves the part a script cannot: that a REAL model, handed a question about
// the wearer's own life with no store attached, still never reaches the wearer.
//
// This is the turn that was broken for twenty phases. The transcript is clean, so
// the 0.72 input gate passes; before Phase 21 the model answered and whatever it
// invented was spoken as ResponseKind::Normal with flag_caregiver false. A small
// instruct model will happily produce "Yes, you took them at nine" — fluent,
// well-formed, and completely made up.
void test_autobiographical_question_abstains_with_real_llm(ICognitiveCore& core) {
    const SafeModeConfig cfg;
    auto p = observation("did i take my tablets today", 0.90f);
    CHECK(p.aggregate_confidence().value >= cfg.min_confidence);  // input gate PASSES

    auto r = core.respond(p).value();
    std::printf("  autobiographical turn -> kind=%d text=\"%s\" (no store: abstained)\n",
                static_cast<int>(r.kind), r.text.c_str());
    CHECK(r.kind == ResponseKind::Unverified);          // not Normal, not SafeMode
    CHECK(r.text == cfg.unverified_response);           // known-good line, not a model reply
    CHECK(!r.flag_caregiver);                           // an honest "I don't know" is not a fault
}

// --- 4. The measured answer-confidence distribution (Phase 21) ---------------
// A MEASUREMENT, not a test. Phase 21 derives an answer-side confidence from the
// real logits (llm.cpp: softmax over the sampled token, meaned across the
// generation) and gates on it at SafeModeConfig::min_answer_confidence. That
// threshold must be set from what a real small model actually does, not from taste,
// so this prints the observed value across prompts of deliberately different kinds:
//
//   * ordinary answerable questions      — should score high
//   * an unanswerable autobiographical   — the case Phase 21 exists for
//   * nonsense / adversarial input       — should score lower IF the signal works
//
// Read the output honestly. The expected finding, given Phase 12's observation that
// a real model produces confident text for EVERY prompt, is that these values sit in
// a NARROW band near the top and separate the categories poorly. If so, say exactly
// that in STATE.md: it means the fluency floor is doing very little and the grounding
// rule (which needs no probability at all) is carrying the whole guarantee. That is a
// legitimate and useful phase outcome — it is the reason the floor defaults low and
// is documented as a degeneracy catch rather than a truth test.
//
// Deliberately asserts only the INVARIANTS (a score was produced, and it is a real
// probability in [0,1]). Asserting a separation the model may not provide would be
// tuning the test to the conclusion we wanted.
void test_measure_answer_confidence_distribution(ILlm& llm) {
    struct Probe { const char* label; const char* prompt; };
    static const Probe kProbes[] = {
        {"answerable-factual  ", "What is two plus two?"},
        {"answerable-simple   ", "What colour is the sky on a clear day?"},
        {"device-command      ", "play some jazz music"},
        {"autobiographical    ", "Did I take my tablets this morning?"},
        {"unanswerable-personal", "Who visited me yesterday afternoon?"},
        {"nonsense            ", "Colourless green ideas sleep furiously wxqz?"},
    };

    std::printf("\n  --- Phase 21: measured answer confidence (mean token probability) ---\n");
    std::printf("  %-22s %-10s  %s\n", "prompt kind", "confidence", "reply (truncated)");
    float lo = 1.0f, hi = 0.0f;
    for (const auto& p : kProbes) {
        LlmReply r = llm.generate(p.prompt);
        std::string shown = r.text.substr(0, 58);
        std::printf("  %-22s %-10.4f  %s\n", p.label,
                    r.scored ? static_cast<double>(r.confidence.value) : -1.0, shown.c_str());

        // Invariants only: a real backend must score, and a probability is in [0,1].
        CHECK(r.scored);
        CHECK(r.confidence.value >= 0.0f && r.confidence.value <= 1.0f);
        if (r.scored && !r.text.empty()) {
            lo = std::min(lo, r.confidence.value);
            hi = std::max(hi, r.confidence.value);
        }
    }
    std::printf("  observed range: %.4f .. %.4f  (spread %.4f); floor is %.2f\n",
                static_cast<double>(lo), static_cast<double>(hi),
                static_cast<double>(hi - lo),
                static_cast<double>(SafeModeConfig{}.min_answer_confidence));
    std::printf("  NOTE: a narrow spread here means the fluency floor separates good from\n"
                "        bad answers WEAKLY, and the grounding rule is what protects the\n"
                "        wearer. Record the measured numbers in docs/STATE.md as observed.\n\n");
}

}  // namespace

int main() {
    const std::string model = config::llama_model();
    if (!std::filesystem::exists(model)) {
        // Built with ECHO_WITH_LLAMA=ON but the model isn't present (a local build
        // without the fetch). Skip loudly rather than fail — CI always provides it.
        std::printf("[real-llm] SKIP: LLM model not found at \"%s\". Fetch it "
                    "(scripts/fetch_real_engine_assets.sh) or set $ECHO_LLAMA_MODEL "
                    "(see MANIFEST.md).\n",
                    model.c_str());
        return echo::test::report("real-llm");
    }

    // --- Direct adapter: route-tag parsing + sanity generation on real output --
    auto llm = make_llm();
    CHECK(llm->initialize() == Status::Ok);  // model present -> it MUST load
    test_route_tag_extracted_from_real_output(*llm);
    test_sanity_generation_is_nonempty(*llm);
    test_measure_answer_confidence_distribution(*llm);   // Phase 21 measurement
    llm->shutdown();

    // --- Full cognitive core: the safe-mode gate against the real LLM ----------
    auto core = make_cognitive_core();       // builds + initializes its own real LLM
    CHECK(core->initialize() == Status::Ok);
    test_confident_media_routes_through_core(*core);
    test_low_confidence_gate_fires_with_real_llm(*core);
    test_autobiographical_question_abstains_with_real_llm(*core);   // Phase 21
    core->shutdown();

    return echo::test::report("real-llm");
}
