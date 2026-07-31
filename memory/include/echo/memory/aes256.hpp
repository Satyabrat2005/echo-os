// ECHO OS — a small, self-contained AES-256 in CTR mode (Phase 16).
//
// WHY THIS EXISTS (honest framing). Phase 16 encrypts the memory store at rest.
// The "textbook" choice is SQLCipher — but SQLCipher is not a self-contained
// amalgamation the way the vendored SQLite is: it must be generated from a source
// tree AND linked against a crypto backend (OpenSSL libcrypto on this platform),
// which is *not present* in the project's MinGW toolchain and would break the
// dependency-free, vendorable stub build (constraint #3/#4). Rather than drag
// OpenSSL in, we implement the one primitive we actually need — AES-256 — in a few
// hundred lines of dependency-free code, and we PROVE it correct against the
// published FIPS-197 and NIST SP 800-38A known-answer vectors in the test suite
// (nothing here is trusted on faith).
//
// SCOPE, stated plainly: this provides CONFIDENTIALITY at rest (the on-disk image
// is AES-256-CTR ciphertext, unreadable without the key). It is NOT an
// authenticated cipher — there is no MAC — so it detects a wrong key / corruption
// only by the sanity check that a correctly-decrypted image must begin with the
// SQLite magic, not by cryptographic authentication. See ADR-14.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace echo::memory::crypto {

using Key256 = std::array<std::uint8_t, 32>;  // AES-256 key
using Block  = std::array<std::uint8_t, 16>;  // AES block / CTR counter (IV)

// Encrypt a single 16-byte block in place-agnostic form (ECB core). Exposed only so
// the KAT test can check it against the FIPS-197 AES-256 example vector; callers use
// ctr_xcrypt.
Block encrypt_block(const Key256& key, const Block& in) noexcept;

// AES-256-CTR over `data` in place, starting from counter block `iv` (incremented as
// a big-endian 128-bit integer across the whole block, per NIST SP 800-38A). CTR is
// symmetric: the same call both encrypts and decrypts. `data` may be any length
// (the final partial block is handled).
void ctr_xcrypt(const Key256& key, const Block& iv,
                std::uint8_t* data, std::size_t len) noexcept;

}  // namespace echo::memory::crypto
