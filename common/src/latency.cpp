#include "echo/latency.hpp"
#include "echo/log.hpp"

#include <chrono>
#include <cstdio>

namespace echo {

const char* to_string(Stage s) noexcept {
    switch (s) {
        case Stage::SensorCapture: return "sensor-capture";
        case Stage::Perception:    return "perception";
        case Stage::Cognitive:     return "cognitive";
        case Stage::VoiceOutput:   return "voice-output";
        case Stage::Overhead:      return "overhead";
        case Stage::Count:         return "count";
    }
    return "unknown";
}

StageTimer::StageTimer(Stage stage) noexcept : stage_(stage), start_(now()) {}

double StageTimer::elapsed_ms() const noexcept {
    const auto delta = now() - start_;
    return std::chrono::duration<double, std::milli>(delta).count();
}

StageTimer::~StageTimer() {
    const double ms = elapsed_ms();
    if (ms > budget_ms(stage_)) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s overran budget: %.2fms > %.2fms",
                      to_string(stage_), ms, budget_ms(stage_));
        log_warn("latency", buf);
    }
}

}  // namespace echo
