#include "echo/safety/safety_source.hpp"
#include "echo/log.hpp"

namespace echo::safety {

const char* to_string(ZoneState s) noexcept {
    switch (s) {
        case ZoneState::Home:     return "home";
        case ZoneState::Boundary: return "boundary";
        case ZoneState::Away:     return "away";
    }
    return "unknown";
}

const char* to_string(ArousalState s) noexcept {
    switch (s) {
        case ArousalState::Calm:     return "calm";
        case ArousalState::Elevated: return "elevated";
        case ArousalState::High:     return "high";
    }
    return "unknown";
}

namespace {

// --- REAL backend hooks: DOCUMENTED STUBS ------------------------------------
//
// There is no real geofence/GPS chip or biometric/prosody sensor to read here — not on this
// dev laptop, and not on the not-yet-existing glasses. Unlike power's battery drain, there is
// no plausible varying value to simulate at all, so rather than fabricate a changing reading
// (which would be a lie the policy above would faithfully act on), the real hook reports a
// fixed, always-safe state and says so once at init. This is where a real geofence/GPS read,
// or a real EEG/prosody/heart-rate classifier output, would go; the DATA is a constant until
// real hardware/algorithms exist. STATE.md logs this as a permanent-until-hardware gap, same
// as Phase 18's battery/thermal gap. Tests never touch this path — they inject the
// deterministic fakes from fake_safety_source.hpp.
class StubLocationSource final : public ILocationSource {
public:
    Status initialize() override {
        log_info("safety",
                 "location source: DOCUMENTED STUB (no real geofence/GPS on this target; "
                 "always reports Home — see safety_source.cpp)");
        return Status::Ok;
    }
    Result<LocationReading> read() override {
        LocationReading r;
        r.zone  = ZoneState::Home;
        r.dwell = std::chrono::seconds{0};
        return Result<LocationReading>::ok(r);
    }
    void shutdown() override {}
};

class StubArousalSource final : public IArousalSource {
public:
    Status initialize() override {
        log_info("safety",
                 "arousal source: DOCUMENTED STUB (no real biometric/prosody sensor on this "
                 "target; always reports Calm — see safety_source.cpp)");
        return Status::Ok;
    }
    Result<ArousalReading> read() override {
        ArousalReading r;
        r.state = ArousalState::Calm;
        r.dwell = std::chrono::seconds{0};
        return Result<ArousalReading>::ok(r);
    }
    void shutdown() override {}
};

}  // namespace

std::unique_ptr<ILocationSource> make_location_source() { return std::make_unique<StubLocationSource>(); }
std::unique_ptr<IArousalSource>  make_arousal_source()  { return std::make_unique<StubArousalSource>(); }

}  // namespace echo::safety
