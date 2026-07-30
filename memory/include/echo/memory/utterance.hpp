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

}  // namespace echo::memory
