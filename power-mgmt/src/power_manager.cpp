#include "echo/power/power_manager.hpp"
#include "echo/log.hpp"

namespace echo::power {

const char* to_string(Profile p) noexcept {
    switch (p) {
        case Profile::Idle:        return "idle";
        case Profile::Interactive: return "interactive";
        case Profile::Throttled:   return "throttled";
    }
    return "unknown";
}

namespace {

// Thresholds at which we protect the battery/thermals over peak performance.
constexpr float        kThermalThrottleC   = 70.0f;
constexpr std::uint8_t kLowBatteryPercent  = 15;

class StubPowerManager final : public IPowerManager {
public:
    Status initialize() override {
        log_info("power", "power manager initialized (idle profile)");
        return Status::Ok;
    }

    Profile evaluate(const PowerState& state) override {
        // Thermal or battery pressure forces Throttled regardless of demand.
        if (state.soc_temp_c >= kThermalThrottleC ||
            (!state.charging && state.battery_percent <= kLowBatteryPercent)) {
            set_profile(Profile::Throttled);
        }
        // Otherwise the runtime drives Idle<->Interactive via wake events; the
        // manager only vetoes toward Throttled here.
        return profile_;
    }

    void report_latency(Stage /*stage*/, double /*elapsed_ms*/) override {
        // TODO(power): feed a moving-average controller that trades duty cycle for
        // thermal headroom while keeping the end-to-end budget satisfied.
    }

    Profile current_profile() const noexcept override { return profile_; }

    void shutdown() override { log_info("power", "power manager shut down"); }

private:
    void set_profile(Profile p) {
        if (p != profile_) {
            profile_ = p;
            log_info("power", to_string(p));
        }
    }

    Profile profile_ = Profile::Idle;
};

}  // namespace

std::unique_ptr<IPowerManager> make_power_manager() {
    return std::make_unique<StubPowerManager>();
}

}  // namespace echo::power
