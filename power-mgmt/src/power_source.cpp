#include "echo/power/power_source.hpp"
#include "echo/log.hpp"

namespace echo::power {

const char* to_string(ThermalState s) noexcept {
    switch (s) {
        case ThermalState::Nominal: return "nominal";
        case ThermalState::Warm:    return "warm";
        case ThermalState::Hot:     return "hot";
    }
    return "unknown";
}

namespace {

// --- REAL backend hook: a DOCUMENTED STUB ------------------------------------
//
// There is no real battery fuel gauge or skin-adjacent thermal sensor to read here — not
// on this dev laptop, and not on the not-yet-existing glasses. Rather than fabricate a
// varying charge (which would be a lie the policy above would faithfully act on), the real
// hook reports a fixed, plainly-simulated healthy reading and says so once at init. This is
// the honest analogue of the sensor-pipeline's stub capture frontends: the SHAPE of the read
// is real (this is exactly where a `/sys/class/power_supply/*/capacity` read, or an embedded
// fuel-gauge I2C transaction, and a `/sys/class/thermal/thermal_zone*/temp` read would go);
// the DATA is simulated until real hardware exists. STATE.md logs this as a permanent-until-
// hardware gap. Tests never touch this path — they inject the deterministic fakes.
class StubPowerSource final : public IPowerSource {
public:
    Status initialize() override {
        log_info("power",
                 "battery source: DOCUMENTED STUB (no real fuel gauge on this target; "
                 "reports a fixed simulated charge — see power_source.cpp)");
        return Status::Ok;
    }
    Result<BatteryReading> read() override {
        // Simulated healthy, discharging reading. On real hardware this is a register/sysfs
        // read; here it is a constant so the device runs full-rate rather than throttling on
        // fabricated pressure.
        BatteryReading b;
        b.percent  = 100;
        b.charging = false;
        return Result<BatteryReading>::ok(b);
    }
    void shutdown() override {}
};

class StubThermalSource final : public IThermalSource {
public:
    Status initialize() override {
        log_info("power",
                 "thermal source: DOCUMENTED STUB (no real thermal zone on this target; "
                 "reports nominal — see power_source.cpp)");
        return Status::Ok;
    }
    Result<ThermalReading> read() override {
        ThermalReading t;
        t.state      = ThermalState::Nominal;
        t.soc_temp_c = 35.0f;  // plausible idle SoC temp; telemetry only
        return Result<ThermalReading>::ok(t);
    }
    void shutdown() override {}
};

}  // namespace

std::unique_ptr<IPowerSource>   make_power_source()   { return std::make_unique<StubPowerSource>(); }
std::unique_ptr<IThermalSource> make_thermal_source() { return std::make_unique<StubThermalSource>(); }

}  // namespace echo::power
