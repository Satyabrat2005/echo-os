// ECHO OS — wandering & distress POLICY (Phase 23).
//
// Pure, deterministic decision logic: given a location reading and an arousal reading,
// decide whether a wandering or distress condition is suspected or confirmed. No I/O, no
// hidden state — every function here is a total function of its inputs, exactly like
// power_policy.hpp, which is what makes the whole policy testable against scripted
// readings with no hardware.
//
// Two independent axes, kept deliberately separate (mirrors power's battery/thermal split):
//   * LOCATION drives WANDERING risk — a brief dip to the boundary (stepping onto the
//     porch) must not alarm; a SUSTAINED absence should.
//   * AROUSAL drives DISTRESS risk — same grace-period shape.
// The grace periods below are placeholder, documented-as-provisional thresholds pending
// real hardware/clinical input — same honesty as power's 40/15/5% battery bands.
//
// A CONFIRMED risk on either axis is the only thing that raises a caregiver alert
// (WanderingRisk::Confirmed -> AlertKind::Wandering, DistressRisk::Confirmed ->
// AlertKind::Distress, both wired in boot::Runtime). Suspected is an internal staging
// state, not yet alert-worthy — it exists so a reading can be inspected/tested one step
// before it would page a caregiver.
#pragma once

#include "echo/safety/safety_source.hpp"

#include <chrono>
#include <cstdint>

namespace echo::safety {

enum class WanderingRisk : std::uint8_t { None, Suspected, Confirmed };
enum class DistressRisk  : std::uint8_t { None, Suspected, Confirmed };

const char* to_string(WanderingRisk r) noexcept;
const char* to_string(DistressRisk r) noexcept;

// Grace periods before a state escalates. Placeholder, documented as such: real values need
// real hardware/clinical input, same honesty as power's battery/thermal thresholds.
inline constexpr std::chrono::seconds kBoundaryGrace{300};  // 5 min at the boundary -> Suspected
inline constexpr std::chrono::seconds kAwayGrace{120};      // 2 min fully away -> Confirmed
inline constexpr std::chrono::seconds kElevatedGrace{30};   // 30s elevated arousal -> Suspected
inline constexpr std::chrono::seconds kHighGrace{10};       // 10s high arousal -> Confirmed

struct SafetyDecision {
    WanderingRisk wandering = WanderingRisk::None;
    DistressRisk  distress  = DistressRisk::None;
    bool wandering_alert = false;  // wandering == Confirmed -> caregiver heads-up
    bool distress_alert  = false;  // distress == Confirmed -> caregiver heads-up
};

// --- pure policy functions (each a total function of its input) --------------

// Away is already Suspected the instant it's read (being outside the known area at all is
// concerning); Boundary only escalates to Suspected after kBoundaryGrace; Away only
// escalates to Confirmed after kAwayGrace. Boundary never reaches Confirmed on its own.
WanderingRisk wandering_risk_for_location(const LocationReading& r) noexcept;

// Same shape as wandering_risk_for_location, over arousal state/dwell.
DistressRisk distress_risk_for_arousal(const ArousalReading& r) noexcept;

// Combine both readings into the single decision the runtime applies. The two axes are
// independent — a wandering episode does not imply anything about distress, and vice versa.
SafetyDecision decide(const LocationReading& location, const ArousalReading& arousal) noexcept;

}  // namespace echo::safety
