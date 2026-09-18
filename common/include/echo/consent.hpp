// ECHO OS — consent vocabulary (Phase 22).
//
// Consent is the hinge the caregiver boundary turns on, and it is named by two
// modules that must not know about each other: the memory engine PERSISTS it (it
// is a record like any other, and it has to survive a reboot), and companion-sync
// ENFORCES it (it is the thing that decides whether a digest may be built at all).
// companion-sync must never link the store — that is the whole Phase 10/15
// guarantee — so the shared vocabulary lives here in common, exactly like
// RuntimeState and Status do.
//
// Note what a scope is NOT: it is not a login, not an identity, and not a
// capability token. It is the wearer's answer to "may a caregiver see counts, and
// may they send me things?", recorded on the device, revocable at any moment.
#pragma once

#include <cstdint>

namespace echo {

// What a paired caregiver is permitted to receive or do. Ordered by increasing
// exposure so a check can read "scope >= X", but the enforcement code deliberately
// compares exactly rather than relying on the ordering.
enum class ConsentScope : std::uint8_t {
    None = 0,               // the default. No digest, no inbound commands.
    Digest = 1,             // counts and states may be sent. Nothing inbound.
    DigestAndCommands = 2,  // as above, plus validated inbound commands.
};

// Who granted it. This is recorded because the two are not equivalent and a later
// reviewer needs to be able to tell them apart: a wearer with memory loss granting
// consent to their own device is a different act from a caregiver granting it
// during a supervised pairing session, which is the arrangement that exists
// precisely because the wearer may not be able to work the pairing flow alone.
enum class ConsentGrantor : std::uint8_t {
    Wearer = 0,
    CaregiverSupervised = 1,
};

const char* to_string(ConsentScope scope) noexcept;
const char* to_string(ConsentGrantor grantor) noexcept;

// True when the scope permits sending a digest at all.
inline bool permits_digest(ConsentScope scope) noexcept {
    return scope == ConsentScope::Digest || scope == ConsentScope::DigestAndCommands;
}

// True when the scope permits acting on anything a caregiver sends IN. Strictly
// narrower than permits_digest(): reading counts is not the same permission as
// writing to the wearer's device, and granting the first must never imply the
// second.
inline bool permits_commands(ConsentScope scope) noexcept {
    return scope == ConsentScope::DigestAndCommands;
}

}  // namespace echo
