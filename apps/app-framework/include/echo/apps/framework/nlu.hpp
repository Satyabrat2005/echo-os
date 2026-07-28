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
//
// Intent selection is specificity-aware, not merely left-to-right. A word in
// `generic_intents` is a catch-all verb (the browser's "read"/"open") that must
// yield to a more specific intent appearing ANYWHERE in the utterance — otherwise
// "read my unread email" latches onto the leading "read" (browser) and never
// reaches "unread"/"email" (mail). So parse() first looks for the earliest
// non-generic intent, and only falls back to a generic verb when the utterance
// carries nothing more specific. Within a tier, earliest position still wins.
VoiceCommand parse(std::string_view utterance,
                   const std::vector<std::string>& vocabulary,
                   const std::vector<std::string>& generic_intents = {"read", "open"});

// Lowercase + trim helper, exposed because a few call sites want it directly.
std::string normalize(std::string_view text);

}  // namespace echo::apps::nlu
