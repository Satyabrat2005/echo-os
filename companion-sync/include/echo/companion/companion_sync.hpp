// ECHO OS — companion sync.
//
// The only path off the device. It carries FOUR things and nothing else:
//   1. alerts   — caregiver-directed events (distress, safe-mode, wandering)
//   2. status   — battery, health, connectivity heartbeats
//   3. digests  — consented counts and states for a caregiver (Phase 22)
//   4. firmware — signed OS updates pulled down
//
// It NEVER carries raw camera, microphone, or EEG data. That is not a policy
// toggle — the transport has no API that accepts a SensorFrame. Privacy by
// default (principle #4) is enforced by the shape of this interface.
//
// PHASE 22 ADDED A FOURTH ITEM AND AN INBOUND DIRECTION. Both are narrower than
// they sound, and the compile-time proof that made items 1-3 trustworthy was
// extended rather than relaxed:
//
//   The digest (caregiver_digest.hpp) is counts and states — every field a scalar
//   or an enum, proven field-by-field at compile time. It is NOT a second, softer
//   channel for memory content; if a field would need a static_assert weakened, it
//   does not ship.
//
//   The digest is gated on CONSENT held on-device (echo/consent.hpp), pushed in
//   here as a plain enum so this module still cannot name a memory type. With no
//   consent, send_digest() returns Unavailable — never an empty digest, which would
//   read to a caregiver as "nothing happened today" rather than "you are not
//   permitted to see this". Those are opposite statements and the difference could
//   matter to someone deciding whether to drive over.
//
//   Inbound (inbound.hpp) is the first path where bytes the device did not produce
//   can change what the wearer is told. Everything arriving is length-capped,
//   schema-validated on the Phase 6 hardened parser, replay-checked, rate-limited,
//   and announced to the wearer before it takes effect.
#pragma once

#include "echo/companion/caregiver_digest.hpp"
#include "echo/companion/inbound.hpp"
#include "echo/companion/secure_channel.hpp"
#include "echo/companion/transport.hpp"
#include "echo/consent.hpp"
#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

    // Check for and apply a signed firmware update. Returns Unavailable if none —
    // and, as of Phase 22, also if one is offered but its signature does not verify.
    // The verifier that exists today refuses everything (see inbound.hpp); this path
    // fails closed on purpose rather than half-verifying.
    virtual Status poll_firmware_update() = 0;

    virtual bool connected() const noexcept = 0;

    virtual void disconnect() = 0;

    // --- Phase 22: the caregiver boundary ------------------------------------

    // Push the consent currently recorded on-device. The runtime calls this every
    // tick, which is what makes revocation go cold on the NEXT TICK by
    // construction, rather than by anyone remembering to call a teardown.
    //
    // A plain enum crosses here, never a memory record: this module still cannot
    // name a memory type, and the compile-time proof still says so.
    virtual void set_consent(ConsentScope scope) noexcept = 0;
    virtual ConsentScope consent_scope() const noexcept = 0;

    // Send a digest. Returns Unavailable with no consent — deliberately NOT an
    // empty digest. "Nothing happened today" and "you are not permitted to see
    // this" are opposite statements, and the difference could decide whether
    // someone drives over to check.
    virtual Status send_digest(const CaregiverDigest& digest) = 0;

    // Drain the inbound queue, returning only commands that survived every check:
    // size cap, MAC, replay window, schema, field validation, rate limit, consent.
    // Rejected traffic is counted, not returned, and never logged verbatim.
    //
    // Returning a validated command is NOT applying it. The caller decides what to
    // do with it, announces it to the wearer, and only then writes.
    virtual std::vector<InboundCommand> poll_inbound(std::int64_t now) = 0;

    // Counters for what was refused — the signal that a link is being probed,
    // available without keeping any of the probe's contents.
    virtual std::uint32_t rejected_commands() const noexcept = 0;
    virtual std::uint32_t rejected_frames() const noexcept = 0;
};

// Default construction: no transport, no verifier — the pre-Phase-22 behaviour, so
// every existing caller is unaffected. Consent defaults to None, so a device built
// this way sends no digest and accepts no command.
std::unique_ptr<ICompanionSync> make_companion_sync();

// Phase 22 construction: a transport (fake / loopback / documented BLE stub), the
// pairing key for the sealed channel, and a firmware verifier. Passing
// make_null_verifier() — the only one that exists — means every update is refused,
// which is the only honest default today.
std::unique_ptr<ICompanionSync> make_companion_sync(
    std::unique_ptr<ICompanionTransport> transport,
    const crypto::Key256&                pairing_key,
    std::unique_ptr<IFirmwareVerifier>   verifier);

}  // namespace echo::companion
