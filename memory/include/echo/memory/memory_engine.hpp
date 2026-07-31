// ECHO OS — the memory & recall engine (Phase 15).
//
// This is the module the whole product is *about*: a local, private, on-device
// store of the people the wearer knows, the reminders they need, and a lightweight
// log of what ECHO has recognized. Everything else in the OS is generic voice-
// assistant plumbing; this is the part that "remembers for you."
//
// PRIVACY (principle #4): the store lives only on the device. Its raw content —
// face embeddings, notes, the event log — has NO path to companion-sync. That is
// not a comment; it is proven at compile time in tests/memory_engine_test.cpp the
// same way Phase 10 proved companion-sync can't accept a SensorFrame.
//
// This header is dependency-free (no SQLite in the interface) so perception,
// cognitive-core, and the tests can use it against fixture data without linking a
// database. The backing store is real (SQLite, persisted to disk); the header just
// doesn't leak that.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace echo::memory {

// Wall-clock time as Unix seconds. The rest of the OS uses steady_clock for the
// latency budget (echo::now()), but "last seen two days ago" and "due at 9am" are
// civil-time facts, so the store speaks Unix seconds. Every method that needs the
// current time takes it as a parameter (rather than reading the clock itself) so
// recurrence and recall are deterministic under test.
using UnixTime = std::int64_t;

// The current wall clock, as Unix seconds. Callers in the live pipeline pass this;
// tests pass fixed values.
UnixTime unix_now() noexcept;

// A face embedding as produced by vision's SFace recognizer (a 128-d float
// vector). Kept as a plain vector so this interface has no OpenCV dependency and
// tests can feed fixture embeddings directly. This — not any pixels — is the
// stable identity key for a person (principle #4: no raw image is persisted).
using Embedding = std::vector<float>;

using PersonId   = std::int64_t;   // stable primary key in the store
using ReminderId = std::int64_t;

// A person the wearer has been told about. Created ONLY when the wearer explicitly
// names someone (see remember_person); never auto-created from an unknown face.
struct PersonRecord {
    PersonId    id = 0;
    std::string name;             // display name, e.g. "Priya"
    std::string relation;         // caregiver-safe phrase, e.g. "your daughter" ("" if none)
    std::string notes;            // free text accumulated over time ("" if none)
    Embedding   embedding;        // SFace identity vector (may be empty if unanchored)
    UnixTime    last_seen = 0;    // last confident recognition (0 = never/just created)
};

// The result of matching a face embedding against the store.
struct PersonMatch {
    bool         matched    = false;
    PersonRecord person;              // valid only when matched
    float        similarity = 0.0f;   // cosine similarity of the match
};

// How a reminder repeats.
enum class Recurrence : std::uint8_t { Once, Daily, Weekly };
const char* to_string(Recurrence r) noexcept;

// A thing the wearer needs to be reminded of.
struct ReminderRecord {
    ReminderId  id = 0;
    std::string text;                 // "take blood pressure medication"
    UnixTime    due = 0;              // next occurrence (Unix seconds)
    Recurrence  recurrence = Recurrence::Once;
    bool        acknowledged = false; // true once done (for Once) — recurring ones re-arm
};

// The kinds of notable event the log records. Deliberately narrow: a caregiver
// could reasonably see any of these. Raw conversation transcripts are NOT events.
enum class EventKind : std::uint8_t {
    PersonNamed,            // wearer named someone new
    PersonSeen,            // a known person was confidently recognized
    ReminderFired,        // a due reminder was spoken
    ReminderAcknowledged, // wearer confirmed they did it
    ReminderMissed,       // reserved: a fired reminder went unacknowledged (future)
};
const char* to_string(EventKind k) noexcept;

// One append-only log line. `summary` is a short, human-readable, caregiver-safe
// string — never a raw buffer or transcript.
struct EventRecord {
    std::int64_t id = 0;
    EventKind    kind = EventKind::PersonSeen;
    UnixTime     at = 0;
    std::string  summary;
    PersonId     person_id = 0;    // 0 when not applicable
    ReminderId   reminder_id = 0;  // 0 when not applicable
};

// Bounds on how the store grows over time (Phase 16, Part 2). The event log is the
// append-only part most likely to grow without limit, so it is capped by BOTH count
// and age. Person notes are meaningfully valuable long-term, so they are NOT pruned
// by age — only bounded by a per-person segment cap with oldest-first eviction (a
// deliberately simple, robust first pass; LLM summarization was considered and
// deferred, see ADR-15). A field set to 0 disables that particular limit.
struct RetentionPolicy {
    int max_events           = 2000;  // hard cap on event-log rows (0 = unlimited)
    int max_event_age_days   = 90;    // drop events older than this many days (0 = none)
    int max_notes_per_person = 20;    // cap on "; "-separated note segments (0 = unlimited)
};

// Defaults, exposed so docs and tests can reference the shipped policy directly.
inline constexpr RetentionPolicy kDefaultRetention{};

// The on-device memory store. All methods are non-throwing; a store that failed to
// open degrades to empty results (the pipeline keeps running without memory rather
// than crashing on a vulnerable device).
class IMemoryEngine {
public:
    virtual ~IMemoryEngine() = default;

    // Open (creating if needed) the store at `db_path`. Pass ":memory:" for a
    // non-persistent store (used by unit tests that don't exercise restart). The
    // live runtime passes a real file path so records survive a reboot.
    //
    // ENCRYPTION AT REST (Phase 16). For a real file path the on-disk image is
    // AES-256-CTR ciphertext, keyed by a per-device key (see device_key.hpp); the
    // plaintext SQLite image lives only in process memory. An existing *plaintext*
    // Phase-15 database found at `db_path` is migrated in place to the encrypted
    // format on open (logged, never silently discarded). A ":memory:" store is not
    // encrypted (there is no file).
    virtual Status open(const std::string& db_path) = 0;
    virtual bool   is_open() const noexcept = 0;
    virtual void   close() = 0;

    // --- Retention (Phase 16, Part 2) ---------------------------------------

    // Override the growth-bounding policy (defaults to kDefaultRetention, further
    // overridable by ECHO_MEMORY_MAX_* env vars at open()). Tests use this to set
    // small, deterministic caps.
    virtual void set_retention(const RetentionPolicy& policy) = 0;

    // Enforce the retention policy now: evict old/excess event-log rows and bound
    // per-person notes, then persist. The live runtime calls this on the SAME
    // scheduler tick that delivers reminders (constraint: reuse the loop, don't add
    // a third timer). Cheap on the small tables a wearable accumulates.
    virtual void enforce_retention(UnixTime now) = 0;

    // --- People -------------------------------------------------------------

    // Match a face embedding against known people. Returns the best match above the
    // similarity threshold, else matched=false. READ-ONLY: this NEVER creates a
    // record — recognizing a stranger must not invent an identity (principle #4).
    virtual PersonMatch recognize(const Embedding& embedding) = 0;

    // Create a person the wearer explicitly named. This is the ONLY path that adds
    // a person. `relation` is the caregiver-safe phrase ("your daughter"; may be
    // empty). Returns the new id.
    virtual Result<PersonId> remember_person(const std::string& name,
                                             const std::string& relation,
                                             const Embedding& embedding,
                                             UnixTime now) = 0;

    // Record that a known person was just seen: bumps last_seen and appends a
    // PersonSeen event. Call AFTER building any "you last saw them N days ago"
    // phrase from the pre-update last_seen.
    virtual Status mark_seen(PersonId id, UnixTime now) = 0;

    // Append free-text context to a person's notes.
    virtual Status add_note(PersonId id, const std::string& note) = 0;

    virtual std::optional<PersonRecord> get_person(PersonId id) = 0;
    virtual std::vector<PersonRecord>   all_people() = 0;

    // --- Reminders ----------------------------------------------------------

    virtual Result<ReminderId> add_reminder(const std::string& text, UnixTime due,
                                            Recurrence recurrence) = 0;

    // Reminders that are due now and not yet acknowledged (for queries/inspection).
    virtual std::vector<ReminderRecord> due_reminders(UnixTime now) = 0;

    // Reminders that are due AND have not yet been delivered for this occurrence.
    // The runtime tick speaks these, then calls mark_fired so they don't repeat
    // every tick. Separate from due_reminders so "what's due" and "what to speak
    // now" are distinct questions.
    virtual std::vector<ReminderRecord> pending_deliveries(UnixTime now) = 0;

    // Mark a reminder as delivered for its current occurrence (logs ReminderFired).
    virtual Status mark_fired(ReminderId id, UnixTime now) = 0;

    // Wearer confirms they did it: a Once reminder is closed; a recurring one
    // advances to its next occurrence and re-arms. Logs ReminderAcknowledged.
    virtual Status acknowledge_reminder(ReminderId id, UnixTime now) = 0;

    virtual std::vector<ReminderRecord> all_reminders() = 0;

    // --- Memory-query (retrieval-augmented answers) -------------------------

    // Answer a memory question ("did I take my medication today?") from the REAL
    // record — never fabricated. Returns "" when the store has nothing concrete to
    // say, so the caller can fall back rather than assert a made-up fact.
    virtual std::string answer_query(const std::string& question, UnixTime now) = 0;

    // --- Event log ----------------------------------------------------------

    virtual Status log_event(const EventRecord& event) = 0;
    virtual std::vector<EventRecord> recent_events(int limit) = 0;
};

std::unique_ptr<IMemoryEngine> make_memory_engine();

// Build the enriched, spoken recall sentence for a recognized person from the real
// record — e.g. "That's Priya, your daughter. You last saw her 2 days ago." This is
// the concrete behavior change "who is this" now produces (a fact, not a bare
// "face detected"). Pure formatting over a PersonRecord, so it lives with the store
// and is unit-testable without a database.
std::string recall_sentence(const PersonRecord& person, UnixTime now);

// Cosine similarity of two embeddings, in [-1, 1]; 0 for empty/mismatched-length
// inputs. Exposed for tests and for callers that want to score a match themselves.
float cosine_similarity(const Embedding& a, const Embedding& b) noexcept;

// The cosine threshold above which two SFace embeddings are treated as the same
// person. Matches vision's SFace convention (OpenCV documents 0.363).
inline constexpr float kMatchThreshold = 0.363f;

}  // namespace echo::memory
