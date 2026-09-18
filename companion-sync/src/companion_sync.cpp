#include "echo/companion/companion_sync.hpp"
#include "echo/log.hpp"

#include <optional>
#include <utility>

namespace echo::companion {

const char* to_string(AlertKind k) noexcept {
    switch (k) {
        case AlertKind::SafeModeEngaged:    return "safe-mode-engaged";
        case AlertKind::Distress:           return "distress";
        case AlertKind::Wandering:          return "wandering";
        case AlertKind::LowConfidenceTrend: return "low-confidence-trend";
        case AlertKind::EngineDegraded:     return "engine-degraded";
    }
    return "unknown";
}

namespace {

// Serialize a digest onto the wire, field by field, big-endian — a struct memcpy
// would bake this build's padding and endianness into a protocol.
//
// Note what this encoder CANNOT do, and why that is the whole point: every digest
// field is a scalar or an enum (proven at compile time in caregiver_digest.hpp), so
// there is no branch here that could ever emit a length-prefixed string. The
// encoder has no way to leak content because the type it encodes has no content.
void put_u32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (24 - 8 * i)) & 0xFF));
}

void put_i64(std::string& out, std::int64_t v) {
    const auto u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((u >> (56 - 8 * i)) & 0xFF));
}

std::string encode_digest(const CaregiverDigest& d) {
    std::string out;
    out.reserve(56);
    put_u32(out, d.window_hours);
    put_u32(out, d.reminders_due);
    put_u32(out, d.reminders_delivered);
    put_u32(out, d.reminders_acknowledged);
    put_u32(out, d.safe_mode_engagements);
    put_u32(out, d.unverified_answers);
    put_u32(out, d.people_known);
    // Phase 23. Adding a field to ECHO_CAREGIVER_DIGEST_FIELDS is NOT enough by itself —
    // this serializer is hand-written, not X-macro-generated, so a field that compiles
    // fine (the privacy static_assert only checks type) can still silently never leave
    // the device if this call is forgotten. See caregiver_digest.hpp's field comment.
    put_u32(out, d.wandering_flags);
    put_u32(out, d.distress_flags);
    put_i64(out, d.generated_at);
    put_i64(out, d.last_sync_at);
    out.push_back(static_cast<char>(d.state));
    out.push_back(static_cast<char>(d.scope));
    return out;
}

class StubCompanionSync final : public ICompanionSync {
public:
    StubCompanionSync() = default;

    StubCompanionSync(std::unique_ptr<ICompanionTransport> transport,
                      const crypto::Key256&                pairing_key,
                      std::unique_ptr<IFirmwareVerifier>   verifier)
        : transport_(std::move(transport)),
          verifier_(std::move(verifier)),
          // The device end of the conversation: it seals outbound frames as
          // DeviceToCaregiver and will only accept CaregiverToDevice ones, so a
          // frame the device itself emitted, reflected back at it, is refused.
          channel_(std::in_place, pairing_key, Direction::DeviceToCaregiver),
          limiter_(/*burst=*/5, /*refill_seconds=*/60, /*daily_cap=*/50) {}

    Status connect(Transport transport) override {
        // With a transport wired in (Phase 22), connect() means something: bring the
        // link up and report honestly when it can't come up. The BLE backend returns
        // Unavailable here, which is the truth on a build with no radio.
        if (transport_) {
            const Status st = transport_->open();
            connected_ = (st == Status::Ok);
            log_info("companion", connected_ ? "companion link up" : "companion link unavailable");
            return st;
        }

        // No transport: the pre-Phase-22 scaffold behaviour, unchanged, so every
        // existing caller keeps working exactly as before.
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
        // The scaffold's TODO promised "verify signature before applying" and then
        // returned Unavailable, which meant the promise was never exercised. Phase 22
        // makes the refusal structural instead: any offered image goes through the
        // verifier, and the only verifier that exists refuses everything (inbound.hpp
        // explains why a symmetric MAC here would be worse than nothing).
        //
        // No image offered is Unavailable. An image offered without a valid signature
        // is ALSO Unavailable, and is never written anywhere.
        if (!pending_firmware_) return Status::Unavailable;

        const FirmwareImage image = std::move(*pending_firmware_);
        pending_firmware_.reset();

        if (!verifier_ || !verifier_->verify(image)) {
            ++rejected_firmware_;
            log_warn("companion", "firmware update refused: signature not verified");
            return Status::Unavailable;
        }

        // Unreachable with the null verifier, and deliberately the ONLY place an
        // accepted image could ever be applied — so a real verifier, when there is
        // one, has exactly one seam to plug into and nothing else moves.
        log_info("companion", "firmware update verified");
        return Status::Ok;
    }

    bool connected() const noexcept override { return connected_; }

    void disconnect() override {
        connected_ = false;
        if (transport_) transport_->close();
    }

    // --- Phase 22 ------------------------------------------------------------

    void set_consent(ConsentScope scope) noexcept override { scope_ = scope; }
    ConsentScope consent_scope() const noexcept override { return scope_; }

    Status send_digest(const CaregiverDigest& digest) override {
        // Consent is checked BEFORE connectivity, so a revoked link reports "not
        // permitted" rather than "offline". Those are different facts.
        if (!permits_digest(scope_)) {
            // Unavailable, never an empty digest. An all-zero digest reads as
            // "nothing happened today" — a factual claim about the wearer that this
            // device has no right to make when it has been told not to speak.
            // Someone could decide not to drive over because of it.
            log_debug("companion", "digest suppressed: no consent");
            return Status::Unavailable;
        }
        if (!connected_) return Status::Unavailable;

        // A digest built under one scope must never go out claiming another: the
        // receiving side reads digest.scope to know whether commands are permitted,
        // so a stale value here would be a privilege statement.
        CaregiverDigest to_send = digest;
        to_send.scope = scope_;

        if (!transport_ || !channel_) {
            log_debug("companion", "digest accepted (no transport; nothing left the process)");
            return Status::Ok;
        }

        const Frame frame = channel_->seal(FrameType::Digest, encode_digest(to_send));
        return transport_->send(frame);
    }

    std::vector<InboundCommand> poll_inbound(std::int64_t now) override {
        std::vector<InboundCommand> accepted;
        if (!transport_ || !channel_) return accepted;

        for (const Frame& frame : transport_->receive()) {
            // 1. Authenticity and freshness. A frame that fails the MAC, replays a
            //    sequence number, or claims the wrong direction dies here — before
            //    the cipher, and long before the JSON decoder.
            OpenedFrame opened;
            FrameReject frame_reason = FrameReject::None;
            if (!channel_->open(frame, opened, frame_reason)) {
                ++rejected_frames_;
                continue;
            }

            if (opened.type != FrameType::Command) {
                // A firmware offer rides the same channel. It is stashed, never
                // applied here; poll_firmware_update() decides, and it fails closed.
                if (opened.type == FrameType::Firmware) {
                    FirmwareImage image;
                    image.bytes.assign(opened.payload.begin(), opened.payload.end());
                    pending_firmware_ = std::move(image);
                }
                continue;
            }

            // 2. Consent. Being able to authenticate is not permission to write, and
            //    permission to read counts (ConsentScope::Digest) does not imply it
            //    either — that separation is the reason there are two scopes.
            if (!permits_commands(scope_)) {
                ++rejected_commands_;
                continue;
            }

            // 3. Schema and field validation, on the hardened parser.
            const DecodeResult decoded = decode_command(opened.payload, now, last_command_seq_);
            if (!decoded.ok) {
                ++rejected_commands_;
                // The REASON is logged; the payload never is. A peer must not be
                // able to write attacker-chosen text into the wearer's log just by
                // sending it.
                log_warn("companion", to_string(decoded.reason));
                continue;
            }

            // 4. Rate limit. Holding a valid pairing key is not a licence to flood:
            //    every accepted command becomes something spoken aloud to a person
            //    with memory loss, and there is a number of those per hour that is
            //    help and a number that is harm.
            if (!limiter_.allow(now)) {
                ++rejected_commands_;
                log_warn("companion", "inbound command rate-limited");
                continue;
            }

            last_command_seq_ = decoded.command.seq;
            accepted.push_back(decoded.command);
        }

        return accepted;
    }

    std::uint32_t rejected_commands() const noexcept override { return rejected_commands_; }

    std::uint32_t rejected_frames() const noexcept override {
        return rejected_frames_ + rejected_firmware_;
    }

private:
    bool connected_ = false;

    std::unique_ptr<ICompanionTransport> transport_;
    std::unique_ptr<IFirmwareVerifier>   verifier_;
    std::optional<SecureChannel>         channel_;

    ConsentScope scope_ = ConsentScope::None;  // the only safe default

    RateLimiter   limiter_;
    std::uint64_t last_command_seq_ = 0;
    std::uint32_t rejected_commands_ = 0;
    std::uint32_t rejected_frames_ = 0;
    std::uint32_t rejected_firmware_ = 0;

    std::optional<FirmwareImage> pending_firmware_;
};

}  // namespace

std::unique_ptr<ICompanionSync> make_companion_sync() {
    return std::make_unique<StubCompanionSync>();
}

std::unique_ptr<ICompanionSync> make_companion_sync(
    std::unique_ptr<ICompanionTransport> transport,
    const crypto::Key256&                pairing_key,
    std::unique_ptr<IFirmwareVerifier>   verifier) {
    return std::make_unique<StubCompanionSync>(std::move(transport), pairing_key,
                                               std::move(verifier));
}

}  // namespace echo::companion
