// AES-256-CMAC — NIST SP 800-38B, on top of the Phase 16 AES block function.
//
// The whole algorithm is: derive two subkeys K1/K2 from CIPH_K(0^128) by doubling
// in GF(2^128); CBC-MAC the message; XOR the final block with K1 (message is a
// whole number of complete blocks) or with K2 after 10*-padding (it isn't).
//
// Verified against the SP 800-38B AES-256 vectors in tests/crypto_test.cpp,
// including the subkeys themselves — a CMAC that agrees with the spec on the
// final tag but not on K1/K2 is a coincidence worth catching.
#include "echo/crypto/cmac.hpp"

#include <cstring>

namespace echo::crypto {

namespace {

// Rb for a 128-bit block: the low byte of the reduction polynomial
// x^128 + x^7 + x^2 + x + 1.
constexpr std::uint8_t kRb = 0x87;

// Left-shift a block by one bit, and reduce mod the field polynomial if a 1 was
// shifted off the top. Written branch-free on the data so subkey derivation
// doesn't depend on key bits at the instruction level.
Block dbl(const Block& in) noexcept {
    Block out{};
    std::uint8_t carry = 0;
    for (int i = 15; i >= 0; --i) {
        const std::uint8_t next_carry = static_cast<std::uint8_t>(in[static_cast<std::size_t>(i)] >> 7);
        out[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((in[static_cast<std::size_t>(i)] << 1) | carry);
        carry = next_carry;
    }
    // If the top bit was set, XOR in Rb. Masked rather than branched.
    out[15] = static_cast<std::uint8_t>(out[15] ^ (kRb & static_cast<std::uint8_t>(0u - carry)));
    return out;
}

void xor_into(Block& dst, const Block& src) noexcept {
    for (std::size_t i = 0; i < dst.size(); ++i) dst[i] ^= src[i];
}

}  // namespace

Mac cmac(const Key256& key, const std::uint8_t* data, std::size_t len) noexcept {
    // Subkey generation (SP 800-38B §6.1).
    const Block zero{};
    const Block l  = encrypt_block(key, zero);
    const Block k1 = dbl(l);
    const Block k2 = dbl(k1);

    // Number of blocks: ceil(len/16), with the empty message treated as one
    // (padded) block rather than zero.
    const std::size_t n = (len == 0) ? 1 : (len + 15) / 16;
    const bool complete = (len != 0) && (len % 16 == 0);

    Block x{};  // running CBC-MAC state, starts at 0^128
    for (std::size_t i = 0; i + 1 < n; ++i) {
        Block m{};
        std::memcpy(m.data(), data + i * 16, 16);
        xor_into(x, m);
        x = encrypt_block(key, x);
    }

    // Final block: either the last full block XOR K1, or the 10*-padded remainder
    // XOR K2.
    Block last{};
    if (complete) {
        std::memcpy(last.data(), data + (n - 1) * 16, 16);
        xor_into(last, k1);
    } else {
        const std::size_t rem = len - (n - 1) * 16;  // 0..15
        if (rem > 0) std::memcpy(last.data(), data + (n - 1) * 16, rem);
        last[rem] = 0x80;  // the '1' bit; the rest of the block is already zero
        xor_into(last, k2);
    }

    xor_into(x, last);
    const Block t = encrypt_block(key, x);

    Mac mac{};
    std::memcpy(mac.data(), t.data(), mac.size());
    return mac;
}

bool mac_equal(const Mac& a, const Mac& b) noexcept {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

}  // namespace echo::crypto
