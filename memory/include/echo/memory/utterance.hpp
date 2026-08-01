// ECHO OS memory — interpreting the two utterances the memory engine cares about.
//
// Pure string logic with no SQLite dependency (mirroring how route-tag parsing is
// pure string logic in cognitive-core), so it is always compiled and unit-testable
// in the dependency-free stub build. cognitive-core calls these to decide whether a
// transcript is naming a person or asking who someone is; the memory store itself
// never sees a raw transcript.
#pragma once

#include <optional>
#include <string>

namespace echo::memory {

// The wearer naming someone: "this is my daughter Priya" -> {name:"Priya",
// relation:"your daughter"}. Heuristic and intentionally conservative — it fires
// only on a clear "this is ..." lead-in, so ordinary speech never creates a person.
struct Naming {
    std::string name;      // display name, first letter upper-cased ("Priya")
    std::string relation;  // caregiver-safe phrase ("your daughter"), or "" if none
};

// Returns the parsed naming when `transcript` clearly names a person, else nullopt.
std::optional<Naming> parse_naming(const std::string& transcript);

// True when the transcript is asking who the person in view is ("who is this",
// "who's that", "do you know them"). Used to gate proactive face recall so ECHO
// doesn't answer "that's Priya" to an unrelated question.
bool is_identity_query(const std::string& transcript);

// True when the transcript asks about the WEARER'S OWN life or history — "did I
// take my tablets?", "when did I last see Priya?", "who visited yesterday?",
// "where did I put my keys?".
//
// WHY THIS EXISTS (Phase 21). These are the questions ECHO is worn to answer, and
// they are also the questions where a wrong answer does the most damage: the one
// person who could contradict it is the person who cannot remember. There is no
// confidence score that makes a generated answer to one of these safe, so the
// cognitive core does not use one — a hit here routes to the store, and if the
// store has nothing the core abstains instead of consulting the LLM at all.
//
// Deliberately conservative in the direction of MISSING a question rather than
// catching an innocent one. A miss costs a normal LLM answer to something like
// "what's the weather"; a false positive costs an abstention on a question ECHO
// could have answered. Both are recoverable; asserting an invented memory is not.
//
// Identity queries ("who is this") are excluded — they have their own pre-gate
// face-recall path in cognitive-core, and the two classifiers stay disjoint so it
// is always clear which one is responsible for a given turn.
bool is_self_referential_query(const std::string& transcript);

}  // namespace echo::memory
