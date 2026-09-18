// ECHO OS — AES-256 (CTR) implementation. See aes256.hpp for why this is hand-rolled
// rather than pulled from OpenSSL/SQLCipher, and note that its correctness is proven
// against FIPS-197 and NIST SP 800-38A vectors in tests/memory_engine_test.cpp.
//
// This is a straight, unclever implementation of the FIPS-197 algorithm (Nk=8,
// Nr=14). It is not constant-time and makes no side-channel claims — an honest
// limitation for a local at-rest key that never leaves the device (ADR-14). It is
// deliberately small and readable so it can be audited by eye.
#include "echo/crypto/aes256.hpp"

#include <cstring>

namespace echo::crypto {

namespace {

// The AES S-box (FIPS-197 Fig. 7).
constexpr std::uint8_t kSBox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

// Round constants (only the first Nr are ever consumed for a 256-bit key).
constexpr std::uint8_t kRcon[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

constexpr int kNk = 8;    // key length in 32-bit words (256-bit key)
constexpr int kNr = 14;   // number of rounds
constexpr int kNb = 4;    // block size in words (always 4 for AES)

using RoundKey = std::array<std::uint8_t, (kNr + 1) * std::size_t{16}>;  // 240 bytes

// Multiply by x (0x02) in GF(2^8) with the AES reduction polynomial.
inline std::uint8_t xtime(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>((x << 1) ^ ((x >> 7) * 0x1b));
}

// Full GF(2^8) multiply (used by MixColumns).
inline std::uint8_t gmul(std::uint8_t a, std::uint8_t b) noexcept {
    std::uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        const std::uint8_t hi = a & 0x80;
        a = static_cast<std::uint8_t>(a << 1);
        if (hi) a ^= 0x1b;
        b = static_cast<std::uint8_t>(b >> 1);
    }
    return p;
}

RoundKey expand_key(const Key256& key) noexcept {
    RoundKey rk{};
    // First Nk words are the key itself.
    std::memcpy(rk.data(), key.data(), key.size());

    // Index arithmetic is done in size_t throughout so no signed multiplication is
    // ever widened after the fact (keeps clang-tidy's widening checks satisfied).
    constexpr std::size_t kTotalWords = static_cast<std::size_t>(kNb) * (kNr + 1);
    std::uint8_t temp[4];
    for (std::size_t i = kNk; i < kTotalWords; ++i) {
        for (std::size_t j = 0; j < 4; ++j) temp[j] = rk[(i - 1) * 4 + j];
        if (i % kNk == 0) {
            // RotWord + SubWord + Rcon.
            const std::uint8_t t = temp[0];
            temp[0] = static_cast<std::uint8_t>(kSBox[temp[1]] ^ kRcon[i / kNk]);
            temp[1] = kSBox[temp[2]];
            temp[2] = kSBox[temp[3]];
            temp[3] = kSBox[t];
        } else if (i % kNk == 4) {
            // AES-256 has an extra SubWord at the quarter point.
            for (auto& b : temp) b = kSBox[b];
        }
        for (std::size_t j = 0; j < 4; ++j)
            rk[i * 4 + j] = static_cast<std::uint8_t>(rk[(i - kNk) * 4 + j] ^ temp[j]);
    }
    return rk;
}

using State = std::uint8_t[4][4];

void add_round_key(State s, const RoundKey& rk, std::size_t round) noexcept {
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            s[r][c] ^= rk[round * 16 + c * 4 + r];
}

void sub_bytes(State s) noexcept {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) s[r][c] = kSBox[s[r][c]];
}

void shift_rows(State s) noexcept {
    std::uint8_t t;
    // row 1 <<< 1
    t = s[1][0]; s[1][0] = s[1][1]; s[1][1] = s[1][2]; s[1][2] = s[1][3]; s[1][3] = t;
    // row 2 <<< 2
    t = s[2][0]; s[2][0] = s[2][2]; s[2][2] = t;
    t = s[2][1]; s[2][1] = s[2][3]; s[2][3] = t;
    // row 3 <<< 3 (== >>> 1)
    t = s[3][3]; s[3][3] = s[3][2]; s[3][2] = s[3][1]; s[3][1] = s[3][0]; s[3][0] = t;
}

void mix_columns(State s) noexcept {
    for (int c = 0; c < 4; ++c) {
        const std::uint8_t a0 = s[0][c], a1 = s[1][c], a2 = s[2][c], a3 = s[3][c];
        s[0][c] = static_cast<std::uint8_t>(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
        s[1][c] = static_cast<std::uint8_t>(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
        s[2][c] = static_cast<std::uint8_t>(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
        s[3][c] = static_cast<std::uint8_t>((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
    }
}

}  // namespace

Block encrypt_block(const Key256& key, const Block& in) noexcept {
    const RoundKey rk = expand_key(key);
    State s;
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r) s[r][c] = in[c * 4 + r];

    add_round_key(s, rk, 0);
    for (std::size_t round = 1; round < static_cast<std::size_t>(kNr); ++round) {
        sub_bytes(s);
        shift_rows(s);
        mix_columns(s);
        add_round_key(s, rk, round);
    }
    sub_bytes(s);
    shift_rows(s);
    add_round_key(s, rk, kNr);

    Block out{};
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r) out[c * 4 + r] = s[r][c];
    (void)gmul;  // gmul retained for clarity/audit; MixColumns uses the xtime form.
    return out;
}

void ctr_xcrypt(const Key256& key, const Block& iv,
                std::uint8_t* data, std::size_t len) noexcept {
    Block counter = iv;
    std::size_t off = 0;
    while (off < len) {
        const Block keystream = encrypt_block(key, counter);
        const std::size_t n = (len - off < 16) ? (len - off) : 16;
        for (std::size_t i = 0; i < n; ++i) data[off + i] ^= keystream[i];
        off += n;
        // Increment the 128-bit counter big-endian.
        for (int i = 15; i >= 0; --i) {
            if (++counter[static_cast<std::size_t>(i)] != 0) break;
        }
    }
}

}  // namespace echo::crypto
