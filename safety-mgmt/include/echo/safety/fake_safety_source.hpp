// ECHO OS — deterministic FAKE location/arousal sources (Phase 23).
//
// The test-side half of the dual-path discipline every engine has kept since Phase 3: a
// fully deterministic, scriptable fake with no hardware and no hidden state. Header-only,
// mirrors fake_power_source.hpp exactly, so the same DI seam through the runtime that
// power-mgmt uses works identically for safety-mgmt.
//
// Can also simulate a SENSOR FAULT (a read that throws), so the runtime's "sensing must
// never take the loop down, and must never fabricate a zone/arousal state" behaviour is
// exercised the same way Phase 18 injects power/thermal source faults.
#pragma once

#include "echo/safety/safety_source.hpp"

#include <atomic>
#include <stdexcept>

namespace echo::safety {

// Scriptable location source. Set the zone/dwell fields; read() returns them verbatim. Set
// `fault` to make read() throw, modelling a GPS/geofence receiver fault.
class FakeLocationSource final : public ILocationSource {
public:
    std::atomic<ZoneState>   zone{ZoneState::Home};
    std::atomic<long long>   dwell_seconds{0};
    std::atomic<bool>        fault{false};  // when true, read() throws (receiver fault)

    Status initialize() override { return Status::Ok; }
    Result<LocationReading> read() override {
        if (fault.load()) throw std::runtime_error("location source read fault");
        LocationReading r;
        r.zone  = zone.load();
        r.dwell = std::chrono::seconds(dwell_seconds.load());
        return Result<LocationReading>::ok(r);
    }
    void shutdown() override {}

    // Convenience for tests reading like prose.
    void set(ZoneState z, std::chrono::seconds d) { zone = z; dwell_seconds = d.count(); }
};

// Scriptable arousal source. Same shape as FakeLocationSource. Set `fault` to make read()
// throw, modelling a biometric/prosody sensor fault.
class FakeArousalSource final : public IArousalSource {
public:
    std::atomic<ArousalState> state{ArousalState::Calm};
    std::atomic<long long>    dwell_seconds{0};
    std::atomic<bool>         fault{false};

    Status initialize() override { return Status::Ok; }
    Result<ArousalReading> read() override {
        if (fault.load()) throw std::runtime_error("arousal source read fault");
        ArousalReading r;
        r.state = state.load();
        r.dwell = std::chrono::seconds(dwell_seconds.load());
        return Result<ArousalReading>::ok(r);
    }
    void shutdown() override {}

    void set(ArousalState s, std::chrono::seconds d) { state = s; dwell_seconds = d.count(); }
};

}  // namespace echo::safety
