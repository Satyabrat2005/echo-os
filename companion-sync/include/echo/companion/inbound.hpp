// ECHO OS — the inbound caregiver path (Phase 22).
//
// This is the first path in the system where bytes the device did not produce get
// to change what the wearer is told. Everything here treats its input as hostile,
// because the honest description of a caregiver command is "structured input from a
// peer we cannot see, over a link we do not control, asking to write to the device
// of a person with memory loss".
//
// THE LESSONS BEING APPLIED, EXPLICITLY:
//
//   Phase 6's depth cap. A payload of ten thousand nested brackets used to overflow
//   the parser's stack. The decoder here is that same hardened parser (now in
//   echo::common so both sides share one), and its kMaxDepth=200 still bites.
//
//   Phase 6's Gmail header-injection lesson: structured input is not trustworthy
//   input just because it parsed. A JSON string that parses cleanly can still carry
//   a CR/LF, and the Gmail backend learned what happens when such a string is
//   pasted into a protocol that treats newlines as structure. A reminder's text
//   ends up in a spoken utterance and in the event log; control characters are
//   rejected outright rather than stripped, because stripping silently changes what
//   the caregiver thinks they set.
//
//   ADR-10's confirm-before-send discipline. A caregiver-set reminder is a WRITE to
//   the wearer's device. It is announced out loud, always. The wearer may not be
//   able to consent meaningfully in the moment — that is the condition they have —
//   but "silently changed while you weren't looking" is not a thing this device
//   does to the person wearing it.
//
//   Phase 15's naming rule, which is not negotiable: a face is bound to a name only
//   by the wearer, on-device, in the moment. A caregiver may PRE-ENROL a name so it
//   is ready when the person walks in. No inbound command may ever create a person
//   FROM AN EMBEDDING — there is no field for one, and a payload that mentions
//   biometric data is rejected rather than ignored, so a future well-meaning change
//   has to delete this comment to break the rule.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "echo/result.hpp"

namespace echo::companion {

// The complete allowlist. Anything not on it is Unknown and is refused.
enum class CommandKind : std::uint8_t {
    Unknown = 0,
    AddReminder,     // "remind them at 9am to take the blue pill"
    EnrolName,       // pre-register a NAME (never a face)
    RequestDigest,   // ask for a digest now rather than at the next heartbeat
};

// Mirrors memory::Recurrence without depending on it — companion-sync must remain
// unable to name a memory type. The link layer maps between them.
enum class CommandRepeat : std::uint8_t { Once = 0, Daily = 1, Weekly = 2 };

const char* to_string(CommandKind kind) noexcept;

struct InboundCommand {
    CommandKind   kind = CommandKind::Unknown;
    std::uint64_t seq = 0;

    // Validated free text. Non-empty only for the command that uses it.
    std::string   text;      // AddReminder
    std::string   name;      // EnrolName
    std::string   relation;  // EnrolName, optional

    std::int64_t  due = 0;                            // AddReminder, unix seconds
    CommandRepeat repeat = CommandRepeat::Once;       // AddReminder
};

// Why a command was refused. Every one of these is a counter, not a log line with
// the payload in it: a hostile peer must not be able to write attacker-chosen text
// into the device's log just by sending it.
enum class CommandReject : std::uint8_t {
    None = 0,
    TooLarge,        // over the byte cap, refused before parsing
    Malformed,       // the hardened parser returned Null (bad JSON, too deep, garbage)
    NotAnObject,     // parsed, but the document isn't an object
    BadVersion,      // unknown schema version
    UnknownCommand,  // not on the allowlist
    MissingField,
    BadField,        // wrong type, out of range, too long, or control characters
    ForbiddenField,  // a field that must never exist inbound (embedding, person_id, ...)
    RateLimited,
    NotPermitted,    // consent doesn't extend to inbound commands
};

const char* to_string(CommandReject reason) noexcept;

// --- Hard limits -------------------------------------------------------------
// Checked BEFORE parsing where possible, so oversized input costs a length compare
// rather than an allocation.
inline constexpr std::size_t kMaxCommandBytes   = 4096;  // whole payload
inline constexpr std::size_t kMaxReminderChars  = 120;   // fits in a spoken utterance
inline constexpr std::size_t kMaxNameChars      = 64;
inline constexpr std::size_t kMaxRelationChars  = 48;

// A reminder more than a year out, or more than a day in the past, is a bug or an
// attack, not a caregiver. Bounded relative to the device's clock at decode time.
inline constexpr std::int64_t kMaxDueAheadSeconds  = 366LL * 24 * 3600;
inline constexpr std::int64_t kMaxDueBehindSeconds = 24LL * 3600;

struct DecodeResult {
    bool           ok = false;
    InboundCommand command;
    CommandReject  reason = CommandReject::None;
};

// Parse and validate one payload. `now` bounds the due-time check; `last_seq` is
// the highest sequence already accepted (a command at or below it is a replay).
//
// Never throws. Never partially applies. A false result means nothing happened.
DecodeResult decode_command(const std::string& payload, std::int64_t now, std::uint64_t last_seq);

// --- Rate limiting -----------------------------------------------------------
// A token bucket plus a hard daily ceiling. The bucket absorbs a caregiver setting
// three reminders in a row; the ceiling means that even a peer holding a valid
// pairing key cannot turn the link into a firehose — authentication is not a licence
// to flood, and the wearer's device is not obliged to keep listening.
class RateLimiter {
public:
    RateLimiter() = default;
    RateLimiter(std::uint32_t burst, std::int64_t refill_seconds, std::uint32_t daily_cap) noexcept
        : burst_(burst), refill_seconds_(refill_seconds), daily_cap_(daily_cap), tokens_(burst) {}

    // Consume one token. False means refuse — and refusing costs nothing, which is
    // the property that makes this useful under load.
    bool allow(std::int64_t now) noexcept;

    std::uint32_t accepted_today() const noexcept { return accepted_today_; }
    std::uint32_t refused() const noexcept { return refused_; }
    void reset() noexcept;

private:
    std::uint32_t burst_ = 5;
    std::int64_t  refill_seconds_ = 60;  // one full bucket per minute
    std::uint32_t daily_cap_ = 50;

    std::uint32_t tokens_ = 5;
    std::int64_t  last_refill_ = 0;
    std::int64_t  day_start_ = 0;
    std::uint32_t accepted_today_ = 0;
    std::uint32_t refused_ = 0;
};

// --- Firmware ----------------------------------------------------------------
// The TODO in the original scaffold said "verify signature before applying" and
// then returned Unavailable. Phase 22 will not ship a path that half-verifies, so
// it does the other thing the spec allows: FAIL CLOSED, structurally.
struct FirmwareImage {
    std::string               version;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> signature;
};

class IFirmwareVerifier {
public:
    virtual ~IFirmwareVerifier() = default;

    // True ONLY if this image is authentic. An implementation that cannot decide
    // must return false; "probably fine" is not a return value.
    virtual bool verify(const FirmwareImage& image) const noexcept = 0;

    // Human-readable name of the scheme, for logs and for STATE.md's gap table.
    virtual const char* scheme() const noexcept = 0;
};

// The only verifier that exists today. It refuses EVERYTHING, including a
// correctly-signed image, because there is no signing infrastructure to check
// against: no vendor keypair, no asymmetric primitive in this dependency-free
// build, and no release process that signs anything.
//
// A symmetric MAC would not fix this — the device would hold the same key it used
// to verify, so anyone who compromised a device could forge an update for it. That
// is worse than no verification, because it looks like verification.
//
// So the seam exists, is wired in, and is tested to refuse. When there is a real
// signing story, it is one implementation of this interface and nothing else moves.
// Recorded as a permanent-until-signing-infrastructure gap in docs/STATE.md.
std::unique_ptr<IFirmwareVerifier> make_null_verifier();

}  // namespace echo::companion
