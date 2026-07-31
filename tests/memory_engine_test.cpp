// ECHO OS — memory & recall engine unit tests (Phase 15, deliverable #4).
//
// Exercises the REAL SqliteMemory engine through make_memory_engine(), against an
// on-disk store (so persistence-across-restart is actually proven, not mocked) plus
// fixture face embeddings (no OpenCV/ORT needed — constraint #1). Covers:
//   * create / query / update person + reminder records
//   * recurrence advancement on acknowledge
//   * the no-auto-create-from-an-unnamed-face rule
//   * the retrieval-augmented "did I take my medication" query
//   * naming / identity-query utterance parsing
//   * the recall sentence
// and, at the bottom, a COMPILE-TIME extension of Phase 10's structural privacy
// proof: companion-sync cannot accept the memory store's raw content either.
#include "echo/memory/memory_engine.hpp"
#include "echo/memory/utterance.hpp"
#include "echo/companion/companion_sync.hpp"
#include "echo/types.hpp"

#include "check.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

using namespace echo;
using echo::memory::Embedding;
using echo::memory::Recurrence;

namespace {

// A deterministic, dependency-free stand-in for an SFace embedding. Distinct seeds
// are near-orthogonal (cosine ~0), the same seed is identical (cosine 1) — exactly
// the shape the real recognizer produces for different vs. same faces.
Embedding fixture_embedding(int seed, std::size_t dim = 128) {
    Embedding e(dim, 0.0f);
    // A single hot dimension per seed => orthogonal across seeds.
    e[static_cast<std::size_t>(seed) % dim] = 1.0f;
    // A little shared low-energy texture so it's not a pure basis vector.
    for (std::size_t i = 0; i < dim; ++i) e[i] += 0.01f * static_cast<float>((seed + i) % 3);
    return e;
}

std::string temp_db_path(const char* tag) {
    auto p = std::filesystem::temp_directory_path() /
             ("echo_mem_test_" + std::string(tag) + ".db");
    std::error_code ec;
    std::filesystem::remove(p, ec);           // start clean
    std::filesystem::remove(std::filesystem::path(p) += "-wal", ec);
    std::filesystem::remove(std::filesystem::path(p) += "-shm", ec);
    return p.string();
}

constexpr memory::UnixTime kDay = 86400;
// A fixed midday timestamp so "today" logic never straddles local midnight.
constexpr memory::UnixTime kNow = 1753960000;  // ~2025, a Wednesday afternoon UTC

// --- People: create / recognize / update -------------------------------------

void test_person_create_recognize_update() {
    auto db = temp_db_path("people");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);
    CHECK(m->is_open());

    // Nothing known yet.
    CHECK(m->all_people().empty());
    CHECK(!m->recognize(fixture_embedding(1)).matched);

    // Wearer names Priya, bound to face #1.
    auto id = m->remember_person("Priya", "your daughter", fixture_embedding(1), kNow);
    CHECK(id.is_ok());
    CHECK(m->all_people().size() == 1);

    // Recognize the same face -> match; a different face -> no match.
    auto hit = m->recognize(fixture_embedding(1));
    CHECK(hit.matched);
    CHECK(hit.person.name == "Priya");
    CHECK(hit.person.relation == "your daughter");
    CHECK(hit.similarity >= memory::kMatchThreshold);
    CHECK(!m->recognize(fixture_embedding(50)).matched);

    // last_seen updates via mark_seen; notes accumulate.
    CHECK(m->mark_seen(id.value(), kNow + 3 * kDay) == Status::Ok);
    CHECK(m->add_note(id.value(), "likes gardening") == Status::Ok);
    auto p = m->get_person(id.value());
    CHECK(p.has_value());
    CHECK(p->last_seen == kNow + 3 * kDay);
    CHECK(p->notes.find("gardening") != std::string::npos);

    m->close();
}

// THE core privacy rule (Phase 15 deliverable): recognizing an unknown face with no
// naming utterance must create ZERO person records. recognize() is read-only.
void test_unknown_face_creates_no_person() {
    auto db = temp_db_path("noauto");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);

    for (int i = 0; i < 5; ++i) {
        auto hit = m->recognize(fixture_embedding(100 + i));
        CHECK(!hit.matched);
    }
    // No name was ever spoken -> the store must still be empty.
    CHECK(m->all_people().empty());
    m->close();
}

// --- Persistence across "restart" --------------------------------------------

void test_persistence_across_reopen() {
    auto db = temp_db_path("persist");
    memory::PersonId id = 0;
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(db) == Status::Ok);
        auto r = m->remember_person("Sam", "your neighbor", fixture_embedding(7), kNow);
        CHECK(r.is_ok());
        id = r.value();
        m->close();   // simulate shutdown
    }
    {
        auto m = memory::make_memory_engine();   // fresh engine, same file (reboot)
        CHECK(m->open(db) == Status::Ok);
        auto p = m->get_person(id);
        CHECK(p.has_value());
        CHECK(p->name == "Sam");
        // The embedding round-tripped through the BLOB column, so recall still works.
        CHECK(m->recognize(fixture_embedding(7)).matched);
        m->close();
    }
}

// --- Reminders + recurrence ---------------------------------------------------

void test_reminders_and_recurrence() {
    auto db = temp_db_path("reminders");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);

    // A one-off, due in the past relative to kNow.
    auto once = m->add_reminder("call the doctor", kNow - 100, Recurrence::Once);
    CHECK(once.is_ok());

    // Due now, not yet delivered.
    auto due = m->due_reminders(kNow);
    CHECK(due.size() == 1);
    auto pending = m->pending_deliveries(kNow);
    CHECK(pending.size() == 1);

    // Deliver it -> no longer pending (won't re-fire every tick), still unacked.
    CHECK(m->mark_fired(once.value(), kNow) == Status::Ok);
    CHECK(m->pending_deliveries(kNow).empty());
    CHECK(m->due_reminders(kNow).size() == 1);

    // Acknowledge -> a Once reminder closes.
    CHECK(m->acknowledge_reminder(once.value(), kNow) == Status::Ok);
    CHECK(m->due_reminders(kNow).empty());

    // A daily reminder advances by one day on acknowledge and re-arms.
    auto daily = m->add_reminder("take blood pressure medication", kNow - 10, Recurrence::Daily);
    CHECK(daily.is_ok());
    CHECK(m->due_reminders(kNow).size() == 1);
    CHECK(m->acknowledge_reminder(daily.value(), kNow) == Status::Ok);
    // Now not due at kNow (advanced ~a day out), but due again a day later.
    CHECK(m->due_reminders(kNow).empty());
    CHECK(m->due_reminders(kNow + kDay + 20).size() == 1);

    m->close();
}

// --- Retrieval-augmented memory query ----------------------------------------

void test_medication_query_from_real_record() {
    auto db = temp_db_path("medquery");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);

    auto med = m->add_reminder("take your evening medication", kNow - 10, Recurrence::Daily);
    CHECK(med.is_ok());

    // Before acknowledging: the honest answer is "not yet".
    std::string before = m->answer_query("did I take my medication today?", kNow);
    CHECK(before.find("Not yet") != std::string::npos);

    // After acknowledging at kNow: the answer is drawn from the real log.
    CHECK(m->acknowledge_reminder(med.value(), kNow) == Status::Ok);
    std::string after = m->answer_query("did I take my medication today?", kNow);
    CHECK(after.find("Yes") != std::string::npos);
    CHECK(after.find("medication") != std::string::npos);

    // A query the store has nothing concrete for returns "" (caller falls back;
    // never fabricates).
    CHECK(m->answer_query("what is the capital of France?", kNow).empty());

    m->close();
}

// --- Event log ---------------------------------------------------------------

void test_event_log_records_notable_events() {
    auto db = temp_db_path("events");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);

    auto id = m->remember_person("Priya", "your daughter", fixture_embedding(1), kNow);
    CHECK(id.is_ok());
    m->mark_seen(id.value(), kNow + kDay);
    auto rem = m->add_reminder("drink water", kNow, Recurrence::Once);
    m->mark_fired(rem.value(), kNow);
    m->acknowledge_reminder(rem.value(), kNow);

    auto events = m->recent_events(50);
    // PersonNamed, PersonSeen, ReminderFired, ReminderAcknowledged -> 4 events.
    CHECK(events.size() == 4);
    bool saw_named = false, saw_seen = false, saw_fired = false, saw_ack = false;
    for (const auto& e : events) {
        if (e.kind == memory::EventKind::PersonNamed)          saw_named = true;
        if (e.kind == memory::EventKind::PersonSeen)           saw_seen = true;
        if (e.kind == memory::EventKind::ReminderFired)        saw_fired = true;
        if (e.kind == memory::EventKind::ReminderAcknowledged) saw_ack = true;
        CHECK(!e.summary.empty());   // every event is human-readable
    }
    CHECK(saw_named && saw_seen && saw_fired && saw_ack);
    m->close();
}

// --- Utterance parsing (pure, no DB) ------------------------------------------

void test_naming_parse() {
    auto a = memory::parse_naming("this is my daughter Priya");
    CHECK(a.has_value());
    CHECK(a->name == "Priya");
    CHECK(a->relation == "your daughter");

    auto b = memory::parse_naming("this is Sam");
    CHECK(b.has_value());
    CHECK(b->name == "Sam");
    CHECK(b->relation.empty());

    auto c = memory::parse_naming("this is my good friend John");
    CHECK(c.has_value());
    CHECK(c->name == "John");
    CHECK(c->relation == "your good friend");

    // Ordinary speech must NOT be read as naming (else it would create people).
    CHECK(!memory::parse_naming("what time is it").has_value());
    CHECK(!memory::parse_naming("play some jazz music").has_value());
    CHECK(!memory::parse_naming("who is this").has_value());
}

void test_identity_query_detection() {
    CHECK(memory::is_identity_query("who is this"));
    CHECK(memory::is_identity_query("Who's that?"));
    CHECK(memory::is_identity_query("remind me who they are"));
    CHECK(!memory::is_identity_query("what time is it"));
    CHECK(!memory::is_identity_query("this is my daughter Priya"));
}

void test_recall_sentence() {
    memory::PersonRecord p;
    p.name = "Priya";
    p.relation = "your daughter";
    p.last_seen = kNow - 2 * kDay;
    std::string s = memory::recall_sentence(p, kNow);
    CHECK(s.find("Priya") != std::string::npos);
    CHECK(s.find("your daughter") != std::string::npos);
    CHECK(s.find("2 days ago") != std::string::npos);
    CHECK(s.find("her") != std::string::npos);   // pronoun from "daughter"
}

// --- COMPILE-TIME PRIVACY PROOF (extends Phase 10's companion-sync proof) ------
// Phase 10 proved companion-sync's transport cannot accept a SensorFrame. The
// memory store adds NEW raw content — face embeddings, notes, the event log — that
// must be just as unable to reach the sync path. We prove that structurally, the
// same way: if someone ever added a send path (or a payload constructor) that could
// swallow the store's content, THIS FILE STOPS COMPILING.
using companion::ICompanionSync;
using companion::Alert;
using companion::StatusReport;

template <class... Args>
inline constexpr bool alert_invocable_with =
    std::is_invocable_v<decltype(&ICompanionSync::send_alert), ICompanionSync&, Args...>;
template <class... Args>
inline constexpr bool status_invocable_with =
    std::is_invocable_v<decltype(&ICompanionSync::send_status), ICompanionSync&, Args...>;

// No send path accepts a raw embedding, a person record, or an event record.
static_assert(!alert_invocable_with<memory::Embedding>,
              "PRIVACY: no companion send path may accept a face embedding");
static_assert(!alert_invocable_with<const memory::Embedding&>,
              "PRIVACY: no companion send path may accept a face embedding");
static_assert(!alert_invocable_with<memory::PersonRecord>,
              "PRIVACY: no companion send path may accept a person record");
static_assert(!alert_invocable_with<const memory::PersonRecord&>,
              "PRIVACY: no companion send path may accept a person record");
static_assert(!alert_invocable_with<memory::EventRecord>,
              "PRIVACY: no companion send path may accept an event record");
static_assert(!status_invocable_with<memory::PersonRecord>,
              "PRIVACY: no companion status path may accept a person record");
static_assert(!status_invocable_with<memory::Embedding>,
              "PRIVACY: no companion status path may accept a face embedding");

// The payloads themselves cannot be built out of the store's raw content.
static_assert(!std::is_constructible_v<Alert, memory::PersonRecord>,
              "PRIVACY: an Alert must not be constructible from a person record");
static_assert(!std::is_constructible_v<Alert, memory::Embedding>,
              "PRIVACY: an Alert must not be constructible from a face embedding");
static_assert(!std::is_constructible_v<StatusReport, memory::PersonRecord>,
              "PRIVACY: a StatusReport must not be constructible from a person record");

void test_memory_content_cannot_reach_sync_path() {
    CHECK((!alert_invocable_with<memory::PersonRecord>));
    CHECK((!alert_invocable_with<memory::Embedding>));
    CHECK((!std::is_constructible_v<Alert, memory::EventRecord>));
    std::printf("[memory] structural privacy guarantee holds: the memory store's raw "
                "content (embeddings, person records, events) has no path to "
                "companion-sync (enforced at compile time)\n");
}

}  // namespace

int main() {
    test_person_create_recognize_update();
    test_unknown_face_creates_no_person();
    test_persistence_across_reopen();
    test_reminders_and_recurrence();
    test_medication_query_from_real_record();
    test_event_log_records_notable_events();
    test_naming_parse();
    test_identity_query_detection();
    test_recall_sentence();
    test_memory_content_cannot_reach_sync_path();
    return echo::test::report("memory-engine");
}
