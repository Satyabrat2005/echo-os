// ECHO OS — per-device memory key management. See device_key.hpp for the honest
// account of what security property the key file does and does not provide.
#include "echo/memory/device_key.hpp"

#include "echo/log.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace echo::memory {

namespace {

// Fill `key` with cryptographically-intended randomness. std::random_device is
// backed by the OS CSPRNG on this toolchain (RDRAND / getrandom); we draw the full
// 256 bits from it. This is an honest best-effort local CSPRNG, not a HSM.
void fill_random(crypto::Key256& key) {
    std::random_device rd;
    std::uniform_int_distribution<int> byte(0, 255);
    for (auto& b : key) b = static_cast<std::uint8_t>(byte(rd));
}

// Owner-only permissions on the key file — identical intent to the memory store's own
// restriction and to appkit's token files. Best-effort (Windows ACLs still apply).
void restrict_permissions(const std::string& path) {
    std::error_code ec;
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) log_warn("memory", "could not restrict key file permissions");
}

}  // namespace

std::string key_path_for(const std::string& db_path) {
    if (db_path == ":memory:" || db_path.empty()) return {};
    if (const char* env = std::getenv("ECHO_MEMORY_KEY"); env && *env) return env;
    return db_path + ".key";
}

std::optional<crypto::Key256> load_or_create_device_key(const std::string& key_path) {
    if (key_path.empty()) return std::nullopt;

    // Try to read an existing key.
    {
        std::ifstream in(key_path, std::ios::binary);
        if (in) {
            crypto::Key256 key{};
            in.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
            if (in && in.gcount() == static_cast<std::streamsize>(key.size())) {
                return key;
            }
            log_warn("memory", "device key file present but unreadable/short; regenerating");
        }
    }

    // First boot (or unreadable): generate and persist a fresh key.
    crypto::Key256 key{};
    fill_random(key);

    std::error_code ec;
    const std::filesystem::path p(key_path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream out(key_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log_error("memory", "could not create device key file; refusing plaintext fallback");
        return std::nullopt;
    }
    out.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
    if (!out) {
        log_error("memory", "could not write device key file");
        return std::nullopt;
    }
    out.close();
    restrict_permissions(key_path);
    log_info("memory", "generated new per-device memory key (first boot)");
    return key;
}

}  // namespace echo::memory
