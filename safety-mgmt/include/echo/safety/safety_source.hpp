// ECHO OS — wandering & distress SOURCES (Phase 23).
//
// The read-only sensing boundary the wandering/distress POLICY reasons over. Mirrors
// power_source.hpp's discipline exactly: a narrow interface, a REAL backend hook, and a
// fully deterministic FAKE for tests — so the whole policy is verifiable in the
// dependency-free stub build with no real sensor present.
//
// These interfaces only READ. Deciding what a standing "away from home" or "elevated
// arousal" condition MEANS (suspected vs. confirmed, when to alert) is safety_policy.hpp;
// applying it lives in the runtime. Keeping sensing and policy apart is what lets the
// policy be tested against scripted readings with no hardware in the loop.
//
// Honest hardware note, stated once here because it is the crux of Phase 23's scope: there
// is no real geofence/GPS chip and no real biometric/prosody classifier on a dev laptop, and
// certainly none on the not-yet-existing glasses. So make_location_source() /
// make_arousal_source() return a DOCUMENTED STUB (see safety_source.cpp) that always reports
// the safe state — there is nothing plausible to simulate changing, unlike power's battery
// drain, so the stub does not pretend otherwise. The policy this feeds is real and tested;
// the sensor behind it is not. STATE.md logs this as a permanent-until-hardware gap, not a
// temporary one, same as Phase 18's battery/thermal gap.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

namespace echo::safety {

// Coarse location/geofence state, not raw GPS coordinates — the policy needs a zone, not a
// lat/long, and a state-only backend (the realistic glasses case) loses nothing (mirrors
// power_source.hpp's ThermalState choice).
enum class ZoneState : std::uint8_t {
    Home,      // within the known/expected area
    Boundary,  // at the edge of the known area
    Away,      // outside the known area entirely
};

// Coarse arousal/distress state — what a coarse EEG/prosody/heart-rate classifier would
// report, if one existed. Not a numeric score: the policy picks a risk band, not a setpoint.
enum class ArousalState : std::uint8_t {
    Calm,
    Elevated,
    High,
};

const char* to_string(ZoneState s) noexcept;
const char* to_string(ArousalState s) noexcept;

// A location reading: the coarse zone state, plus how long it has held. Dwell is carried by
// the reading (not reconstructed by counting ticks in Runtime) so safety_policy.hpp's
// decide() stays a pure function of its inputs, exactly like power_policy.hpp.
struct LocationReading {
    ZoneState             zone  = ZoneState::Home;
    std::chrono::seconds  dwell{0};
};

// An arousal reading: same shape as LocationReading.
struct ArousalReading {
    ArousalState           state = ArousalState::Calm;
    std::chrono::seconds   dwell{0};
};

// Read-only location/geofence source. Cheap to call every tick.
class ILocationSource {
public:
    virtual ~ILocationSource() = default;
    virtual Status initialize() = 0;
    // A failed read returns fail(...) rather than a fabricated zone — the policy fails to
    // its last-known-good decision, never to a guessed location.
    virtual Result<LocationReading> read() = 0;
    virtual void shutdown() = 0;
};

// Read-only arousal/distress source.
class IArousalSource {
public:
    virtual ~IArousalSource() = default;
    virtual Status initialize() = 0;
    virtual Result<ArousalReading> read() = 0;
    virtual void shutdown() = 0;
};

// Real-backend hooks. On this dev laptop / the future embedded target there is no real
// sensor to read, so these are DOCUMENTED STUBS that always report the safe state (see
// safety_source.cpp). Tests never use these — they inject the deterministic fakes from
// fake_safety_source.hpp through the runtime's DI seam.
std::unique_ptr<ILocationSource> make_location_source();
std::unique_ptr<IArousalSource>  make_arousal_source();

}  // namespace echo::safety
