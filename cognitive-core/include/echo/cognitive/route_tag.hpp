// ECHO OS cognitive-core — LLM route-tag parsing.
//
// The reasoning model may prefix its reply with a "[route:<app>]" tag so the
// runtime can hand app-shaped requests to the apps layer (see the system prompt
// in llm.cpp). Parsing that tag is pure string logic with no llama.cpp
// dependency, so it lives here — always compiled, and unit-testable in the
// dependency-free stub build (see tests/test_main.cpp), even though it is only
// *exercised* on the real ECHO_WITH_LLAMA path.
#pragma once

#include <string>

namespace echo::cognitive {

// Pull a leading "[route:xxx]" tag out of the model's text: returns the intent
// (lowercased, whitespace-trimmed — real models are less tidy than a stub) and
// erases the tag from `text`, leaving the spoken reply. Returns "" when there is
// no well-formed tag, in which case `text` is unchanged.
std::string parse_route_tag(std::string& text);

}  // namespace echo::cognitive
