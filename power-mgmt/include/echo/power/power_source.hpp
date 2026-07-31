// ECHO OS — power & thermal SOURCES (Phase 18).
//
// The read-only sensing boundary the power/thermal POLICY reasons over. It mirrors
// the discipline every engine has followed since Phase 3 (and IHttpClient before
// it): a narrow interface, a REAL backend hook, and a fully deterministic FAKE for
// tests — so the whole policy is verifiable in the dependency-free stub build with
// no real sensor present.
//
// These interfaces only READ. Deciding what to DO with a low battery or a warm SoC
// (duty-cycle vision, throttle inference, prioritise a reminder) is power_policy.hpp;
// applying it lives in the runtime. Keeping sensing and policy apart is what lets the
// policy be tested against scripted readings with no hardware in the loop.
//
// Honest hardware note, stated once here because it is the crux of Phase 18's scope:
// there is NO real battery gauge or skin-adjacent thermal sensor on a dev laptop, and
// certainly none on the not-yet-existing glasses. So make_power_source() /
// make_thermal_source() return a DOCUMENTED STUB (see power_source.cpp) — the shape of
// the real fuel-gauge / thermal-zone read is captured, but the values are simulated.
// The policy this feeds is real and tested; the sensor behind it is not. STATE.md logs
// this as a permanent-until-hardware gap, not a temporary one.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace echo::power {

// A battery reading. `percent` is state-of-charge in [0,100]; `charging` is true when
// on external power (which the policy treats as "no need to conserve").
struct BatteryReading {
    std::uint8_t percent  = 100;
    bool         charging = false;
};

// The thermal signal, as a coarse STATE rather than a raw temperature.
//
// Choice, documented (brief item 1): the primary thermal signal is a three-level state,
// not a calibrated °C. On the realistic target — glasses worn against the temple — the
// meaningful input is a small number of thermal-zone trip points ("comfortable / warm to
// the wearer / must shed heat now"), not a precise skin temperature the platform would
// expose. A discrete state is also what the policy actually needs (it picks a throttle
// level, not a PID setpoint). The optional numeric `soc_temp_c` is carried WHEN a backend
// happens to expose one (a dev laptop's SoC sensor does), but no policy decision depends on
// it — it is telemetry, so a state-only backend (the realistic glasses case) loses nothing.
enum class ThermalState : std::uint8_t {
    Nominal,  // comfortable; run at full capability
    Warm,     // elevated; begin shedding inference load
    Hot,      // must reduce load now to avoid an uncomfortable/unsafe temple temperature
};

const char* to_string(ThermalState s) noexcept;

// A thermal reading: the coarse state (always) plus an optional numeric SoC temperature
// (telemetry only — present on a laptop-class backend, absent on a state-only one).
struct ThermalReading {
    ThermalState         state      = ThermalState::Nominal;
    std::optional<float> soc_temp_c;  // nullopt when the backend exposes no numeric sensor
};

// Read-only battery/state-of-charge source. Cheap to call every tick (a fuel-gauge
// register read on real hardware).
class IPowerSource {
public:
    virtual ~IPowerSource() = default;
    virtual Status initialize() = 0;
    // A failed read returns fail(...) rather than a fabricated charge — the policy fails
    // to its last-known-good decision, never to a guessed battery level.
    virtual Result<BatteryReading> read() = 0;
    virtual void shutdown() = 0;
};

// Read-only thermal source (a thermal-zone / SoC-temperature read on real hardware).
class IThermalSource {
public:
    virtual ~IThermalSource() = default;
    virtual Status initialize() = 0;
    virtual Result<ThermalReading> read() = 0;
    virtual void shutdown() = 0;
};

// Real-backend hooks. On this dev laptop / the future embedded target there is no real
// sensor to read, so these are DOCUMENTED STUBS returning simulated-but-plausible values
// (see power_source.cpp). Tests never use these — they inject the deterministic fakes from
// fake_power_source.hpp through the runtime's DI seam.
std::unique_ptr<IPowerSource>   make_power_source();
std::unique_ptr<IThermalSource> make_thermal_source();

}  // namespace echo::power
