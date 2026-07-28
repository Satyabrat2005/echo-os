#include "echo/log.hpp"

#include <atomic>
#include <cstdio>

namespace echo {
namespace {

std::atomic<LogLevel> g_min_level{LogLevel::Info};

const char* level_tag(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

}  // namespace

void set_log_level(LogLevel level) noexcept {
    g_min_level.store(level, std::memory_order_relaxed);
}

void log(LogLevel level, std::string_view module, std::string_view message) noexcept {
    if (static_cast<int>(level) < static_cast<int>(g_min_level.load(std::memory_order_relaxed))) {
        return;
    }
    // On device this is a lock-free journal write; here, stderr is fine.
    std::fprintf(stderr, "[echo][%s][%.*s] %.*s\n",
                 level_tag(level),
                 static_cast<int>(module.size()), module.data(),
                 static_cast<int>(message.size()), message.data());
}

}  // namespace echo
