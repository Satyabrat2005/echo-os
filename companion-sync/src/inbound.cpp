// ECHO OS — validating caregiver commands. See inbound.hpp for which prior lesson
// each check here is paying off.
#include "echo/companion/inbound.hpp"

#include "echo/json.hpp"

#include <cmath>

namespace echo::companion {

const char* to_string(CommandKind kind) noexcept {
    switch (kind) {
        case CommandKind::Unknown:       return "unknown";
        case CommandKind::AddReminder:   return "add-reminder";
        case CommandKind::EnrolName:     return "enrol-name";
        case CommandKind::RequestDigest: return "request-digest";
    }
    return "unknown";
}

const char* to_string(CommandReject reason) noexcept {
    switch (reason) {
        case CommandReject::None:           return "none";
        case CommandReject::TooLarge:       return "too-large";
        case CommandReject::Malformed:      return "malformed";
        case CommandReject::NotAnObject:    return "not-an-object";
        case CommandReject::BadVersion:     return "bad-version";
        case CommandReject::UnknownCommand: return "unknown-command";
        case CommandReject::MissingField:   return "missing-field";
        case CommandReject::BadField:       return "bad-field";
        case CommandReject::ForbiddenField: return "forbidden-field";
        case CommandReject::RateLimited:    return "rate-limited";
        case CommandReject::NotPermitted:   return "not-permitted";
    }
    return "unknown";
}

namespace {

// Keys that must NEVER appear on an inbound command, checked before anything else
// is read. Their presence is treated as an attack, not as a field to ignore.
//
// "embedding" and "descriptor": Phase 15's rule is that a face is bound to a name
// only by the wearer, on-device, in the moment. There is no code path here that
// would accept biometric data, but rejecting the KEY means a future change that
// adds one has to delete this list first — a deliberate speed bump in front of the
// one rule the phase spec calls non-negotiable.
//
// "person_id" / "reminder_id": a caregiver may CREATE things. They may not reach in
// and address a specific row in the wearer's store by its internal identifier.
constexpr const char* kForbiddenKeys[] = {
    "embedding", "descriptor", "face", "template", "biometric",
    "person_id", "reminder_id", "event_id",
};

bool has_forbidden_key(const Json& doc) noexcept {
    for (const char* key : kForbiddenKeys)
        if (doc.contains(key)) return true;
    return false;
}

// Reject control characters outright rather than stripping them.
//
// This is the Gmail header-injection lesson from Phase 6, in a different costume: a
// string that parsed cleanly is not a safe string. This text ends up in a spoken
// utterance and in the event log, and a caregiver who typed a newline into a
// reminder deserves to be told their reminder was refused — not to have it silently
// altered into something they never wrote and cannot see.
bool is_clean_text(const std::string& s) noexcept {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) return false;  // C0 controls and DEL
    }
    return true;
}

// A validated free-text field: present, a string, non-empty after the caller's own
// rules, within the cap, and free of control characters.
//
// Absent and present-but-invalid are reported as different reasons. The distinction
// never changes the decision — both are refused, and refusal is the only outcome that
// protects the wearer — but it is the entire diagnostic value of the reason code. An
// author whose reminder is rejected with "bad-field" will go hunting for a character
// that offended the filter, when in fact they never sent the key at all.
CommandReject take_text(const Json& doc, const char* key, std::size_t max_chars,
                        std::string& out) {
    const Json& v = doc[key];
    if (v.is_null()) return CommandReject::MissingField;  // absent, or explicitly null
    if (!v.is_string()) return CommandReject::BadField;   // present, wrong type
    out = v.as_string();
    if (out.empty() || out.size() > max_chars) return CommandReject::BadField;
    return is_clean_text(out) ? CommandReject::None : CommandReject::BadField;
}

// A JSON number used as an integer. Rejects NaN/inf and anything not exactly
// representable — a due time is a moment, not an approximation.
bool take_int(const Json& doc, const char* key, std::int64_t& out) {
    const Json& v = doc[key];
    if (!v.is_number()) return false;
    const double d = v.as_number();
    if (!std::isfinite(d)) return false;
    if (d < -9.0e15 || d > 9.0e15) return false;
    out = static_cast<std::int64_t>(d);
    return static_cast<double>(out) == d;
}

}  // namespace

DecodeResult decode_command(const std::string& payload, std::int64_t now, std::uint64_t last_seq) {
    DecodeResult result;
    auto refuse = [&result](CommandReject reason) {
        result.ok = false;
        result.reason = reason;
        result.command = InboundCommand{};  // never partially populated
        return result;
    };

    // Size cap first: a length compare, before any allocation or parsing.
    if (payload.size() > kMaxCommandBytes) return refuse(CommandReject::TooLarge);

    // The Phase 6 hardened parser. Malformed input — including the deeply-nested
    // payload that used to overflow the stack — comes back as Null, never as a throw
    // and never as a crash.
    const Json doc = Json::parse(payload);
    if (doc.is_null()) return refuse(CommandReject::Malformed);
    if (!doc.is_object()) return refuse(CommandReject::NotAnObject);

    if (has_forbidden_key(doc)) return refuse(CommandReject::ForbiddenField);

    std::int64_t version = 0;
    if (!take_int(doc, "v", version)) return refuse(CommandReject::MissingField);
    if (version != 1) return refuse(CommandReject::BadVersion);

    std::int64_t seq = 0;
    if (!take_int(doc, "seq", seq)) return refuse(CommandReject::MissingField);
    if (seq <= 0) return refuse(CommandReject::BadField);
    if (static_cast<std::uint64_t>(seq) <= last_seq) return refuse(CommandReject::BadField);
    result.command.seq = static_cast<std::uint64_t>(seq);

    const Json& cmd = doc["cmd"];
    if (!cmd.is_string()) return refuse(CommandReject::MissingField);
    const std::string& kind = cmd.as_string();

    if (kind == "add_reminder") {
        result.command.kind = CommandKind::AddReminder;

        if (const CommandReject r = take_text(doc, "text", kMaxReminderChars, result.command.text);
            r != CommandReject::None)
            return refuse(r);

        std::int64_t due = 0;
        if (!take_int(doc, "due", due)) return refuse(CommandReject::MissingField);
        // Bounded against the device's own clock. A reminder two years out or a week
        // in the past is a bug or an attack; either way the wearer should not be
        // holding it.
        if (due > now + kMaxDueAheadSeconds || due < now - kMaxDueBehindSeconds)
            return refuse(CommandReject::BadField);
        result.command.due = due;

        // "repeat" is optional and defaults to once — the least surprising, least
        // durable choice. A typo in it is an error, not a silent daily reminder.
        if (doc.contains("repeat")) {
            const Json& rep = doc["repeat"];
            if (!rep.is_string()) return refuse(CommandReject::BadField);
            const std::string& r = rep.as_string();
            if (r == "once")        result.command.repeat = CommandRepeat::Once;
            else if (r == "daily")  result.command.repeat = CommandRepeat::Daily;
            else if (r == "weekly") result.command.repeat = CommandRepeat::Weekly;
            else return refuse(CommandReject::BadField);
        }
    } else if (kind == "enrol_name") {
        result.command.kind = CommandKind::EnrolName;

        if (const CommandReject r = take_text(doc, "name", kMaxNameChars, result.command.name);
            r != CommandReject::None)
            return refuse(r);

        // Relation is optional; if present it must still be clean and capped.
        if (doc.contains("relation")) {
            if (const CommandReject r =
                    take_text(doc, "relation", kMaxRelationChars, result.command.relation);
                r != CommandReject::None)
                return refuse(r);
        }
    } else if (kind == "request_digest") {
        result.command.kind = CommandKind::RequestDigest;
    } else {
        return refuse(CommandReject::UnknownCommand);
    }

    result.ok = true;
    result.reason = CommandReject::None;
    return result;
}

// --- Rate limiting -----------------------------------------------------------

bool RateLimiter::allow(std::int64_t now) noexcept {
    if (last_refill_ == 0) last_refill_ = now;
    if (day_start_ == 0) day_start_ = now;

    // Roll the daily window. A fixed 24h window from first use, not a calendar day:
    // the device has no timezone and does not need one to cap a count.
    if (now - day_start_ >= 24 * 3600) {
        day_start_ = now;
        accepted_today_ = 0;
    }

    // Refill. Whole buckets only — a caregiver who sends five commands and waits a
    // minute gets five more, which is the behaviour worth being predictable about.
    if (refill_seconds_ > 0 && now - last_refill_ >= refill_seconds_) {
        const std::int64_t periods = (now - last_refill_) / refill_seconds_;
        last_refill_ += periods * refill_seconds_;
        tokens_ = burst_;
    }

    if (accepted_today_ >= daily_cap_ || tokens_ == 0) {
        ++refused_;
        return false;
    }

    --tokens_;
    ++accepted_today_;
    return true;
}

void RateLimiter::reset() noexcept {
    tokens_ = burst_;
    last_refill_ = 0;
    day_start_ = 0;
    accepted_today_ = 0;
    refused_ = 0;
}

// --- Firmware ----------------------------------------------------------------

namespace {

class NullVerifier final : public IFirmwareVerifier {
public:
    bool verify(const FirmwareImage&) const noexcept override {
        // Fail closed. There is no vendor keypair, no asymmetric primitive in this
        // dependency-free build, and no release process that signs anything — so
        // there is nothing to check an image against. Returning false for a
        // correctly-signed image is the correct behaviour when "correctly signed"
        // is not a thing this device can determine.
        return false;
    }
    const char* scheme() const noexcept override { return "none (fail-closed)"; }
};

}  // namespace

std::unique_ptr<IFirmwareVerifier> make_null_verifier() {
    return std::make_unique<NullVerifier>();
}

}  // namespace echo::companion
