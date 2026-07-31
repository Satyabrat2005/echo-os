#include "echo/power/power_policy.hpp"

#include <algorithm>
#include <cmath>

namespace echo::power {

const char* to_string(VisionDuty d) noexcept {
    switch (d) {
        case VisionDuty::Full:    return "full";
        case VisionDuty::Reduced: return "reduced";
        case VisionDuty::Off:     return "off";
    }
    return "unknown";
}

const char* to_string(Throttle t) noexcept {
    switch (t) {
        case Throttle::None: return "none";
        case Throttle::Warm: return "warm";
        case Throttle::Hot:  return "hot";
    }
    return "unknown";
}

VisionDuty vision_duty_for_battery(const BatteryReading& b) noexcept {
    // On external power there is no need to conserve — run full-rate regardless of charge.
    if (b.charging) return VisionDuty::Full;
    if (b.percent >= kFullRateAbovePercent) return VisionDuty::Full;      // > 40%: full
    if (b.percent >= kVisionOffBelowPercent) return VisionDuty::Reduced;  // 15–40%: reduced
    return VisionDuty::Off;                                               // < 15%: voice-only
}

VisionDuty vision_floor_for_thermal(ThermalState s) noexcept {
    // Thermal pressure forces AT LEAST a reduced cadence, but never fully off — turning
    // vision completely off is reserved for a critical battery, keeping the two axes distinct.
    return (s == ThermalState::Nominal) ? VisionDuty::Full : VisionDuty::Reduced;
}

Throttle throttle_for_thermal(ThermalState s) noexcept {
    switch (s) {
        case ThermalState::Nominal: return Throttle::None;
        case ThermalState::Warm:    return Throttle::Warm;
        case ThermalState::Hot:     return Throttle::Hot;
    }
    return Throttle::None;
}

int vision_sample_interval(VisionDuty d) noexcept {
    switch (d) {
        case VisionDuty::Full:    return 1;
        case VisionDuty::Reduced: return kReducedVisionSampleInterval;
        case VisionDuty::Off:     return 0;  // caller must process no camera frames
    }
    return 1;
}

double latency_budget_scale(Throttle t) noexcept {
    switch (t) {
        case Throttle::None: return 1.0;
        case Throttle::Warm: return 1.5;
        case Throttle::Hot:  return 2.0;
    }
    return 1.0;
}

PowerDecision decide(const BatteryReading& battery, const ThermalReading& thermal) noexcept {
    PowerDecision d;

    // Vision cadence: the MORE restrictive of the battery-driven cadence and the thermal
    // floor. VisionDuty is ordered Full < Reduced < Off, so std::max picks the stricter one.
    const VisionDuty by_battery = vision_duty_for_battery(battery);
    const VisionDuty by_thermal = vision_floor_for_thermal(thermal.state);
    d.vision = std::max(by_battery, by_thermal);

    d.inference = throttle_for_thermal(thermal.state);

    // Battery flags are only meaningful while DISCHARGING — on a charger neither a low nor a
    // critical reading should raise a "conserve/shutdown-imminent" signal.
    d.battery_low      = !battery.charging && battery.percent < kFullRateAbovePercent;
    d.battery_critical = !battery.charging && battery.percent <= kCriticalBatteryPercent;
    d.thermal_elevated = thermal.state != ThermalState::Nominal;
    return d;
}

std::chrono::milliseconds scaled_budget(std::chrono::milliseconds base, Throttle t) noexcept {
    const double scaled = static_cast<double>(base.count()) * latency_budget_scale(t);
    // Round to nearest (llround, not a +0.5 cast), then clamp so the result is never SHORTER
    // than the base — a throttle may only ever relax the hang bound, never tighten it.
    auto ms = static_cast<std::chrono::milliseconds::rep>(std::llround(scaled));
    ms = std::max(ms, base.count());
    return std::chrono::milliseconds(ms);
}

}  // namespace echo::power
