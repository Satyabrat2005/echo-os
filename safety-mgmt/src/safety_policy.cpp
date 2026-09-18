#include "echo/safety/safety_policy.hpp"

namespace echo::safety {

const char* to_string(WanderingRisk r) noexcept {
    switch (r) {
        case WanderingRisk::None:      return "none";
        case WanderingRisk::Suspected: return "suspected";
        case WanderingRisk::Confirmed: return "confirmed";
    }
    return "unknown";
}

const char* to_string(DistressRisk r) noexcept {
    switch (r) {
        case DistressRisk::None:      return "none";
        case DistressRisk::Suspected: return "suspected";
        case DistressRisk::Confirmed: return "confirmed";
    }
    return "unknown";
}

WanderingRisk wandering_risk_for_location(const LocationReading& r) noexcept {
    switch (r.zone) {
        case ZoneState::Home:
            return WanderingRisk::None;
        case ZoneState::Boundary:
            return (r.dwell >= kBoundaryGrace) ? WanderingRisk::Suspected : WanderingRisk::None;
        case ZoneState::Away:
            return (r.dwell >= kAwayGrace) ? WanderingRisk::Confirmed : WanderingRisk::Suspected;
    }
    return WanderingRisk::None;
}

DistressRisk distress_risk_for_arousal(const ArousalReading& r) noexcept {
    switch (r.state) {
        case ArousalState::Calm:
            return DistressRisk::None;
        case ArousalState::Elevated:
            return (r.dwell >= kElevatedGrace) ? DistressRisk::Suspected : DistressRisk::None;
        case ArousalState::High:
            return (r.dwell >= kHighGrace) ? DistressRisk::Confirmed : DistressRisk::Suspected;
    }
    return DistressRisk::None;
}

SafetyDecision decide(const LocationReading& location, const ArousalReading& arousal) noexcept {
    SafetyDecision d;
    d.wandering = wandering_risk_for_location(location);
    d.distress  = distress_risk_for_arousal(arousal);
    d.wandering_alert = (d.wandering == WanderingRisk::Confirmed);
    d.distress_alert  = (d.distress  == DistressRisk::Confirmed);
    return d;
}

}  // namespace echo::safety
