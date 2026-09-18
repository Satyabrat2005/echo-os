// ECHO OS — per-install key binding (Phase 24, opt-in).
//
// WHY THIS EXISTS. Phase 16's device key is a random file next to the store it protects —
// honest about not being hardware-backed (see key_store.hpp), but also fully portable: copy
// the `.db` and `.key` files anywhere and they decrypt. There is no TPM/secure-enclave on
// this dev laptop, or on the not-yet-existing glasses, to bind to for real (permanent-
// until-hardware, same as STATE.md's other sensor gaps). The only improvement available in
// software is mixing in an OS-level per-install identifier, which at least stops a
// *casual* copy of the two files from decrypting on a different machine.
#pragma once

#include "echo/crypto/aes256.hpp"

#include <string>

namespace echo::crypto {

// An OS-level install identifier: Windows' MachineGuid
// (HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid) or Linux's /etc/machine-id (falling
// back to /var/lib/dbus/machine-id). Returns an empty string if unavailable for any reason
// (unsupported platform, registry/file read failure) — callers MUST treat empty as
// "binding degraded", never as an error: bind_key() below already no-ops on an empty
// fingerprint, so a missing OS identifier just means the key stays unbound, exactly
// today's behaviour, not a failure to boot.
std::string install_fingerprint();

// Mixes `fingerprint` into `raw_key` via a small counter-mode construction built on the
// already-KAT-proven CMAC (see cmac.hpp) — inspired by SP 800-108's counter-mode idea, but
// NOT a byte-exact, certified implementation of that standard: there is no published KAT
// vector for this simplified construction to honestly pin against, so it makes no
// compliance claim, only a confidentiality-composition one (CMAC's own proof still holds
// for each block).
//
// If `fingerprint` is empty, returns `raw_key` unchanged — the no-op path, so callers can
// unconditionally route the loaded key through this function regardless of whether binding
// is enabled or the fingerprint could be read.
//
// CRITICAL, read before enabling: if `fingerprint` ever changes (an OS reinstall/sysprep
// regenerating the GUID, for example), a store bound with the OLD fingerprint becomes
// PERMANENTLY undecryptable — even with the original key file intact, and with no attacker
// required at all. This is why callers gate this behind an explicit opt-in
// ($ECHO_MEMORY_KEY_BIND, see device_key.cpp) rather than enabling it by default. See
// ADR-21 for the full trade-off and what this does and doesn't protect against.
Key256 bind_key(const Key256& raw_key, const std::string& fingerprint) noexcept;

}  // namespace echo::crypto
