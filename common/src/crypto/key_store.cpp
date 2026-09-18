// ECHO OS — on-device key files. See key_store.hpp for the honest account of what
// security property a key file does and does not provide.
//
// This is the Phase 16 device-key implementation, moved down from memory/ in Phase
// 22 so the caregiver pairing key could reuse it instead of growing a parallel one.
// The only change is that the log tag is now a parameter.
#include "echo/crypto/key_store.hpp"

#include "echo/log.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace echo::crypto {

namespace {

// Fill `key` with cryptographically-intended randomness. std::random_device is
// backed by the OS CSPRNG on this toolchain (RDRAND / getrandom); we draw the full
// 256 bits from it. This is an honest best-effort local CSPRNG, not a HSM.
void fill_random(Key256& key) {
    std::random_device rd;
    std::uniform_int_distribution<int> byte(0, 255);
    for (auto& b : key) b = static_cast<std::uint8_t>(byte(rd));
}

// Owner-only permissions on the key file — identical intent to the memory store's own
// restriction and to appkit's token files. Best-effort (Windows ACLs still apply).
void restrict_permissions(const std::string& path, const char* tag) {
    std::error_code ec;
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) log_warn(tag, "could not restrict key file permissions");
}

}  // namespace

std::optional<Key256> load_or_create_key(const std::string& path, const char* tag) {
    if (path.empty()) return std::nullopt;

    // Try to read an existing key.
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            Key256 key{};
            in.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
            if (in && in.gcount() == static_cast<std::streamsize>(key.size())) {
                return key;
            }
            log_warn(tag, "key file present but unreadable/short; regenerating");
        }
    }

    // First boot (or unreadable): generate and persist a fresh key.
    Key256 key{};
    fill_random(key);

    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log_error(tag, "could not create key file; refusing unprotected fallback");
        return std::nullopt;
    }
    out.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
    if (!out) {
        log_error(tag, "could not write key file");
        return std::nullopt;
    }
    out.close();
    restrict_permissions(path, tag);
    log_info(tag, "generated new key (first use)");
    return key;
}

}  // namespace echo::crypto
