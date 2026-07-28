#include "echo/cognitive/cognitive_core.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include "llm.hpp"

#include <memory>
#include <utility>

namespace echo::cognitive {
namespace {

// The cognitive core: the confidence-threshold safe-mode gate, unchanged from the
// scaffold, now with the real quantized LLM wired in *behind* it. The gate is the
// whole point of the module (principle #5): a low-confidence observation never
// reaches the LLM, and even a confident one falls back to safe mode if the LLM
// fails to produce text. We never let the model guess.
class CognitiveCore final : public ICognitiveCore {
public:
    explicit CognitiveCore(SafeModeConfig config)
        : config_(std::move(config)), llm_(make_llm()) {}

    Status initialize() override {
        // Warm the LLM. A load failure is non-fatal: the core still runs, it just
        // stays in safe mode (better than refusing to boot on a vulnerable device).
        if (llm_->initialize() != Status::Ok)
            log_warn("cognitive", "LLM unavailable; core will use safe mode only");
        log_info("cognitive", "core initialized");
        return Status::Ok;
    }

    Result<Response> respond(const perception::Perception& observation) override {
        StageTimer timer(Stage::Cognitive);
        (void)timer;

        const Confidence agg = observation.aggregate_confidence();

        // --- The safe-mode gate (UNCHANGED) ------------------------------------
        // If we are not confident, we do not ask the LLM to guess.
        if (agg.below(config_.min_confidence)) {
            log_warn("cognitive", "confidence below threshold -> safe mode");
            return Result<Response>::ok(safe_mode(agg));
        }

        // --- Confident path: run the real LLM ----------------------------------
        // We only reach here above threshold. There must be a transcript to reason
        // over; without one there's nothing to answer, so stay calm and safe.
        if (!observation.speech || observation.speech->text.empty())
            return Result<Response>::ok(safe_mode(agg));

        LlmReply reply = llm_->generate(observation.speech->text);
        if (reply.text.empty()) {
            // Generation failed. Fail safe, not smart.
            log_warn("cognitive", "LLM produced no text -> safe mode");
            return Result<Response>::ok(safe_mode(agg));
        }

        Response r;
        r.kind           = ResponseKind::Normal;
        r.text           = std::move(reply.text);
        r.intent         = std::move(reply.intent);
        r.confidence     = agg;
        r.flag_caregiver = false;
        return Result<Response>::ok(std::move(r));
    }

    void shutdown() override {
        llm_->shutdown();
        log_info("cognitive", "core shut down");
    }

private:
    Response safe_mode(Confidence agg) const {
        Response r;
        r.kind           = ResponseKind::SafeMode;
        r.text           = config_.safe_response;
        r.confidence     = agg;
        r.flag_caregiver = true;
        return r;
    }

    SafeModeConfig        config_;
    std::unique_ptr<ILlm> llm_;
};

}  // namespace

std::unique_ptr<ICognitiveCore> make_cognitive_core(SafeModeConfig config) {
    return std::make_unique<CognitiveCore>(std::move(config));
}

}  // namespace echo::cognitive
