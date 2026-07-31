// ECHO OS — deterministic FAKE power/thermal sources (Phase 18).
//
// The test-side half of the dual-path discipline every engine has kept since Phase 3:
// a fully deterministic, scriptable fake with no hardware and no hidden state. Header-only
// so both the power-mgmt test and any future host can compose the runtime with scripted
// readings and step it through the battery/thermal threshold transitions.
//
// It can also simulate a SENSOR FAULT (a read that throws), so the runtime's "power sensing
// must never take the loop down, and must never fabricate a charge level" behaviour is
// exercised the same way Phase 17 injects engine faults.
#pragma once

#include "echo/power/power_source.hpp"

#include <atomic>
#include <stdexcept>

namespace echo::power {

// Scriptable battery source. Set the charge/charging fields; read() returns them verbatim.
// Set `fault` to make read() throw, modelling a fuel-gauge I2C fault.
class FakePowerSource final : public IPowerSource {
public:
    std::atomic<std::uint8_t> percent{100};
    std::atomic<bool>         charging{false};
    std::atomic<bool>         fault{false};   // when true, read() throws (I2C/gauge fault)

    Status initialize() override { return Status::Ok; }
    Result<BatteryReading> read() override {
        if (fault.load()) throw std::runtime_error("battery gauge read fault");
        BatteryReading b;
        b.percent  = percent.load();
        b.charging = charging.load();
        return Result<BatteryReading>::ok(b);
    }
    void shutdown() override {}

    // Convenience for tests reading like prose.
    void set(std::uint8_t p, bool chg = false) { percent = p; charging = chg; }
};

// Scriptable thermal source. Set the state (and, if you like, a numeric temp for the
// telemetry field); read() returns them verbatim. Set `fault` to make read() throw.
class FakeThermalSource final : public IThermalSource {
public:
    std::atomic<ThermalState> state{ThermalState::Nominal};
    std::atomic<bool>         fault{false};

    Status initialize() override { return Status::Ok; }
    Result<ThermalReading> read() override {
        if (fault.load()) throw std::runtime_error("thermal zone read fault");
        ThermalReading t;
        t.state = state.load();
        // A laptop-class backend would carry a numeric temp too; the fake supplies a
        // representative one per state so the telemetry field is exercised. No policy
        // decision depends on it (see power_source.hpp).
        switch (t.state) {
            case ThermalState::Nominal: t.soc_temp_c = 35.0f; break;
            case ThermalState::Warm:    t.soc_temp_c = 60.0f; break;
            case ThermalState::Hot:     t.soc_temp_c = 75.0f; break;
        }
        return Result<ThermalReading>::ok(t);
    }
    void shutdown() override {}

    void set(ThermalState s) { state = s; }
};

}  // namespace echo::power
