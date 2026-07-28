// ECHO OS — power management.
//
// Balances the 120 ms latency target against battery life and thermals. It owns
// the DVFS/duty-cycle policy: when idle it drops sensors and the NPU to low
// power; when a wake-word fires it ramps to the performance profile fast enough
// that the FIRST interaction still meets the latency budget.
//
// Zero jank (principle #3) constrains this module: throttling must degrade the
// duty cycle, never the smoothness of an in-flight response.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"
#include "echo/latency.hpp"

#include <memory>

namespace echo::power {

// Operating profiles, coarsest lever the runtime pulls.
enum class Profile : std::uint8_t {
    Idle,         // sensors low-rate, NPU parked, waiting on wake-word
    Interactive,  // full clocks, meeting the latency budget
    Throttled,    // thermal/battery pressure: reduce duty cycle, keep responses smooth
};

const char* to_string(Profile p) noexcept;

// A live view of the constraints the manager reasons over.
struct PowerState {
    std::uint8_t battery_percent = 100;
    float        soc_temp_c      = 30.0f;   // system-on-chip temperature
    bool         charging        = false;
};

class IPowerManager {
public:
    virtual ~IPowerManager() = default;

    virtual Status initialize() = 0;

    // Called by the runtime with the latest sensed constraints; returns the
    // profile the system should now run in.
    virtual Profile evaluate(const PowerState& state) = 0;

    // Report a measured stage latency so the manager can learn how much thermal
    // headroom the budget currently has.
    virtual void report_latency(Stage stage, double elapsed_ms) = 0;

    virtual Profile current_profile() const noexcept = 0;

    virtual void shutdown() = 0;
};

std::unique_ptr<IPowerManager> make_power_manager();

}  // namespace echo::power
