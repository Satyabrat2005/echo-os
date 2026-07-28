// ECHO OS apps — the per-app process entrypoint helper.
//
// Each app ships as its own executable so the supervisor can run it as an
// isolated OS process (constraint #2). Their main()s are near-identical, so this
// helper carries all of it: parse argv, initialize the app, and serve commands.
//
// Modes (selected by argv):
//   --serve            read Cmd messages from stdin, write Rsp messages to
//                      stdout (the mode the supervisor drives over pipes).
//                      This is the default when no mode flag is given.
//   --once "<phrase>"  handle a single utterance, print a human-readable result,
//                      exit. Handy for poking an app from a shell on the laptop.
//   --selftest         initialize + run one canned command, exit 0 on success.
//                      Used by the supervisor smoke path to prove a spawn works.
//
// Service flags:
//   --mock (default) / --real   which backend the app's factory should use.
#pragma once

#include "echo/apps/framework/app.hpp"

#include <memory>

namespace echo::apps {

// True if `flag` appears anywhere in argv. Apps use this to pick mock vs real
// before constructing themselves (the factory takes a bool).
bool has_flag(int argc, char** argv, const char* flag);

// Value following `flag` in argv, or empty if absent/last.
std::string flag_value(int argc, char** argv, const char* flag);

// Drive an app process to completion. Returns a process exit code.
int run_app(std::unique_ptr<IApp> app, int argc, char** argv);

}  // namespace echo::apps
