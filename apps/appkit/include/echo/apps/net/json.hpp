// ECHO OS appkit — JSON reader (alias).
//
// The parser this header used to define MOVED to echo/json.hpp (echo::common) in
// Phase 22: companion-sync needed the same hardened decoder for the caregiver
// inbound path, and apps/ builds after — and optionally without — the core
// modules, so a core module cannot link echo::appkit. Growing a second parser
// would have been exactly the parallel mechanism Phase 22 forbids.
//
// Nothing about the type changed: same depth cap, same "malformed input yields
// Null, never throws" contract, same tests, same fuzz harness. This alias keeps
// every Phase 5-8 call site (echo::apps::net::Json) compiling unchanged.
#pragma once

#include "echo/json.hpp"

namespace echo::apps::net {
using Json = ::echo::Json;
}  // namespace echo::apps::net
