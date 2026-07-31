// ECHO OS tests — a small, honest, cross-platform process-resource probe (Phase 20).
//
// The longevity soak needs to answer two blunt questions across a long run:
//   * is process memory (RSS) plateauing, or creeping upward tick after tick?
//   * is the open file-descriptor / handle count staying flat, or leaking one per
//     watchdog-recovery / save-to-disk cycle?
//
// This is a COARSE leak detector, and deliberately so — it samples the OS's own
// accounting rather than instrumenting the allocator. That is enough to catch the
// class of bug Phase 20 is about (unbounded growth over hours), and it is honest
// about the platforms where it can't measure: on anything but Linux/Windows the
// samplers return kUnavailable and the caller skips those assertions rather than
// pretend a number it doesn't have.
//
//   * Linux  — RSS from /proc/self/statm (resident pages x page size); open fds
//              from the entry count of /proc/self/fd. This is the precise path CI
//              (ubuntu-latest) exercises.
//   * Windows — WorkingSetSize via GetProcessMemoryInfo; GetProcessHandleCount for
//              the (much noisier, all-kinds) handle total. Lets the harness produce
//              real local numbers on the MinGW dev box too.
#pragma once

#include <cstdint>

#if defined(__linux__)
#  include <cstdio>
#  include <dirent.h>
#  include <unistd.h>
#elif defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#endif

namespace echo::test {

// Sentinel returned when the current platform can't supply a metric. Callers treat a
// negative sample as "unavailable — skip this assertion", never as a real value.
inline constexpr std::int64_t kUnavailable = -1;

// Resident set size in kibibytes, or kUnavailable.
inline std::int64_t rss_kib() noexcept {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return kUnavailable;
    long pages_total = 0, pages_resident = 0;
    const int got = std::fscanf(f, "%ld %ld", &pages_total, &pages_resident);
    std::fclose(f);
    if (got != 2 || pages_resident < 0) return kUnavailable;
    const long page_kib = sysconf(_SC_PAGESIZE) / 1024;
    return static_cast<std::int64_t>(pages_resident) * page_kib;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return kUnavailable;
    return static_cast<std::int64_t>(pmc.WorkingSetSize / 1024);
#else
    return kUnavailable;
#endif
}

// Count of open file descriptors (Linux) / process handles (Windows), or kUnavailable.
// On Linux this is a tight proxy for the memory engine's SQLite file handles and any
// leaked temp-file descriptor from save-to-disk; on Windows it is the (noisier) total
// handle count, still a valid "does it grow per cycle?" signal.
inline std::int64_t open_handle_count() noexcept {
#if defined(__linux__)
    DIR* d = ::opendir("/proc/self/fd");
    if (!d) return kUnavailable;
    std::int64_t n = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;  // skip "." and ".."
        ++n;
    }
    ::closedir(d);
    // Subtract the descriptor opendir itself is holding open right now, so the count
    // reflects the process's own fds, not our measurement.
    return n > 0 ? n - 1 : n;
#elif defined(_WIN32)
    DWORD handles = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &handles)) return kUnavailable;
    return static_cast<std::int64_t>(handles);
#else
    return kUnavailable;
#endif
}

inline bool resource_metrics_available() noexcept {
    return rss_kib() != kUnavailable && open_handle_count() != kUnavailable;
}

}  // namespace echo::test
