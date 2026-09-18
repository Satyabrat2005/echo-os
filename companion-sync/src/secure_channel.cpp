// ECHO OS — sealed frames for the caregiver link. See secure_channel.hpp for the
// frame layout and for the plain-language account of what pairing does and does not
// guarantee.
#include "echo/companion/secure_channel.hpp"

#include <cstdlib>
#include <cstring>

namespace echo::companion {

const char* to_string(FrameReject reason) noexcept {
    switch (reason) {
        case FrameReject::None:           return "none";
        case FrameReject::TooShort:       return "too-short";
        case FrameReject::TooLong:        return "too-long";
        case FrameReject::BadVersion:     return "bad-version";
        case FrameReject::BadLength:      return "bad-length";
        case FrameReject::BadMac:         return "bad-mac";
        case FrameReject::Replay:         return "replay";
        case FrameReject::WrongDirection: return "wrong-direction";
    }
    return "unknown";
}

namespace {

void put_u64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>((v >> (56 - 8 * i)) & 0xFF);
}

std::uint64_t get_u64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

void put_u32(std::uint8_t* p, std::uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>((v >> (24 - 8 * i)) & 0xFF);
}

std::uint32_t get_u32(const std::uint8_t* p) noexcept {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | p[i];
    return v;
}

// The CTR nonce: direction || zeros || sequence. Derived rather than random, so
// uniqueness under a given key is structural — the sequence strictly increases and
// the direction byte keeps the two halves of the conversation from ever colliding
// on the same counter. Keystream reuse is the one mistake CTR does not survive, and
// this build has no RNG in the send path to lean on.
crypto::Block derive_iv(Direction dir, std::uint64_t seq) noexcept {
    crypto::Block iv{};
    iv[0] = static_cast<std::uint8_t>(dir);
    put_u64(iv.data() + 8, seq);
    return iv;
}

}  // namespace

SecureChannel::SecureChannel(const crypto::Key256& key, Direction send_direction) noexcept
    : key_(key),
      send_direction_(send_direction),
      expect_direction_(send_direction == Direction::DeviceToCaregiver
                            ? Direction::CaregiverToDevice
                            : Direction::DeviceToCaregiver) {}

Frame SecureChannel::seal(FrameType type, const std::string& payload) {
    const std::uint64_t seq = send_seq_++;
    const crypto::Block iv  = derive_iv(send_direction_, seq);

    Frame frame(kFrameHeaderLen + payload.size() + kFrameMacLen, 0);

    frame[0] = kFrameVersion;
    frame[1] = static_cast<std::uint8_t>(type);
    frame[2] = static_cast<std::uint8_t>(send_direction_);
    frame[3] = 0;
    put_u64(frame.data() + 4, seq);
    std::memcpy(frame.data() + 12, iv.data(), iv.size());
    put_u32(frame.data() + 28, static_cast<std::uint32_t>(payload.size()));

    if (!payload.empty()) {
        std::memcpy(frame.data() + kFrameHeaderLen, payload.data(), payload.size());
        crypto::ctr_xcrypt(key_, iv, frame.data() + kFrameHeaderLen, payload.size());
    }

    // Encrypt-then-MAC: the tag covers the header AND the ciphertext, so neither the
    // sequence number, the direction, the type, the IV, nor a single byte of payload
    // can be altered without the receiver noticing.
    const std::size_t macced = kFrameHeaderLen + payload.size();
    const crypto::Mac mac = crypto::cmac(key_, frame.data(), macced);
    std::memcpy(frame.data() + macced, mac.data(), mac.size());

    return frame;
}

bool SecureChannel::open(const Frame& frame, OpenedFrame& out, FrameReject& reason) noexcept {
    auto refuse = [&](FrameReject r) {
        reason = r;
        ++rejects_[static_cast<std::size_t>(r) & 7u];
        return false;
    };

    reason = FrameReject::None;

    if (frame.size() < kFrameOverhead) return refuse(FrameReject::TooShort);
    if (frame.size() > kFrameOverhead + kMaxPayloadBytes) return refuse(FrameReject::TooLong);
    if (frame[0] != kFrameVersion) return refuse(FrameReject::BadVersion);

    const std::uint32_t len = get_u32(frame.data() + 28);
    if (static_cast<std::size_t>(len) + kFrameOverhead != frame.size())
        return refuse(FrameReject::BadLength);

    // MAC FIRST, before a single byte is decrypted or interpreted. This ordering is
    // the entire reason the inbound path is defensible: a tampered frame never
    // reaches the cipher, let alone the JSON decoder.
    const std::size_t macced = kFrameHeaderLen + len;
    const crypto::Mac want = crypto::cmac(key_, frame.data(), macced);
    crypto::Mac got{};
    std::memcpy(got.data(), frame.data() + macced, got.size());
    if (!crypto::mac_equal(want, got)) return refuse(FrameReject::BadMac);

    // Authentic — now the header can be trusted enough to route on.
    const auto dir = static_cast<Direction>(frame[2]);
    if (dir != expect_direction_) return refuse(FrameReject::WrongDirection);

    const std::uint64_t seq = get_u64(frame.data() + 4);
    // A MAC proves a frame was genuine ONCE. Freshness is this line: anything at or
    // below the high-water mark is a replay, including seq 0, which seal() never
    // emits and which therefore can only be a forgery or a bug.
    if (seq == 0 || seq <= recv_seq_) return refuse(FrameReject::Replay);

    crypto::Block iv{};
    std::memcpy(iv.data(), frame.data() + 12, iv.size());

    std::string payload(reinterpret_cast<const char*>(frame.data() + kFrameHeaderLen), len);
    if (len > 0)
        crypto::ctr_xcrypt(key_, iv, reinterpret_cast<std::uint8_t*>(&payload[0]), payload.size());

    out.type      = static_cast<FrameType>(frame[1]);
    out.direction = dir;
    out.seq       = seq;
    out.payload   = std::move(payload);

    recv_seq_ = seq;
    return true;
}

std::uint32_t SecureChannel::rejects(FrameReject reason) const noexcept {
    return rejects_[static_cast<std::size_t>(reason) & 7u];
}

std::uint32_t SecureChannel::total_rejects() const noexcept {
    std::uint32_t total = 0;
    // Index 0 is FrameReject::None, which open() never counts.
    for (std::size_t i = 1; i < 8; ++i) total += rejects_[i];
    return total;
}

std::string pairing_key_path_for(const std::string& db_path) {
    if (const char* env = std::getenv("ECHO_PAIRING_KEY"); env && *env) return env;
    if (db_path == ":memory:" || db_path.empty()) return {};
    // Beside the store, next to the Phase 16 device key, same owner-only posture as
    // appkit's .echo-tokens/. Deliberately the SECOND consumer of one key mechanism
    // rather than a third mechanism of its own.
    return db_path + ".pairing.key";
}

}  // namespace echo::companion
