// ECHO OS — AES-256-CMAC (NIST SP 800-38B).
//
// A message authentication code built on the AES block cipher we already have.
//
// WHY THIS EXISTS (Phase 22). ADR-14 was explicit that the at-rest cipher is
// AES-CTR with NO authentication: a keystream XOR is malleable, and an attacker
// who can flip ciphertext bits flips the corresponding plaintext bits. For the
// database that was an accepted trade — the threat model there is a lost device,
// the attacker has the file but not the key, and a corrupt image is caught by the
// "decrypted bytes must start with the SQLite magic" check.
//
// The caregiver link is a different threat model entirely. It is the device's
// first INBOUND path: bytes arrive from a peer we do not control, over a medium
// we do not control, and they are asked to change what the wearer is told. An
// unauthenticated cipher there is indefensible — a tampered frame would decrypt
// to attacker-chosen garbage that the parser would then dutifully process. So the
// transport is encrypt-then-MAC: CTR for confidentiality, CMAC over the framed
// ciphertext for integrity, verified BEFORE a single byte reaches the decoder.
//
// WHY CMAC RATHER THAN HMAC. CMAC needs only the AES block function that Phase 16
// already implemented and verified against FIPS-197; HMAC would mean writing and
// KAT-ing a SHA-256 as well. Same precedent as ADR-14: implement the one primitive
// actually needed, and prove it against published vectors rather than trusting it.
//
// WHAT THIS DOES AND DOESN'T GIVE YOU. CMAC proves a message was produced by
// someone holding the shared key and has not been altered. It is symmetric: both
// endpoints can forge each other's messages, so it authenticates the CHANNEL, not
// a person. It says nothing about freshness on its own — replay is handled a layer
// up, by the monotonic sequence number in the frame header. And a shared key that
// lives in a file on the device is only as good as the device: this is not, and
// does not claim to be, hardware-backed attestation. See ADR-19.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "echo/crypto/aes256.hpp"

namespace echo::crypto {

using Mac = std::array<std::uint8_t, 16>;  // full 128-bit CMAC tag, never truncated

// CMAC of `len` bytes at `data` under `key`. Handles the empty message (the
// standard's Mlen=0 case) and any length, padded or not, per SP 800-38B.
Mac cmac(const Key256& key, const std::uint8_t* data, std::size_t len) noexcept;

// Constant-time tag comparison. Always compares all 16 bytes: a MAC check that
// returns early on the first mismatched byte leaks, through timing, how much of a
// forged tag was correct — which is enough to construct one byte at a time.
bool mac_equal(const Mac& a, const Mac& b) noexcept;

}  // namespace echo::crypto
