// ECHO OS — minimal, allocation-light logging.
//
// The real device routes this to a ring-buffered journal (no blocking I/O on the
// audio thread). For the scaffold it writes to stderr. Keep call sites terse.
#pragma once

#include <cstdint>
#include <string_view>

namespace echo {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error };

// Set the minimum level that will be emitted. Defaults to Info.
void set_log_level(LogLevel level) noexcept;

void log(LogLevel level, std::string_view module, std::string_view message) noexcept;

// Convenience wrappers.
inline void log_info (std::string_view m, std::string_view msg) noexcept { log(LogLevel::Info,  m, msg); }
inline void log_warn (std::string_view m, std::string_view msg) noexcept { log(LogLevel::Warn,  m, msg); }
inline void log_error(std::string_view m, std::string_view msg) noexcept { log(LogLevel::Error, m, msg); }
inline void log_debug(std::string_view m, std::string_view msg) noexcept { log(LogLevel::Debug, m, msg); }

}  // namespace echo
