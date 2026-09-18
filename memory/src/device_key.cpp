// ECHO OS — per-device memory key management. See device_key.hpp for the honest
// account of what security property the key file does and does not provide.
//
// The generate/load/permissions mechanism moved to echo::crypto::load_or_create_key
// (echo/crypto/key_store.hpp) in Phase 22 so the caregiver pairing key could reuse
// it rather than grow a second one. What stays here is the part that is genuinely
// about the memory store: where its key file lives.
#include "echo/memory/device_key.hpp"

#include "echo/crypto/key_store.hpp"
#include "echo/crypto/install_binding.hpp"

#include <cstdlib>

namespace echo::memory {

std::string key_path_for(const std::string& db_path) {
    if (db_path == ":memory:" || db_path.empty()) return {};
    if (const char* env = std::getenv("ECHO_MEMORY_KEY"); env && *env) return env;
    return db_path + ".key";
}

std::optional<crypto::Key256> load_or_create_device_key(const std::string& key_path) {
    auto key = ::echo::crypto::load_or_create_key(key_path, "memory");
    if (!key) return key;

    // Phase 24, opt-in and OFF BY DEFAULT: mix a per-install OS fingerprint into the key.
    // See install_binding.hpp for why this defaults off — fingerprint drift (an OS
    // reinstall regenerating the GUID) permanently bricks the store with no attacker
    // required, so it needs an operator's explicit opt-in, the same convention
    // ECHO_MEMORY_KEY/ECHO_PAIRING_KEY already use. bind_key() itself no-ops on an empty
    // fingerprint, so an unsupported platform degrades to today's unbound behaviour
    // rather than failing.
    if (const char* bind = std::getenv("ECHO_MEMORY_KEY_BIND"); bind && *bind && *bind != '0') {
        *key = ::echo::crypto::bind_key(*key, ::echo::crypto::install_fingerprint());
    }
    return key;
}

}  // namespace echo::memory
