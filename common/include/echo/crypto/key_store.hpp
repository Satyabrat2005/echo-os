// ECHO OS — on-device key files.
//
// One mechanism for "a 32-byte secret that lives in a file next to the thing it
// protects, owner-only, generated on first use". Phase 16 wrote it for the memory
// store's device key; Phase 22 needed the same thing for the caregiver pairing key
// and, rather than invent a third key-storage story (the phase forbids exactly
// that), moved the mechanism down here so both callers share it.
//
// Callers: echo::memory::load_or_create_device_key (the store's at-rest key) and
// echo::companion::pairing_key_path_for's consumer (the caregiver channel key).
// They pass different paths; that is the whole difference between them.
//
// HONEST LIMITS — unchanged from Phase 16, and they still apply:
//   * The key is a file. Owner-only permissions are best-effort (Windows ACLs have
//     their own opinions) and stop another USER, not another PROCESS running as the
//     wearer, and certainly not someone who has the unlocked device in their hand.
//   * It is not hardware-backed. There is no secure enclave, no TPM, no
//     key-wrapping, and no attestation that the key was ever generated on this
//     device at all.
//   * Randomness comes from std::random_device, which is the OS CSPRNG on the
//     supported toolchains. That is a reasonable best effort and not a certified
//     entropy source.
// The right fix is platform key storage on real hardware; see the gap table in
// docs/STATE.md.
#pragma once

#include <optional>
#include <string>

#include "echo/crypto/aes256.hpp"

namespace echo::crypto {

// Load the 32-byte key at `path`, creating it (random, owner-only) if absent.
//
// Returns nullopt only when a key can neither be read nor created — an I/O failure.
// Callers must treat that as "refuse to proceed", never as "carry on unprotected":
// falling back to plaintext on a key error is how encryption-at-rest quietly stops
// happening. `tag` is the log subsystem name so failures are attributable.
std::optional<Key256> load_or_create_key(const std::string& path, const char* tag);

}  // namespace echo::crypto
