#include "echo/perception/perception_engine.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include <algorithm>
#include <memory>

namespace echo::perception {

Confidence Perception::aggregate_confidence() const noexcept {
    // Gate on the weakest contributing modality: a confident face with a shaky
    // transcript should still defer to safe mode.
    float weakest = 1.0f;
    bool  any = false;
    auto consider = [&](Confidence c) { weakest = std::min(weakest, c.value); any = true; };

    if (wake)   consider(wake->confidence);
    if (speech) consider(speech->confidence);
    for (const auto& f : faces)   consider(f.confidence);
    for (const auto& o : objects) consider(o.confidence);

    return any ? Confidence{weakest} : Confidence{0.0f};
}

namespace {

class StubPerceptionEngine final : public IPerceptionEngine {
public:
    Status initialize() override {
        // TODO(perception): mmap quantized CNN + wake-word + ASR models onto the
        // NPU and run a warm-up inference so the first real frame isn't cold.
        log_info("perception", "engine initialized (stub models)");
        return Status::Ok;
    }

    Result<Perception> process(const SensorFrame& frame) override {
        StageTimer timer(Stage::Perception);

        Perception p;
        // The scaffold returns an empty, low-confidence observation. Real models
        // would populate wake/speech/faces/objects here based on `frame.modality`.
        switch (frame.modality) {
            case Modality::Microphone:
                p.wake = WakeWord{/*detected=*/false, Confidence{0.0f}};
                break;
            case Modality::Camera:
            case Modality::Eeg:
                break;
        }
        (void)timer;
        return Result<Perception>::ok(std::move(p));
    }

    void shutdown() override {
        log_info("perception", "engine shut down");
    }
};

}  // namespace

std::unique_ptr<IPerceptionEngine> make_perception_engine() {
    return std::make_unique<StubPerceptionEngine>();
}

}  // namespace echo::perception
