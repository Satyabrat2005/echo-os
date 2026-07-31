// ECHO OS — companion sync.
//
// The only path off the device. It carries THREE things and nothing else:
//   1. alerts   — caregiver-directed events (distress, safe-mode, wandering)
//   2. status   — battery, health, connectivity heartbeats
//   3. firmware — signed OS updates pulled down
//
// It NEVER carries raw camera, microphone, or EEG data. That is not a policy
// toggle — the transport has no API that accepts a SensorFrame. Privacy by
// default (principle #4) is enforced by the shape of this interface.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace echo::companion {

enum class Transport : std::uint8_t { Ble, Wifi };

enum class AlertKind : std::uint8_t {
    SafeModeEngaged,    // system deferred; caregiver may want to check in
    Distress,           // vocal stress / distress detected
    Wandering,          // location/behavior anomaly
    LowConfidenceTrend, // repeated low-confidence responses
    EngineDegraded      // a subsystem (vision/ASR/LLM/TTS/memory) failed or needs a restart (Phase 17)
};

const char* to_string(AlertKind k) noexcept;

// A caregiver-directed alert. Note: it carries a *category and a timestamp*, not
// the sensor evidence that produced it.
struct Alert {
    AlertKind     kind;
    TimePoint     at;
    std::string   note;  // short human-readable summary, no raw data
};

// Periodic device health snapshot.
struct StatusReport {
    RuntimeState  state;
    std::uint8_t  battery_percent = 0;
    std::int8_t   rssi_dbm        = 0;
    std::uint32_t uptime_seconds  = 0;
};

class ICompanionSync {
public:
    virtual ~ICompanionSync() = default;

    // Bring up the chosen transport and pair with the companion app.
    virtual Status connect(Transport transport) = 0;

    // Push a caregiver alert. Best-effort, queued if offline.
    virtual Status send_alert(const Alert& alert) = 0;

    // Push a status heartbeat.
    virtual Status send_status(const StatusReport& status) = 0;

    // Check for and apply a signed firmware update. Returns Unavailable if none.
    virtual Status poll_firmware_update() = 0;

    virtual bool connected() const noexcept = 0;

    virtual void disconnect() = 0;
};

std::unique_ptr<ICompanionSync> make_companion_sync();

}  // namespace echo::companion
