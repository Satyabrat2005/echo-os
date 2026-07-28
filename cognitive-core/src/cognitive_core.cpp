#include "echo/cognitive/cognitive_core.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

namespace echo::cognitive {
namespace {

class StubCognitiveCore final : public ICognitiveCore {
public:
    explicit StubCognitiveCore(SafeModeConfig config) : config_(std::move(config)) {}

    Status initialize() override {
        // TODO(cognitive): load quantized LLM via ONNX Runtime / TensorRT, pin
        // KV-cache buffers, run a warm-up token so first response isn't cold.
        log_info("cognitive", "core initialized (stub LLM)");
        return Status::Ok;
    }

    Result<Response> respond(const perception::Perception& observation) override {
        StageTimer timer(Stage::Cognitive);
        (void)timer;

        const Confidence agg = observation.aggregate_confidence();

        // The safe-mode gate. This is the whole point of the module: if we are
        // not confident, we do not ask the LLM to guess.
        if (agg.below(config_.min_confidence)) {
            log_warn("cognitive", "confidence below threshold -> safe mode");
            Response r;
            r.kind           = ResponseKind::SafeMode;
            r.text           = config_.safe_response;
            r.confidence     = agg;
            r.flag_caregiver = true;
            return Result<Response>::ok(std::move(r));
        }

        // TODO(cognitive): run the LLM with the perception context and produce a
        // grounded, empathetic answer. Stubbed as an acknowledgement for now.
        Response r;
        r.kind           = ResponseKind::Normal;
        r.text           = "Okay.";
        r.confidence     = agg;
        r.flag_caregiver = false;
        return Result<Response>::ok(std::move(r));
    }

    void shutdown() override { log_info("cognitive", "core shut down"); }

private:
    SafeModeConfig config_;
};

}  // namespace

std::unique_ptr<ICognitiveCore> make_cognitive_core(SafeModeConfig config) {
    return std::make_unique<StubCognitiveCore>(std::move(config));
}

}  // namespace echo::cognitive
