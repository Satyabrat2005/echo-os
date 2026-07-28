// ECHO OS — common types shared across every runtime module.
//
// These types form the vocabulary of the core perception-to-response loop.
// They are intentionally lightweight and copy-cheap; anything that carries a
// real payload (camera frames, audio, EEG windows) is passed as a non-owning
// view so we never copy raw sensor data through the pipeline (privacy + latency).
#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <string_view>

namespace echo {

// Monotonic clock used for every latency measurement in the system.
// steady_clock never jumps (unlike system_clock), which is what we need for
// enforcing the sub-120ms budget.
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = std::chrono::nanoseconds;

inline TimePoint now() noexcept { return Clock::now(); }

// Which sensor a frame originated from.
enum class Modality : std::uint8_t {
    Camera,      // OV2640 DVP stream
    Microphone,  // PDM/I2S mic
    Eeg,         // frontal EEG frontend
};

const char* to_string(Modality m) noexcept;

// A non-owning view over a captured sensor payload.
//
// The sensor-pipeline owns the backing memory in a lock-free ring buffer; every
// downstream stage receives only this view. No stage copies or persists the
// bytes — this is the mechanism behind "no raw sensor data leaves the device."
struct SensorFrame {
    Modality             modality   = Modality::Camera;
    std::uint64_t        sequence   = 0;       // monotonic per-modality counter
    TimePoint            captured_at{};        // when the frontend latched it
    const std::uint8_t*  data       = nullptr; // borrowed, never owned here
    std::size_t          size       = 0;       // bytes at `data`
    std::uint32_t        width      = 0;        // camera only, else 0
    std::uint32_t        height     = 0;        // camera only, else 0
    std::uint32_t        sample_rate= 0;        // audio/eeg only, else 0

    bool valid() const noexcept { return data != nullptr && size > 0; }
};

// Model confidence, normalized to [0, 1]. The cognitive core compares this
// against a per-task threshold to decide between a real response and safe mode.
struct Confidence {
    float value = 0.0f;

    constexpr Confidence() = default;
    constexpr explicit Confidence(float v) : value(v) {}

    bool below(float threshold) const noexcept { return value < threshold; }
    bool at_least(float threshold) const noexcept { return value >= threshold; }
};

// The runtime's coarse operating state. Power-mgmt and the boot sequence both
// read and drive this.
enum class RuntimeState : std::uint8_t {
    Booting,
    Ready,      // idle, sensors warm, waiting on wake-word
    Active,     // running the perception-to-response loop
    SafeMode,   // low-confidence fallback engaged
    LowPower,   // throttled to preserve battery
    Shutdown,
};

const char* to_string(RuntimeState s) noexcept;

}  // namespace echo
