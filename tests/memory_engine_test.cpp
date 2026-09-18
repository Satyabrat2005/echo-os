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
#include "echo/memory/aes256.hpp"
#include "echo/companion/companion_sync.hpp"
#include "echo/types.hpp"

#include "check.hpp"
#include "fixture_io.hpp"   // fixture_path() + ECHO_FIXTURES_DIR

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    std::filesystem::remove(std::filesystem::path(p) += ".key", ec);  // Phase 16 key file
    std::filesystem::remove(std::filesystem::path(p) += ".tmp", ec);
    return p.string();
}

// Read a whole file into bytes (for inspecting the raw on-disk store).
std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

// Does the raw byte buffer contain `needle` as a substring? Used to prove a stored
// name does NOT appear in plaintext inside the encrypted file.
bool contains_bytes(const std::vector<std::uint8_t>& hay, const std::string& needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (std::memcmp(hay.data() + i, needle.data(), needle.size()) == 0) return true;
    return false;
}

bool starts_with_str(const std::vector<std::uint8_t>& b, const std::string& s) {
    return b.size() >= s.size() && std::memcmp(b.data(), s.data(), s.size()) == 0;
}

void hex_to(const char* h, std::uint8_t* out, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned v = 0;
        (void)std::sscanf(h + 2 * i, "%2x", &v);
        out[i] = static_cast<std::uint8_t>(v);
    }
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

// --- Phase 16: AES-256 correctness (known-answer vectors) ---------------------
// The at-rest encryption is only as trustworthy as its cipher, and the cipher is
// hand-rolled (see aes256.hpp for why). So we pin it to the PUBLISHED vectors:
// FIPS-197's AES-256 example block and NIST SP 800-38A's CTR-AES256 test vector.
void test_aes_known_answer_vectors() {
    using namespace echo::memory::crypto;

    // FIPS-197, Appendix C.3 — AES-256 single-block encryption.
    Key256 k{};
    hex_to("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", k.data(), 32);
    Block in{};
    hex_to("00112233445566778899aabbccddeeff", in.data(), 16);
    Block out = encrypt_block(k, in);
    Block want{};
    hex_to("8ea2b7ca516745bfeafc49904b496089", want.data(), 16);
    CHECK(out == want);

    // NIST SP 800-38A, F.5.5 — CTR-AES256, first two blocks.
    Key256 k2{};
    hex_to("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", k2.data(), 32);
    Block iv{};
    hex_to("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv.data(), 16);
    std::uint8_t buf[32];
    hex_to("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", buf, 32);
    ctr_xcrypt(k2, iv, buf, 32);
    std::uint8_t exp[32];
    hex_to("601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5", exp, 32);
    CHECK(std::memcmp(buf, exp, 32) == 0);

    // CTR is symmetric: decrypting the ciphertext returns the plaintext.
    ctr_xcrypt(k2, iv, buf, 32);
    std::uint8_t pt[32];
    hex_to("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", pt, 32);
    CHECK(std::memcmp(buf, pt, 32) == 0);
}

// --- Phase 16: encryption at rest ---------------------------------------------
// Proves the on-disk store is ciphertext, not a readable SQLite file, and that the
// data is unrecoverable without the device key.
void test_encryption_at_rest() {
    auto db = temp_db_path("encrest");
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(db) == Status::Ok);
        auto id = m->remember_person("Priya", "your daughter", fixture_embedding(1), kNow);
        CHECK(id.is_ok());
        CHECK(m->add_note(id.value(), "likes gardening in the mornings") == Status::Ok);
        m->close();
    }

    // The raw file is our encrypted container, NOT a plaintext SQLite database, and
    // the stored name/notes do not appear as plaintext anywhere in it.
    auto raw = read_bytes(db);
    CHECK(!raw.empty());
    CHECK(!starts_with_str(raw, "SQLite format 3"));   // would be the header of a plaintext db
    CHECK(starts_with_str(raw, "ECHOAEM1"));           // our authenticated container magic (Phase 24)
    CHECK(!contains_bytes(raw, "Priya"));
    CHECK(!contains_bytes(raw, "gardening"));
    CHECK(!contains_bytes(raw, "your daughter"));

    // With the (unchanged) key file beside it, a fresh engine recovers everything.
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(db) == Status::Ok);
        CHECK(m->all_people().size() == 1);
        auto hit = m->recognize(fixture_embedding(1));
        CHECK(hit.matched);
        CHECK(hit.person.name == "Priya");
        CHECK(hit.person.notes.find("gardening") != std::string::npos);
        m->close();
    }

    // Destroy/replace the key -> the store is undecryptable and the engine refuses to
    // open it (fails closed rather than exposing or discarding data silently).
    {
        std::ofstream keyout(db + ".key", std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 32; ++i) keyout.put(static_cast<char>(0xA5));  // wrong key
    }
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(db) != Status::Ok);   // wrong key -> cannot decrypt
        CHECK(!m->is_open());
    }
}

// --- Phase 16: one-time migration of an unencrypted Phase-15 store -------------
// Opens the checked-in plaintext fixture DB and proves its person/reminder records
// survive and the file is rewritten encrypted (a Phase-15 wearer's data is migrated,
// never discarded).
void test_migration_from_plaintext() {
    // Work on a private copy so the checked-in fixture stays pristine.
    auto dst = temp_db_path("migrate");
    std::error_code ec;
    std::filesystem::copy_file(echo::test::fixture_path("phase15_plaintext.db"), dst,
                               std::filesystem::copy_options::overwrite_existing, ec);
    CHECK(!ec);

    // Sanity: the copy really is a plaintext SQLite db going in.
    CHECK(starts_with_str(read_bytes(dst), "SQLite format 3"));

    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(dst) == Status::Ok);   // triggers the one-time migration

        // The Phase-15 person survived, notes and relation intact.
        bool found_margaret = false;
        for (const auto& p : m->all_people()) {
            if (p.name == "Margaret") {
                found_margaret = true;
                CHECK(p.relation == "your mother");
                CHECK(p.notes.find("crosswords") != std::string::npos);
            }
        }
        CHECK(found_margaret);

        // The Phase-15 reminder survived.
        bool found_reminder = false;
        for (const auto& r : m->all_reminders())
            if (r.text.find("heart medication") != std::string::npos) found_reminder = true;
        CHECK(found_reminder);
        m->close();
    }

    // After migration the on-disk file is encrypted, not plaintext — and, since save_to_disk
    // always writes v2, authenticated (Phase 24), not the old unauthenticated container.
    auto raw = read_bytes(dst);
    CHECK(starts_with_str(raw, "ECHOAEM1"));
    CHECK(!starts_with_str(raw, "SQLite format 3"));
    CHECK(!contains_bytes(raw, "Margaret"));

    // And it reopens cleanly as an encrypted store.
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(dst) == Status::Ok);
        CHECK(!m->all_people().empty());
        m->close();
    }
}

// --- Phase 24: authenticated encryption rejects tampering --------------------
// The v2 container's CMAC is verified BEFORE decryption. A flipped byte anywhere in the
// IV, ciphertext, or tag must be rejected (Status::HardwareError, never a partial or
// garbage open), and a truncated tag must be rejected too. Mirrors the tamper-test style
// of tests/caregiver_link_test.cpp's test_secure_channel_rejects_tampering, scoped to a
// representative sample of positions rather than every byte of a multi-KB SQLite image.
void test_tampered_ciphertext_rejected() {
    auto db = temp_db_path("tamper");
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(db) == Status::Ok);
        auto id = m->remember_person("Omar", "your neighbor", fixture_embedding(2), kNow);
        CHECK(id.is_ok());
        m->close();
    }

    auto original = read_bytes(db);
    CHECK(original.size() > 40);  // magic(8) + iv(16) + at least some ciphertext + tag(16)

    auto write_and_try_open = [&](const std::vector<std::uint8_t>& bytes) {
        {
            std::ofstream out(db, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        auto m = memory::make_memory_engine();
        CHECK(m->open(db) == Status::HardwareError);
        CHECK(!m->is_open());
    };

    auto flip = [&](std::size_t pos) {
        auto tampered = original;
        tampered[pos] ^= 0x01;
        write_and_try_open(tampered);
    };

    flip(0);                      // magic
    flip(8);                      // first IV byte
    flip(23);                     // last IV byte
    flip(original.size() / 2);    // mid-ciphertext
    for (std::size_t i = original.size() - 16; i < original.size(); ++i) flip(i);  // every tag byte

    // Truncating the trailing tag entirely must also be rejected, not silently accepted
    // as "shorter ciphertext".
    auto truncated = original;
    truncated.resize(truncated.size() - 16);
    write_and_try_open(truncated);

    // Restore the untampered file and confirm it still opens cleanly — proves the checks
    // above were rejecting the tampering, not some unrelated fixture problem.
    {
        std::ofstream out(db, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(original.data()),
                  static_cast<std::streamsize>(original.size()));
    }
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);
    CHECK(m->all_people().size() == 1);
    m->close();
}

// --- Phase 24: migrating a legacy (Phase 16) unauthenticated store forward ---
// Synthesizes a v1 "ECHOAES1" container in-test (using the already-KAT'd ctr_xcrypt
// directly over the real Phase-15 plaintext fixture — no new fixture file needed), opens
// it, confirms the data survives, confirms the store is rewritten as the v2 authenticated
// container on close, and confirms a SECOND open now rejects tampering — proving the
// migration actually re-protects the store, not just re-encodes it.
void test_migration_from_v1_unauthenticated() {
    using namespace echo::memory::crypto;

    auto plaintext = read_bytes(echo::test::fixture_path("phase15_plaintext.db"));
    CHECK(starts_with_str(plaintext, "SQLite format 3"));

    auto dst = temp_db_path("migrate-v1");

    // Pin the key ourselves — reusing the exact FIPS-197 AES-256 key already proven
    // correct by test_aes_known_answer_vectors above — so the legacy container below can
    // be built under a KNOWN key before the engine ever runs.
    Key256 key{};
    hex_to("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key.data(), 32);
    {
        std::ofstream keyout(dst + ".key", std::ios::binary | std::ios::trunc);
        keyout.write(reinterpret_cast<const char*>(key.data()),
                      static_cast<std::streamsize>(key.size()));
    }

    // Hand-build the legacy v1 container: magic "ECHOAES1" + 16-byte IV + AES-256-CTR
    // ciphertext, no MAC — exactly the format Phase 16 shipped. IV reused from the
    // already-proven SP 800-38A CTR vector above, for the same reason as the key.
    Block iv{};
    hex_to("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv.data(), 16);
    std::vector<std::uint8_t> legacy;
    const char magic[8] = {'E', 'C', 'H', 'O', 'A', 'E', 'S', '1'};
    legacy.insert(legacy.end(), magic, magic + 8);
    legacy.insert(legacy.end(), iv.begin(), iv.end());
    legacy.insert(legacy.end(), plaintext.begin(), plaintext.end());
    ctr_xcrypt(key, iv, legacy.data() + 24, plaintext.size());
    {
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(legacy.data()),
                  static_cast<std::streamsize>(legacy.size()));
    }

    // First open: reads the legacy container, migrates it (the fixture's people/reminders
    // come through intact).
    {
        auto m = memory::make_memory_engine();
        CHECK(m->open(dst) == Status::Ok);
        CHECK(!m->all_people().empty());
        m->close();  // dirty_ was set by the migration -> rewrites as v2 on close
    }

    // The on-disk file is now the v2 authenticated container, not the legacy one.
    auto migrated = read_bytes(dst);
    CHECK(starts_with_str(migrated, "ECHOAEM1"));
    CHECK(!starts_with_str(migrated, "ECHOAES1"));

    // And it is now genuinely authenticated: tampering the migrated file is rejected,
    // which the original legacy format never would have caught.
    {
        auto tampered = migrated;
        tampered.back() ^= 0x01;  // flip the last byte of the CMAC tag
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(tampered.data()),
                  static_cast<std::streamsize>(tampered.size()));
    }
    auto m2 = memory::make_memory_engine();
    CHECK(m2->open(dst) == Status::HardwareError);
}

// --- Phase 24: a file that is neither format fails closed, never silently empties ---
// This used to be treated as "empty" and silently overwritten with a fresh empty store on
// the very next save_to_disk() (which runs on every on-disk open, not gated on dirty_) — a
// real, pre-existing silent-data-loss bug. Refusing to open is the fix.
void test_unrecognized_file_fails_closed() {
    auto db = temp_db_path("garbage");
    std::vector<std::uint8_t> garbage = {'n', 'o', 't', ' ', 'a', ' ', 's', 't', 'o', 'r', 'e'};
    {
        std::ofstream out(db, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(garbage.data()),
                  static_cast<std::streamsize>(garbage.size()));
    }

    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::HardwareError);
    CHECK(!m->is_open());

    // The file itself must be untouched — not overwritten with an empty encrypted store.
    auto after = read_bytes(db);
    CHECK(after == garbage);
}

// --- Phase 16: bounded event log ----------------------------------------------
// After exceeding the cap, the oldest events are evicted and the newest retained —
// and a retained acknowledgment's reminder state is untouched.
void test_event_log_retention_bounded() {
    auto db = temp_db_path("retevents");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);

    // Oldest event first: naming Priya.
    auto id = m->remember_person("Priya", "your daughter", fixture_embedding(1), kNow);
    CHECK(id.is_ok());

    // A pile of PersonSeen events.
    for (int i = 0; i < 20; ++i)
        CHECK(m->mark_seen(id.value(), kNow + i * 60) == Status::Ok);

    // Newest events last: fire + acknowledge a reminder (so an ack event is retained).
    auto rem = m->add_reminder("take blood pressure medication", kNow, Recurrence::Once);
    CHECK(rem.is_ok());
    CHECK(m->mark_fired(rem.value(), kNow) == Status::Ok);
    CHECK(m->acknowledge_reminder(rem.value(), kNow) == Status::Ok);

    // Way more than the cap we're about to set.
    CHECK(m->recent_events(1000).size() > 5);

    // Cap to the newest 5, no age limit, notes untouched, then enforce (the tick).
    m->set_retention(memory::RetentionPolicy{/*events*/5, /*age*/0, /*notes*/0});
    m->enforce_retention(kNow + 100000);

    auto events = m->recent_events(1000);
    CHECK(events.size() == 5);

    // Oldest (the PersonNamed) was evicted; the newest survive.
    bool has_named = false, has_ack = false;
    for (const auto& e : events) {
        if (e.kind == memory::EventKind::PersonNamed)          has_named = true;
        if (e.kind == memory::EventKind::ReminderAcknowledged) has_ack = true;
    }
    CHECK(!has_named);   // evicted
    CHECK(has_ack);      // retained (it was among the newest)

    // Acknowledgment state for the retained records is intact: the Once reminder is
    // still acknowledged (closed), untouched by event-log pruning.
    bool reminder_acked = false;
    for (const auto& r : m->all_reminders())
        if (r.id == rem.value()) reminder_acked = r.acknowledged;
    CHECK(reminder_acked);
    CHECK(m->due_reminders(kNow).empty());   // closed, not resurfacing

    // Survives a reopen (persisted encrypted, still bounded).
    m->close();
    auto m2 = memory::make_memory_engine();
    CHECK(m2->open(db) == Status::Ok);
    CHECK(m2->recent_events(1000).size() == 5);
    m2->close();
}

// --- Phase 16: bounded person notes -------------------------------------------
// Notes are capped by count with oldest-first eviction (never pruned by age).
void test_notes_growth_bounded() {
    auto db = temp_db_path("retnotes");
    auto m = memory::make_memory_engine();
    CHECK(m->open(db) == Status::Ok);

    // Generous cap while adding, so all six notes accumulate.
    m->set_retention(memory::RetentionPolicy{2000, 90, 100});
    auto id = m->remember_person("Sam", "your neighbor", fixture_embedding(7), kNow);
    CHECK(id.is_ok());
    const char* notes[] = {"note-one", "note-two", "note-three",
                           "note-four", "note-five", "note-six"};
    for (const char* n : notes) CHECK(m->add_note(id.value(), n) == Status::Ok);
    CHECK(m->get_person(id.value())->notes.find("note-one") != std::string::npos);

    // Tighten to the newest 2 and enforce on the (retention) tick.
    m->set_retention(memory::RetentionPolicy{2000, 90, 2});
    m->enforce_retention(kNow);

    auto p = m->get_person(id.value());
    CHECK(p.has_value());
    CHECK(p->notes.find("note-five") != std::string::npos);   // retained (newest)
    CHECK(p->notes.find("note-six")  != std::string::npos);   // retained (newest)
    CHECK(p->notes.find("note-one")   == std::string::npos);  // evicted (oldest)
    CHECK(p->notes.find("note-four")  == std::string::npos);  // evicted

    // The immediate per-add cap also bounds growth without waiting for a tick.
    auto id2 = m->remember_person("Jo", "your friend", fixture_embedding(9), kNow);
    for (int i = 0; i < 10; ++i) CHECK(m->add_note(id2.value(), "x" + std::to_string(i)) == Status::Ok);
    auto p2 = m->get_person(id2.value());
    // cap is 2 -> only the last two "x8; x9" remain.
    CHECK(p2->notes.find("x9") != std::string::npos);
    CHECK(p2->notes.find("x8") != std::string::npos);
    CHECK(p2->notes.find("x0") == std::string::npos);

    m->close();
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

// --- Phase 22: the caregiver digest, held to the same rule -------------------
//
// This is the file that matters most for the digest, because this is the only
// translation unit in the project that can name BOTH the store's raw content and
// companion-sync's send paths. companion-sync itself cannot name memory::Embedding
// or memory::PersonRecord at all — it does not link echo::memory, on purpose — so
// the assertions over there are about shapes (vectors, strings, buffers). Here they
// are about the actual types, and that is a stronger claim.
//
// A caregiver digest is periodic, scheduled, and read by a named person. Of every
// payload this device can emit, it is the one where "just add the name, it would be
// so much more useful" is most likely to be proposed, and most reasonable-sounding.
// The answer is in these assertions rather than in anyone's judgement.
using companion::CaregiverDigest;
using companion::InboundCommand;

template <class... Args>
inline constexpr bool digest_invocable_with =
    std::is_invocable_v<decltype(&ICompanionSync::send_digest), ICompanionSync&, Args...>;

// The digest send path accepts NONE of the store's raw content.
static_assert(!digest_invocable_with<memory::Embedding>,
              "PRIVACY: no companion send path may accept a face embedding");
static_assert(!digest_invocable_with<const memory::Embedding&>,
              "PRIVACY: no companion send path may accept a face embedding");
static_assert(!digest_invocable_with<memory::PersonRecord>,
              "PRIVACY: no companion send path may accept a person record");
static_assert(!digest_invocable_with<const memory::PersonRecord&>,
              "PRIVACY: no companion send path may accept a person record");
static_assert(!digest_invocable_with<memory::EventRecord>,
              "PRIVACY: no companion send path may accept an event record");
static_assert(!digest_invocable_with<const memory::EventRecord&>,
              "PRIVACY: no companion send path may accept an event record");
static_assert(!digest_invocable_with<memory::ReminderRecord>,
              "PRIVACY: no companion send path may accept a reminder record");

// And a digest cannot be MADE from any of them. Counts about a person's day are
// derived inside the memory engine and handed over as integers; the record itself
// never crosses the module boundary, so there is no assembly step where a name
// could be picked up along the way.
static_assert(!std::is_constructible_v<CaregiverDigest, memory::Embedding>,
              "PRIVACY: a CaregiverDigest must not be constructible from a face embedding");
static_assert(!std::is_constructible_v<CaregiverDigest, memory::PersonRecord>,
              "PRIVACY: a CaregiverDigest must not be constructible from a person record");
static_assert(!std::is_constructible_v<CaregiverDigest, memory::EventRecord>,
              "PRIVACY: a CaregiverDigest must not be constructible from an event record");
static_assert(!std::is_constructible_v<CaregiverDigest, memory::ReminderRecord>,
              "PRIVACY: a CaregiverDigest must not be constructible from a reminder record");

// THE NAMING RULE (Phase 15), as a compile-time fact. A face is bound to a name
// only by the wearer, on-device, in the moment. An inbound caregiver command has
// nowhere to put an embedding — not a field that gets validated and rejected at
// run time, but no field at all — so "no inbound command may ever create a person
// from an embedding" is a property of the type, not a property of the code that
// happens to handle it today.
static_assert(!std::is_constructible_v<InboundCommand, memory::Embedding>,
              "PRIVACY: an InboundCommand must not be constructible from a face embedding");
static_assert(!std::is_constructible_v<InboundCommand, memory::PersonRecord>,
              "PRIVACY: an InboundCommand must not be constructible from a person record");

// The consent record is the one place the store holds a permission rather than a
// memory. It is scalars and enums for the same reason the digest is: whatever this
// grows into, it must never become somewhere to stash a caregiver's details.
static_assert(std::is_trivially_copyable_v<memory::ConsentRecord>,
              "PRIVACY: a ConsentRecord must be a flat bag of scalars");
static_assert(std::is_enum_v<decltype(memory::ConsentRecord::scope)>,
              "PRIVACY: consent scope must be an enum, never free text");
static_assert(std::is_enum_v<decltype(memory::ConsentRecord::grantor)>,
              "PRIVACY: consent grantor must be an enum — WHO granted it is a role, "
              "not a name; recording a caregiver's identity here would put a second "
              "person's data on the wearer's device without their consent");

void test_memory_content_cannot_reach_sync_path() {
    CHECK((!alert_invocable_with<memory::PersonRecord>));
    CHECK((!alert_invocable_with<memory::Embedding>));
    CHECK((!std::is_constructible_v<Alert, memory::EventRecord>));
    // Phase 22: the same three questions, asked of the digest path.
    CHECK((!digest_invocable_with<memory::PersonRecord>));
    CHECK((!digest_invocable_with<memory::Embedding>));
    CHECK((!std::is_constructible_v<CaregiverDigest, memory::EventRecord>));
    CHECK((!std::is_constructible_v<InboundCommand, memory::Embedding>));
    std::printf("[memory] structural privacy guarantee holds: the memory store's raw "
                "content (embeddings, person records, events) has no path to "
                "companion-sync — not through alerts, not through status, and not "
                "through the Phase-22 caregiver digest (enforced at compile time)\n");
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
    // Phase 16 — encryption at rest + retention.
    test_aes_known_answer_vectors();
    test_encryption_at_rest();
    test_migration_from_plaintext();
    // Phase 24 — authenticated encryption at rest.
    test_tampered_ciphertext_rejected();
    test_migration_from_v1_unauthenticated();
    test_unrecognized_file_fails_closed();
    test_event_log_retention_bounded();
    test_notes_growth_bounded();
    test_memory_content_cannot_reach_sync_path();
    return echo::test::report("memory-engine");
}
