// ECHO OS — a generic encrypt-then-MAC composition (Phase 24).
//
// WHY THIS EXISTS. Phase 22's SecureChannel already proved the right composition for a
// framed buffer: encrypt with AES-256-CTR, then MAC the whole thing with AES-256-CMAC, and
// on open, verify the MAC BEFORE decrypting or interpreting a single byte. Phase 24 needs
// the identical composition for the at-rest memory store (memory_engine.cpp's
// seal_image/unseal_image) — which is exactly the situation this file exists to prevent: a
// second hand-written "verify then decrypt" is one more chance for a future call site to
// get the order wrong, which is the entire class of bug this phase exists to close. One
// choke point, reused, removes that chance.
//
// SHAPE. Both known call sites (the caregiver-link frame, and the memory container) already
// lay their buffer out as [authenticated header bytes][ciphertext region] contiguously
// before any crypto runs, so this operates on a caller-owned buffer in place — no extra
// copies, no allocation beyond what the caller already made.
#pragma once

#include "echo/crypto/aes256.hpp"
#include "echo/crypto/cmac.hpp"

#include <cstddef>
#include <vector>

namespace echo::crypto {

// Encrypts buf[plaintext_offset..) in place (AES-256-CTR under key/iv), then returns a CMAC
// over the WHOLE buffer as it now stands — the untouched header bytes preceding
// plaintext_offset, plus the now-encrypted region. The caller appends the returned tag to
// its own framing (it is not written into `buf`).
Mac seal_in_place(const Key256& key, const Block& iv, std::vector<std::uint8_t>& buf,
                   std::size_t plaintext_offset) noexcept;

// Verifies `tag` against CMAC(key, buf) BEFORE decrypting anything — the same order
// SecureChannel::open() already established, so a tampered or wrong-key buffer is rejected
// before its bytes are ever treated as plaintext. On a match, decrypts buf[plaintext_offset..)
// in place and returns true. On ANY mismatch (or an out-of-range offset), `buf` is left
// byte-for-byte untouched and this returns false — the caller's existing buffer/file is
// never corrupted by a failed open.
bool open_in_place(const Key256& key, const Block& iv, const Mac& tag,
                    std::vector<std::uint8_t>& buf, std::size_t plaintext_offset) noexcept;

}  // namespace echo::crypto
