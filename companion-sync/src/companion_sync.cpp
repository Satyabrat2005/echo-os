#include "echo/companion/companion_sync.hpp"
#include "echo/log.hpp"

namespace echo::companion {

const char* to_string(AlertKind k) noexcept {
    switch (k) {
        case AlertKind::SafeModeEngaged:    return "safe-mode-engaged";
        case AlertKind::Distress:           return "distress";
        case AlertKind::Wandering:          return "wandering";
        case AlertKind::LowConfidenceTrend: return "low-confidence-trend";
    }
    return "unknown";
}

namespace {

class StubCompanionSync final : public ICompanionSync {
public:
    Status connect(Transport transport) override {
        // TODO(companion): bring up BLE GATT / WiFi, pair, establish an encrypted
        // channel to the caregiver app. Only alert/status/firmware characteristics
        // are exposed — no bulk data channel exists by design.
        connected_ = true;
        log_info("companion", transport == Transport::Ble ? "connected via BLE (stub)"
                                                           : "connected via WiFi (stub)");
        return Status::Ok;
    }

    Status send_alert(const Alert& alert) override {
        if (!connected_) return Status::Unavailable;
        log_warn("companion", to_string(alert.kind));
        return Status::Ok;
    }

    Status send_status(const StatusReport& /*status*/) override {
        if (!connected_) return Status::Unavailable;
        log_debug("companion", "status heartbeat sent (stub)");
        return Status::Ok;
    }

    Status poll_firmware_update() override {
        // TODO(companion): verify signature before applying. No update in scaffold.
        return Status::Unavailable;
    }

    bool connected() const noexcept override { return connected_; }
    void disconnect() override { connected_ = false; }

private:
    bool connected_ = false;
};

}  // namespace

std::unique_ptr<ICompanionSync> make_companion_sync() {
    return std::make_unique<StubCompanionSync>();
}

}  // namespace echo::companion
