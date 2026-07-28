// ECHO OS apps — the tiniest possible intent parser.
//
// On the device, the core's ASR + on-device LLM produce a structured intent.
// Here on the laptop we substitute a keyword parser so the whole voice flow is
// exercisable by typing (or piping) plain sentences. It lowercases the utterance,
// finds the first word the router knows as an intent, and packs the remainder as
// a "query" slot. Deliberately dumb — the real NLU lives in cognitive-core and
// this is only the test harness's stand-in.
#pragma once

#include "echo/apps/framework/app.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace echo::apps::nlu {

// Parse a raw utterance into a VoiceCommand, choosing the intent from the given
// vocabulary (the union of every registered app's declared intents). If no known
// verb appears, intent is left empty and the router treats it as unhandled.
VoiceCommand parse(std::string_view utterance, const std::vector<std::string>& vocabulary);

// Lowercase + trim helper, exposed because a few call sites want it directly.
std::string normalize(std::string_view text);

}  // namespace echo::apps::nlu
