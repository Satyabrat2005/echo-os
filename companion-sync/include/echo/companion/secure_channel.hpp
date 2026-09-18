// ECHO OS — the sealed frame format for the caregiver link (Phase 22).
//
// This is where confidentiality and integrity actually happen. A transport moves
// bytes; SecureChannel decides whether those bytes are worth listening to.
//
// ENCRYPT-THEN-MAC. The payload is encrypted with AES-256-CTR (the Phase 16
// cipher), and an AES-256-CMAC is taken over the ENTIRE frame — header included —
// and appended. On receive, the MAC is checked FIRST; a frame that fails it is
// dropped without decrypting, without parsing, and without touching the JSON
// decoder. This ordering is the point: ADR-14 was explicit that CTR alone is
// malleable, and the inbound path is where malleability stops being an accepted
// trade-off and becomes a way to steer what the wearer is told.
//
// NONCES. The IV is derived, not random: direction byte || zeros || sequence
// number. CTR keystream reuse is catastrophic, so the nonce must never repeat for a
// given key — deriving it from a strictly-increasing per-direction sequence makes
// that structural rather than dependent on an RNG the stub build doesn't have. The
// IV is carried in the header and covered by the MAC, so an attacker cannot move it.
//
// REPLAY. The MAC proves a frame was authentic ONCE; it says nothing about
// freshness. A recorded "add reminder: take two of the blue pills" replayed
// nightly is a valid frame forever. So the receiver keeps the highest sequence
// number it has accepted and refuses anything at or below it. Frames therefore
// cannot be reordered or replayed, and a dropped frame is not recoverable by
// resend — an acceptable trade for a link whose payloads are idempotent-ish and
// re-derivable on the next tick.
//
// WHAT PAIRING DOES AND DOES NOT GUARANTEE — stated as bluntly as ADR-14 stated
// the AES-CTR caveat:
//   IT DOES     bind the two endpoints that completed a supervised pairing session
//               to a shared secret, so a third party who never held that secret can
//               neither read the digest nor forge a command.
//   IT DOES NOT authenticate a PERSON. The key authenticates the channel; both ends
//               hold the same secret and can forge each other's traffic. "The
//               caregiver sent this" really means "something holding the pairing
//               key sent this".
//   IT DOES NOT survive an attacker with filesystem access. The key sits in a file
//               with owner-only permissions, exactly like the Phase 16 device key,
//               and is no more hardware-backed than that one was.
//   IT DOES NOT provide forward secrecy. One long-lived symmetric key, derived once
//               out-of-band. Compromise it and every past captured frame is
//               readable. A real deployment wants an ECDH handshake per session;
//               that needs asymmetric crypto this build does not have.
//   IT DOES NOT establish trust. A caregiver who pairs is a caregiver the wearer (or
//               a supervised session) let in. Consent, not the key, is what decides
//               whether anything may flow — and consent is revocable; a key is not.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "echo/companion/transport.hpp"
#include "echo/crypto/aes256.hpp"
#include "echo/crypto/cmac.hpp"
#include "echo/result.hpp"

namespace echo::companion {

// What a frame carries. Kept out of the encrypted payload so a receiver can route
// without decrypting — and covered by the MAC, so it still can't be tampered with.
enum class FrameType : std::uint8_t {
    Digest = 1,    // device -> caregiver: counts and states
    Command = 2,   // caregiver -> device: an untrusted request
    Firmware = 3,  // caregiver -> device: an update offer
};

// Which side sent it. Feeds the IV so the two directions can never collide on a
// counter value under the same key.
enum class Direction : std::uint8_t {
    DeviceToCaregiver = 0,
    CaregiverToDevice = 1,
};

// Frame layout, all integers big-endian:
//   [0]      version
//   [1]      frame type
//   [2]      direction
//   [3]      reserved (zero)
//   [4..11]  sequence number (u64)
//   [12..27] IV (derived; carried explicitly so the MAC covers it)
//   [28..31] payload length (u32)
//   [32..]   ciphertext
//   [tail]   16-byte CMAC over every preceding byte
inline constexpr std::uint8_t  kFrameVersion   = 1;
inline constexpr std::size_t   kFrameHeaderLen = 32;
inline constexpr std::size_t   kFrameMacLen    = 16;
inline constexpr std::size_t   kFrameOverhead  = kFrameHeaderLen + kFrameMacLen;

// Hard cap on a payload. The inbound decoder has its own, tighter limit; this one
// exists so a hostile length field can never make the receiver allocate.
inline constexpr std::size_t kMaxPayloadBytes = 8192;

struct OpenedFrame {
    FrameType     type = FrameType::Digest;
    Direction     direction = Direction::CaregiverToDevice;
    std::uint64_t seq = 0;
    std::string   payload;
};

// Why a frame was refused. Counted rather than logged verbatim: a hostile peer
// should not be able to fill the device's log by sending garbage.
enum class FrameReject : std::uint8_t {
    None = 0,
    TooShort,
    TooLong,
    BadVersion,
    BadLength,
    BadMac,
    Replay,
    WrongDirection,
};

const char* to_string(FrameReject reason) noexcept;

// The sealed-channel endpoint. One per direction pair; holds the pairing key, the
// outbound counter, and the inbound high-water mark.
class SecureChannel {
public:
    SecureChannel(const crypto::Key256& key, Direction send_direction) noexcept;

    // Seal a payload into a frame ready for the wire.
    Frame seal(FrameType type, const std::string& payload);

    // Verify and decrypt. Returns false — and sets `reason` — on anything at all
    // wrong. The MAC is checked before the ciphertext is touched.
    bool open(const Frame& frame, OpenedFrame& out, FrameReject& reason) noexcept;

    std::uint64_t next_send_seq() const noexcept { return send_seq_; }
    std::uint64_t highest_accepted_seq() const noexcept { return recv_seq_; }

    // How many frames this endpoint has refused, by reason — the signal a caregiver
    // link is being probed, without logging the probe's contents.
    std::uint32_t rejects(FrameReject reason) const noexcept;
    std::uint32_t total_rejects() const noexcept;

private:
    crypto::Key256 key_{};
    Direction      send_direction_;
    Direction      expect_direction_;
    std::uint64_t  send_seq_ = 1;  // 0 is never used, so "seq 0" is always invalid
    std::uint64_t  recv_seq_ = 0;
    std::uint32_t  rejects_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
};

// Where the pairing key lives: alongside the memory database, exactly like the
// Phase 16 device key, with the same owner-only permissions. This is deliberately
// NOT a third key-storage mechanism — it is the second consumer of the first one.
// Overridable with $ECHO_PAIRING_KEY for tests.
std::string pairing_key_path_for(const std::string& db_path);

}  // namespace echo::companion
