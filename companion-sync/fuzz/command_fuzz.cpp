// Fuzz harness for the caregiver inbound command decoder
// (echo::companion::decode_command).
//
// This is the second-most-exposed decoder in the system and, unlike the appkit JSON
// reader, it sits on a path that can CHANGE WHAT THE DEVICE DOES: an accepted command
// becomes a reminder spoken aloud to a person with memory loss, or a name written into
// their store. The appkit parser only ever produced text for a screen; this one
// produces actions. That difference is the whole reason it gets its own harness rather
// than relying on the JSON fuzzer one layer down.
//
// The bytes reaching decode_command have already survived the secure channel (MAC,
// direction, replay). Fuzzing here therefore models the strongest realistic attacker:
// one who HOLDS THE PAIRING KEY — a compromised or malicious caregiver phone — and can
// put arbitrary plaintext in front of the decoder. Everything below the MAC is
// deliberately out of scope; that boundary is proven separately in
// tests/caregiver_link_test.cpp by flipping every bit of every frame.
//
// Contract asserted here — the same one the module documents:
//   * decode_command always terminates and never throws, whatever the bytes are.
//     (It parses on the Phase 6 hardened parser, whose depth cap exists because of a
//     real stack-overflow bug; deeply-nested input must degrade, not crash.)
//   * A refusal carries a reason and NO command. Nothing half-decoded escapes.
//   * An acceptance satisfies every field invariant the rest of the system relies on.
//     This is the important half: a crash is a bug, but a malformed command that the
//     decoder *accepts* is a bug that reaches the wearer, and only an assertion here
//     can catch it. libFuzzer reports these via the abort() inside assert.
//
// Any input that trips ASan/UBSan (the CI fuzz job builds with both) or hangs is a
// finding. See apps/appkit/fuzz/README.md for how to run this locally.
#include "echo/companion/inbound.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // Raw bytes, embedded NULs and all — exactly what comes out of the cipher. A
    // std::string built from (ptr, len) preserves them; strlen-based construction
    // would silently truncate and fuzz a shorter input than intended.
    const std::string payload(reinterpret_cast<const char*>(data), size);

    // Split the first byte off as the clock/sequence state so the fuzzer can also
    // explore the freshness checks (seq <= last_seq) and the due-time range window,
    // instead of hammering one fixed "now" forever. Derived from the input so the
    // harness stays deterministic and a crashing case replays exactly.
    const std::int64_t  now      = 1700000000 + static_cast<std::int64_t>(size) * 37;
    const std::uint64_t last_seq = size ? static_cast<std::uint64_t>(data[0]) : 0;

    const echo::companion::DecodeResult r =
        echo::companion::decode_command(payload, now, last_seq);

    if (!r.ok) {
        // A refusal must name itself. An unnamed refusal means a path returned false
        // without setting a reason, which is how a "rejected" command quietly becomes
        // an accepted one after a later refactor.
        assert(r.reason != echo::companion::CommandReject::None);
        (void)echo::companion::to_string(r.reason);
        return 0;
    }

    // --- Accepted. Now assert everything downstream is entitled to assume. --------
    const echo::companion::InboundCommand& c = r.command;

    // Freshness: acceptance must strictly advance the sequence, or replay protection
    // is not protection.
    assert(c.seq > last_seq);

    // The command must be one of the allowlisted kinds — never a default-constructed
    // or out-of-range value smuggled through by a numeric field.
    assert(c.kind == echo::companion::CommandKind::AddReminder ||
           c.kind == echo::companion::CommandKind::EnrolName ||
           c.kind == echo::companion::CommandKind::RequestDigest);

    // Text fields: bounded, and free of the control characters that the Gmail
    // header-injection lesson put on this list. Anything accepted here gets spoken to
    // the wearer or written to their store, so "it parsed" is not a low enough bar.
    assert(c.text.size() <= echo::companion::kMaxReminderChars);
    assert(c.name.size() <= echo::companion::kMaxNameChars);
    assert(c.relation.size() <= echo::companion::kMaxRelationChars);
    for (const char ch : c.text) assert(static_cast<unsigned char>(ch) >= 0x20);
    for (const char ch : c.name) assert(static_cast<unsigned char>(ch) >= 0x20);
    for (const char ch : c.relation) assert(static_cast<unsigned char>(ch) >= 0x20);

    // Per-kind requirements: a reminder without text or with a nonsense due time, or
    // an enrolment without a name, must never reach the runtime.
    if (c.kind == echo::companion::CommandKind::AddReminder) {
        assert(!c.text.empty());
        assert(c.due > 0);
    }
    if (c.kind == echo::companion::CommandKind::EnrolName) assert(!c.name.empty());

    return 0;
}
