#include "echo/crypto/install_binding.hpp"

#include "echo/crypto/cmac.hpp"
#include "echo/log.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace echo::crypto {

namespace {

#if defined(_WIN32)

// Windows: HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid — a GUID generated once at OS
// install and stable across reboots/reimages of the SAME install. Read-only; this process
// never writes it.
std::string read_windows_machine_guid() {
    char buf[64] = {0};
    DWORD size = sizeof(buf);
    LSTATUS st = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography",
                               "MachineGuid", RRF_RT_REG_SZ, nullptr, buf, &size);
    if (st != ERROR_SUCCESS) return {};
    // size includes the trailing NUL on success; trim it if present.
    std::string s(buf, (size > 0 && buf[size - 1] == '\0') ? size - 1 : size);
    return s;
}

#else

// Linux: /etc/machine-id (systemd/dbus), falling back to /var/lib/dbus/machine-id on older
// systems that only populate the legacy path. Both are a single line of hex, no newline
// trimming needed beyond what getline already does.
std::string read_file_first_line(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    std::getline(f, line);
    return line;
}

std::string read_linux_machine_id() {
    std::string id = read_file_first_line("/etc/machine-id");
    if (!id.empty()) return id;
    return read_file_first_line("/var/lib/dbus/machine-id");
}

#endif

}  // namespace

std::string install_fingerprint() {
#if defined(_WIN32)
    std::string id = read_windows_machine_guid();
#else
    std::string id = read_linux_machine_id();
#endif
    if (id.empty()) {
        log_warn("crypto",
                 "install fingerprint unavailable; key binding (if requested) stays a no-op");
    }
    return id;
}

Key256 bind_key(const Key256& raw_key, const std::string& fingerprint) noexcept {
    if (fingerprint.empty()) return raw_key;  // no-op: binding degraded, not broken

    // Two-block counter-mode construction using the already-KAT-proven CMAC as the PRF —
    // see install_binding.hpp for why this is not claimed as a certified SP 800-108
    // implementation, only a confidentiality composition over an already-proven primitive.
    std::vector<std::uint8_t> input;
    input.reserve(1 + fingerprint.size());
    input.push_back(0x01);
    input.insert(input.end(), fingerprint.begin(), fingerprint.end());
    const Mac block1 = cmac(raw_key, input.data(), input.size());

    input[0] = 0x02;
    const Mac block2 = cmac(raw_key, input.data(), input.size());

    Key256 out{};
    std::copy(block1.begin(), block1.end(), out.begin());
    std::copy(block2.begin(), block2.end(), out.begin() + block1.size());
    return out;
}

}  // namespace echo::crypto
