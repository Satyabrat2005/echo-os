// ECHO OS — power & thermal POLICY (Phase 18).
//
// Pure, deterministic decision logic: given a battery reading and a thermal reading,
// decide how hard the perception/inference pipeline is allowed to work. No I/O, no
// hidden state — every function here is a total function of its inputs, which is exactly
// what makes the whole policy testable against scripted readings with no hardware.
//
// Two independent levers, kept deliberately separate (brief items 2 & 3):
//   * BATTERY drives VISION DUTY-CYCLING — camera-based face detection is the expensive,
//     continuous cost, so as charge drops we sample it LESS OFTEN (never guess a result
//     to hide the lower rate — a slower cadence means slower recognition, not a fabricated
//     one). Below a critical floor vision is OFF entirely and the device is voice-only.
//   * THERMAL drives an INFERENCE THROTTLE — when the temple runs warm/hot we reduce
//     inference load and, crucially, ALLOW LONGER LATENCY before the Phase 17 hang
//     watchdog fires, so a legitimately-slower throttled turn is not mistaken for a hang.
//
// The two combine: thermal pressure can force vision to at least a reduced cadence even on
// a full battery, but only a critical BATTERY turns vision fully off — keeping the two axes
// distinct and independently assertable.
#pragma once

#include "echo/power/power_source.hpp"

#include <chrono>
#include <cstdint>

namespace echo::power {

// How often the camera path is sampled. A lower rate = slower/less-frequent recognition,
// NEVER a fabricated one (the runtime simply drops the un-sampled camera frames).
enum class VisionDuty : std::uint8_t {
    Full,     // sample every camera frame
    Reduced,  // sample 1 in N camera frames (see vision_sample_interval)
    Off,      // voice-only: camera frames are not processed at all
};

// Inference throttle level, driven by the thermal state.
enum class Throttle : std::uint8_t {
    None,  // full capability
    Warm,  // reduced context / relaxed latency budget
    Hot,   // minimal capability / most relaxed latency budget
};

const char* to_string(VisionDuty d) noexcept;
const char* to_string(Throttle t) noexcept;

// Battery thresholds (percent, state-of-charge), matching the brief's bands:
//   full-rate ABOVE 40%, reduced-rate 15–40%, vision-off (voice-only) BELOW 15%.
// A separate, lower "critical" floor marks a battery so low the device may be about to
// shut down — the trigger for the low-battery reminder-priority pass (see below).
inline constexpr std::uint8_t kFullRateAbovePercent = 40;
inline constexpr std::uint8_t kVisionOffBelowPercent = 15;
inline constexpr std::uint8_t kCriticalBatteryPercent = 5;

// Camera sampling cadence for the Reduced band: process 1 camera frame in every N.
inline constexpr int kReducedVisionSampleInterval = 4;

// The full decision the runtime applies each tick. Everything here is derived purely
// from the two readings by decide() below.
struct PowerDecision {
    VisionDuty vision      = VisionDuty::Full;
    Throttle   inference   = Throttle::None;
    bool       battery_low = false;  // in the reduced/off band (<=40% & discharging) — for a caregiver heads-up
    bool       battery_critical = false;  // <=critical floor & discharging — shutdown may be imminent
    bool       thermal_elevated = false;  // warm or hot — for a caregiver heads-up
};

// --- pure policy functions (each a total function of its input) --------------

// Vision cadence from battery alone.
VisionDuty vision_duty_for_battery(const BatteryReading& b) noexcept;

// The at-least-this-restrictive cadence thermal pressure imposes (Nominal→Full,
// Warm/Hot→Reduced). Thermal never forces Off — only a critical battery does.
VisionDuty vision_floor_for_thermal(ThermalState s) noexcept;

// Inference throttle from the thermal state.
Throttle throttle_for_thermal(ThermalState s) noexcept;

// How many camera frames pass per processed one, for a given cadence. Full→1, Reduced→N,
// Off→0 (the caller must not process any). Deterministic; the runtime counts ticks against
// this rather than fabricating skipped frames.
int vision_sample_interval(VisionDuty d) noexcept;

// Latency-budget SCALE the throttle applies to the Phase 17 hang watchdog: a throttled
// (slower) inference is legitimately allowed to take longer before it is treated as hung.
// None→1.0, Warm→1.5, Hot→2.0. Never below 1.0 (throttling only ever RELAXES the bound).
double latency_budget_scale(Throttle t) noexcept;

// Combine both readings into the single decision the runtime applies.
PowerDecision decide(const BatteryReading& battery, const ThermalReading& thermal) noexcept;

// Apply latency_budget_scale to a base budget, saturating so no rounding can ever shorten
// it below the base (a throttle must not make the watchdog MORE trigger-happy).
std::chrono::milliseconds scaled_budget(std::chrono::milliseconds base, Throttle t) noexcept;

}  // namespace echo::power
