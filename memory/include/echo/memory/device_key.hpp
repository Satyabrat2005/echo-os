// ECHO OS — per-device memory encryption key (Phase 16).
//
// HONEST SECURITY PROPERTY. This wearable has no keyboard and, today, no companion-
// device pairing flow, so a user passphrase prompt is not realistic yet. The store's
// AES-256 key is therefore a random per-device key, generated once at first boot and
// kept in a local key file with owner-only permissions — the same "least-bad local
// option" already used for OAuth secrets in appkit's `.echo-tokens/` (token_store).
//
// What this DOES provide: the memory database is unreadable to anyone who obtains the
// .db file WITHOUT also obtaining the key file — e.g. a copied/backed-up/exfiltrated
// database, a pulled flash chip imaged offline, or a lost device whose storage is
// read on another machine but whose key file's permissions are honored.
//
// What this does NOT provide, said plainly: it is NOT hardware-backed and NOT a
// secure-enclave key. An attacker with full read access to the running device's
// filesystem (root, or a process running as the wearer) can read both the .db and
// the key file and thus decrypt. Binding the key to a TPM/SE or a paired phone is
// the honest follow-up (ADR-14) once such hardware/flows exist on the platform.
#pragma once

#include "echo/memory/aes256.hpp"

#include <optional>
#include <string>

namespace echo::memory {

// Resolve the key-file path for a given store path. The key lives beside the store as
// "<db_path>.key" unless ECHO_MEMORY_KEY overrides it. A ":memory:" store has no file
// and no key (returns "").
std::string key_path_for(const std::string& db_path);

// Load the device key from `key_path`, creating it (32 random bytes, owner-only
// permissions) on first use if absent. Returns nullopt only if a key can neither be
// read nor created (I/O failure) — in which case the caller must refuse to open an
// encrypted store rather than fall back to plaintext.
std::optional<crypto::Key256> load_or_create_device_key(const std::string& key_path);

}  // namespace echo::memory
