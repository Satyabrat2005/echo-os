// ECHO OS — latency budget tracking for the core perception-to-response loop.
//
// The whole system is designed around one number: 120 ms from sensor input to
// the first sample of voice output. This header encodes the per-stage budget as
// data and provides a lightweight scope timer so any stage can assert it stayed
// within its slice. In a debug build, overruns are logged; in production they
// feed power-mgmt's throttling decisions.
#pragma once

#include "echo/types.hpp"

#include <array>

namespace echo {

// The stages of the core loop, in order. Keep in sync with kBudget below.
enum class Stage : std::uint8_t {
    SensorCapture = 0,  // frontend latch + lock-free handoff
    Perception,         // wake-word / ASR / face / object
    Cognitive,          // LLM orchestration + safe-mode gate
    VoiceOutput,        // TTS synth + first audio sample
    Overhead,           // cross-stage scheduling slack
    Count,
};

const char* to_string(Stage s) noexcept;

// Per-stage budget in milliseconds. Sums to the 120 ms end-to-end target.
// This is the single source of truth referenced by the README's budget table.
inline constexpr std::array<double, static_cast<std::size_t>(Stage::Count)> kBudgetMs = {
    5.0,   // SensorCapture
    45.0,  // Perception
    50.0,  // Cognitive
    18.0,  // VoiceOutput
    2.0,   // Overhead
};

inline constexpr double kEndToEndBudgetMs = 120.0;

inline constexpr double budget_ms(Stage s) noexcept {
    return kBudgetMs[static_cast<std::size_t>(s)];
}

// RAII timer that measures how long a stage took and reports whether it blew its
// budget. Construct at the top of a stage; inspect elapsed()/over_budget() before
// it leaves scope, or let the destructor log an overrun.
class StageTimer {
public:
    explicit StageTimer(Stage stage) noexcept;
    ~StageTimer();

    StageTimer(const StageTimer&)            = delete;
    StageTimer& operator=(const StageTimer&) = delete;

    double elapsed_ms() const noexcept;
    bool   over_budget() const noexcept { return elapsed_ms() > budget_ms(stage_); }

private:
    Stage     stage_;
    TimePoint start_;
};

}  // namespace echo
