// ECHO OS — memory & recall engine implementation (SQLite-backed, encrypted at rest).
//
// The store is SQLite (ADR-13): real ACID logic, a decades-hardened core, and a
// self-contained vendored amalgamation that keeps the dependency-free stub build
// green. Phase 16 changes ONE thing about how it lives on disk: the working database
// is held in an in-memory SQLite connection (loaded via sqlite3_deserialize on open),
// and its serialized image is written to disk as AES-256-CTR ciphertext (ADR-14).
// The plaintext SQLite image therefore never touches storage — it exists only in
// process RAM — while all the SQL logic below is byte-for-byte the Phase-15 logic,
// unchanged, because it runs against an ordinary SQLite connection either way.
//
// The honest trade-offs of this "encrypt the serialized image" approach vs. a
// page-level SQLCipher codec (durability is at checkpoint, not per-transaction; the
// whole DB is resident in RAM) are documented in docs/DECISIONS.md (ADR-14). They are
// acceptable for a wearable's small people/reminders/events store and are the reason
// Part 2 bounds that store's growth.
#include "echo/memory/memory_engine.hpp"

#include "echo/memory/aes256.hpp"
#include "echo/memory/device_key.hpp"

#include "echo/log.hpp"

#include "sqlite3.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace echo::memory {

const char* to_string(Recurrence r) noexcept {
    switch (r) {
        case Recurrence::Once:   return "once";
        case Recurrence::Daily:  return "daily";
        case Recurrence::Weekly: return "weekly";
    }
    return "once";
}

const char* to_string(EventKind k) noexcept {
    switch (k) {
        case EventKind::PersonNamed:          return "person-named";
        case EventKind::PersonSeen:           return "person-seen";
        case EventKind::ReminderFired:        return "reminder-fired";
        case EventKind::ReminderAcknowledged: return "reminder-acknowledged";
        case EventKind::ReminderMissed:       return "reminder-missed";
    }
    return "unknown";
}

UnixTime unix_now() noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

float cosine_similarity(const Embedding& a, const Embedding& b) noexcept {
    if (a.empty() || a.size() != b.size()) return 0.0f;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na  += static_cast<double>(a[i]) * a[i];
        nb  += static_cast<double>(b[i]) * b[i];
    }
    if (na <= 0.0 || nb <= 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

namespace {

// Small RAII wrapper around a prepared statement so every query path is exception-
// free and leak-free without a web of manual finalize() calls.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK) st_ = nullptr;
    }
    ~Stmt() { if (st_) sqlite3_finalize(st_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    explicit operator bool() const noexcept { return st_ != nullptr; }
    sqlite3_stmt* get() const noexcept { return st_; }

    // SQLITE_TRANSIENT is an upstream sentinel macro (a -1 cast to a destructor
    // pointer); the int-to-ptr cast is SQLite's API, not our code, hence NOLINT.
    void bind_text(int i, const std::string& v) {
        sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT);  // NOLINT(performance-no-int-to-ptr)
    }
    void bind_int(int i, std::int64_t v) { sqlite3_bind_int64(st_, i, v); }
    void bind_blob(int i, const Embedding& v) {
        if (v.empty()) { sqlite3_bind_null(st_, i); return; }
        sqlite3_bind_blob(st_, i, v.data(), static_cast<int>(v.size() * sizeof(float)),
                          SQLITE_TRANSIENT);  // NOLINT(performance-no-int-to-ptr)
    }
    bool step_row() { return sqlite3_step(st_) == SQLITE_ROW; }
    bool step_done() { return sqlite3_step(st_) == SQLITE_DONE; }

    std::string col_text(int i) {
        const unsigned char* p = sqlite3_column_text(st_, i);
        return p ? reinterpret_cast<const char*>(p) : std::string{};
    }
    std::int64_t col_int(int i) { return sqlite3_column_int64(st_, i); }
    Embedding col_blob(int i) {
        const void* p = sqlite3_column_blob(st_, i);
        const int   n = sqlite3_column_bytes(st_, i);
        Embedding out;
        if (p && n >= static_cast<int>(sizeof(float))) {
            out.resize(static_cast<std::size_t>(n) / sizeof(float));
            std::memcpy(out.data(), p, out.size() * sizeof(float));
        }
        return out;
    }

private:
    sqlite3_stmt* st_ = nullptr;
};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "9:07am" style local clock label for an event time, for caregiver-readable
// answers. Local time is deliberate: these are civil facts for a human.
std::string clock_label(UnixTime t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    (void)localtime_s(&tm, &tt);
#else
    (void)localtime_r(&tt, &tm);
#endif
    int h = tm.tm_hour, m = tm.tm_min;
    const char* ap = h < 12 ? "am" : "pm";
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    char buf[16];
    (void)std::snprintf(buf, sizeof(buf), "%d:%02d%s", h12, m, ap);
    return buf;
}

// Start of the local civil day containing `now`, as Unix seconds.
UnixTime start_of_local_day(UnixTime now) {
    std::time_t tt = static_cast<std::time_t>(now);
    std::tm tm{};
#if defined(_WIN32)
    (void)localtime_s(&tm, &tt);
#else
    (void)localtime_r(&tt, &tm);
#endif
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
    return static_cast<UnixTime>(std::mktime(&tm));
}

// "earlier today" / "yesterday" / "N days ago" for a last-seen gap.
std::string elapsed_phrase(UnixTime last_seen, UnixTime now) {
    if (last_seen <= 0) return "for the first time";
    const long long days = (start_of_local_day(now) - start_of_local_day(last_seen)) / 86400;
    if (days <= 0) return "earlier today";
    if (days == 1) return "yesterday";
    char buf[48];
    (void)std::snprintf(buf, sizeof(buf), "%lld days ago", days);
    return buf;
}

// Pick a pronoun from the relationship phrase so recall reads naturally. Falls back
// to the neutral "them" whenever the relationship is unknown or ungendered.
const char* pronoun_for(const std::string& relation) {
    const std::string r = lower(relation);
    auto has = [&](const char* w) { return r.find(w) != std::string::npos; };
    if (has("daughter") || has("mother") || has("sister") || has("wife") ||
        has("aunt") || has("grandmother") || has("niece"))
        return "her";
    if (has("son") || has("father") || has("brother") || has("husband") ||
        has("uncle") || has("grandfather") || has("nephew"))
        return "him";
    return "them";
}

// --- On-disk encrypted image format ------------------------------------------
// Layout:  [8-byte magic "ECHOAES1"][16-byte random IV][AES-256-CTR ciphertext].
// The ciphertext is the raw SQLite serialized image, so a plaintext SQLite file
// (which begins "SQLite format 3\0") is trivially distinguishable from ours, and a
// plain unkeyed sqlite3_open of our file sees ciphertext -> "not a database".
constexpr char             kEncMagic[8]  = {'E','C','H','O','A','E','S','1'};
constexpr std::string_view kSqliteMagic  = "SQLite format 3";  // first 15 bytes of any SQLite db

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

bool starts_with(const std::vector<std::uint8_t>& b, const char* magic, std::size_t n) {
    if (b.size() < n) return false;
    return std::memcmp(b.data(), magic, n) == 0;
}

// Encrypt a serialized SQLite image into our on-disk container (magic|IV|ciphertext).
std::vector<std::uint8_t> encrypt_image(const std::uint8_t* image, std::size_t len,
                                        const crypto::Key256& key) {
    crypto::Block iv{};
    std::random_device rd;
    std::uniform_int_distribution<int> byte(0, 255);
    for (auto& b : iv) b = static_cast<std::uint8_t>(byte(rd));

    std::vector<std::uint8_t> out;
    out.reserve(sizeof(kEncMagic) + iv.size() + len);
    out.insert(out.end(), kEncMagic, kEncMagic + sizeof(kEncMagic));
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), image, image + len);
    crypto::ctr_xcrypt(key, iv, out.data() + sizeof(kEncMagic) + iv.size(), len);
    return out;
}

// Decrypt our container back to a plaintext SQLite image. Returns empty on a wrong
// key / corruption: the decrypted bytes must begin with the SQLite magic, which is
// our (non-cryptographic, honestly-scoped) integrity sanity check (ADR-14).
std::vector<std::uint8_t> decrypt_image(const std::vector<std::uint8_t>& file,
                                        const crypto::Key256& key) {
    const std::size_t header = sizeof(kEncMagic) + 16;
    if (file.size() < header || !starts_with(file, kEncMagic, sizeof(kEncMagic))) return {};
    crypto::Block iv{};
    std::memcpy(iv.data(), file.data() + sizeof(kEncMagic), iv.size());
    std::vector<std::uint8_t> image(file.begin() + static_cast<std::ptrdiff_t>(header), file.end());
    crypto::ctr_xcrypt(key, iv, image.data(), image.size());
    if (image.size() < kSqliteMagic.size() ||
        std::memcmp(image.data(), kSqliteMagic.data(), kSqliteMagic.size()) != 0) {
        return {};  // wrong key or corrupt -> fail closed, never deserialize garbage
    }
    return image;
}

class SqliteMemory final : public IMemoryEngine {
public:
    Status open(const std::string& db_path) override {
        close();
        db_path_ = db_path;
        const bool on_disk = (db_path != ":memory:" && !db_path.empty());

        // The working connection is ALWAYS an in-memory SQLite db. On disk we keep
        // only the encrypted serialized image; plaintext never lands on storage.
        if (sqlite3_open(":memory:", &db_) != SQLITE_OK) {
            log_error("memory", "could not open in-memory store; memory disabled");
            if (db_) { sqlite3_close(db_); db_ = nullptr; }
            return Status::HardwareError;
        }
        exec("PRAGMA foreign_keys=ON;");

        load_retention_env();

        if (on_disk) {
            key_path_ = key_path_for(db_path);
            auto key = load_or_create_device_key(key_path_);
            if (!key) {
                log_error("memory", "no encryption key; refusing to open store");
                close();
                return Status::HardwareError;
            }
            key_ = *key;
            have_key_ = true;
            if (!load_from_disk(db_path)) {  // wrong key / unreadable ciphertext
                log_error("memory", "store present but undecryptable; memory disabled");
                close();
                return Status::HardwareError;
            }
        }

        // Ensure the schema exists whether the image was empty, migrated, or fresh.
        if (!create_schema()) {
            log_error("memory", "schema init failed; memory disabled");
            close();
            return Status::HardwareError;
        }

        if (on_disk) save_to_disk();  // materialize the encrypted file on first boot / after migration
        log_info("memory", "store ready (encrypted at rest)");
        return Status::Ok;
    }

    bool is_open() const noexcept override { return db_ != nullptr; }

    void close() override {
        if (db_) {
            if (!db_path_.empty() && db_path_ != ":memory:" && have_key_ && dirty_) save_to_disk();
            sqlite3_close(db_);
            db_ = nullptr;
        }
        have_key_ = false;
        dirty_ = false;
        db_path_.clear();
    }

    ~SqliteMemory() override { close(); }

    // --- Retention ----------------------------------------------------------

    void set_retention(const RetentionPolicy& policy) override { retention_ = policy; }

    void enforce_retention(UnixTime now) override {
        if (!db_) return;
        prune_events(now);
        prune_all_notes();
        if (have_key_ && dirty_) save_to_disk();
    }

    // --- People -------------------------------------------------------------

    PersonMatch recognize(const Embedding& embedding) override {
        PersonMatch best;
        if (!db_ || embedding.empty()) return best;
        Stmt q(db_, "SELECT id,name,relation,notes,embedding,last_seen FROM people;");
        if (!q) return best;
        while (q.step_row()) {
            Embedding e = q.col_blob(4);
            const float sim = cosine_similarity(embedding, e);
            if (sim > best.similarity) {
                best.similarity = sim;
                if (sim >= kMatchThreshold) {
                    best.matched = true;
                    best.person = read_person(q);
                }
            }
        }
        if (!best.matched) best.person = PersonRecord{};  // don't leak a near-miss
        return best;
    }

    Result<PersonId> remember_person(const std::string& name, const std::string& relation,
                                     const Embedding& embedding, UnixTime now) override {
        if (!db_) return Result<PersonId>::fail(Status::NotReady);
        Stmt ins(db_, "INSERT INTO people(name,relation,notes,embedding,last_seen) "
                      "VALUES(?,?,?,?,?);");
        if (!ins) return Result<PersonId>::fail(Status::HardwareError);
        ins.bind_text(1, name);
        ins.bind_text(2, relation);
        ins.bind_text(3, "");
        ins.bind_blob(4, embedding);
        ins.bind_int(5, now);
        if (!ins.step_done()) return Result<PersonId>::fail(Status::HardwareError);
        const PersonId id = sqlite3_last_insert_rowid(db_);

        EventRecord ev;
        ev.kind = EventKind::PersonNamed;
        ev.at = now;
        ev.person_id = id;
        ev.summary = "Named " + name + (relation.empty() ? "" : " (" + relation + ")");
        log_event(ev);
        return Result<PersonId>::ok(id);
    }

    Status mark_seen(PersonId id, UnixTime now) override {
        if (!db_) return Status::NotReady;
        Stmt up(db_, "UPDATE people SET last_seen=? WHERE id=?;");
        if (!up) return Status::HardwareError;
        up.bind_int(1, now); up.bind_int(2, id);
        if (!up.step_done()) return Status::HardwareError;

        auto p = get_person(id);
        EventRecord ev;
        ev.kind = EventKind::PersonSeen;
        ev.at = now;
        ev.person_id = id;
        ev.summary = "Saw " + (p ? p->name : std::string("someone known"));
        log_event(ev);
        return Status::Ok;
    }

    Status add_note(PersonId id, const std::string& note) override {
        if (!db_) return Status::NotReady;
        touch();
        auto p = get_person(id);
        if (!p) return Status::Unavailable;
        std::string merged = p->notes;
        if (!merged.empty() && !note.empty()) merged += "; ";
        merged += note;
        // Bound growth immediately, not only on the retention tick (Phase 16, Part 2):
        // keep the newest N segments so one person's notes can't grow without limit.
        merged = cap_notes(merged, retention_.max_notes_per_person);
        Stmt up(db_, "UPDATE people SET notes=? WHERE id=?;");
        if (!up) return Status::HardwareError;
        up.bind_text(1, merged); up.bind_int(2, id);
        return up.step_done() ? Status::Ok : Status::HardwareError;
    }

    std::optional<PersonRecord> get_person(PersonId id) override {
        if (!db_) return std::nullopt;
        Stmt q(db_, "SELECT id,name,relation,notes,embedding,last_seen FROM people WHERE id=?;");
        if (!q) return std::nullopt;
        q.bind_int(1, id);
        if (!q.step_row()) return std::nullopt;
        return read_person(q);
    }

    std::vector<PersonRecord> all_people() override {
        std::vector<PersonRecord> out;
        if (!db_) return out;
        Stmt q(db_, "SELECT id,name,relation,notes,embedding,last_seen FROM people ORDER BY id;");
        if (!q) return out;
        while (q.step_row()) out.push_back(read_person(q));
        return out;
    }

    // --- Reminders ----------------------------------------------------------

    Result<ReminderId> add_reminder(const std::string& text, UnixTime due,
                                    Recurrence recurrence) override {
        if (!db_) return Result<ReminderId>::fail(Status::NotReady);
        touch();
        Stmt ins(db_, "INSERT INTO reminders(text,due,recurrence,acknowledged,fired) "
                      "VALUES(?,?,?,0,0);");
        if (!ins) return Result<ReminderId>::fail(Status::HardwareError);
        ins.bind_text(1, text);
        ins.bind_int(2, due);
        ins.bind_int(3, static_cast<std::int64_t>(recurrence));
        if (!ins.step_done()) return Result<ReminderId>::fail(Status::HardwareError);
        return Result<ReminderId>::ok(sqlite3_last_insert_rowid(db_));
    }

    std::vector<ReminderRecord> due_reminders(UnixTime now) override {
        return query_reminders("SELECT id,text,due,recurrence,acknowledged FROM reminders "
                               "WHERE acknowledged=0 AND due<=? ORDER BY due;", now);
    }

    std::vector<ReminderRecord> pending_deliveries(UnixTime now) override {
        return query_reminders("SELECT id,text,due,recurrence,acknowledged FROM reminders "
                               "WHERE acknowledged=0 AND due<=? AND fired<due ORDER BY due;", now);
    }

    Status mark_fired(ReminderId id, UnixTime now) override {
        if (!db_) return Status::NotReady;
        Stmt up(db_, "UPDATE reminders SET fired=? WHERE id=?;");
        if (!up) return Status::HardwareError;
        up.bind_int(1, now); up.bind_int(2, id);
        if (!up.step_done()) return Status::HardwareError;
        auto r = get_reminder(id);
        EventRecord ev;
        ev.kind = EventKind::ReminderFired;
        ev.at = now;
        ev.reminder_id = id;
        ev.summary = "Reminded: " + (r ? r->text : std::string("a reminder"));
        log_event(ev);
        return Status::Ok;
    }

    Status acknowledge_reminder(ReminderId id, UnixTime now) override {
        if (!db_) return Status::NotReady;
        auto r = get_reminder(id);
        if (!r) return Status::Unavailable;

        if (r->recurrence == Recurrence::Once) {
            Stmt up(db_, "UPDATE reminders SET acknowledged=1 WHERE id=?;");
            if (!up) return Status::HardwareError;
            up.bind_int(1, id);
            if (!up.step_done()) return Status::HardwareError;
        } else {
            // Recurring: advance to the next occurrence and re-arm (unfired).
            const UnixTime step = (r->recurrence == Recurrence::Daily) ? 86400 : 7 * 86400;
            UnixTime next = r->due;
            // Advance past `now` so an ack always lands the next occurrence ahead.
            do { next += step; } while (next <= now);
            Stmt up(db_, "UPDATE reminders SET due=?, fired=0, acknowledged=0 WHERE id=?;");
            if (!up) return Status::HardwareError;
            up.bind_int(1, next); up.bind_int(2, id);
            if (!up.step_done()) return Status::HardwareError;
        }

        EventRecord ev;
        ev.kind = EventKind::ReminderAcknowledged;
        ev.at = now;
        ev.reminder_id = id;
        ev.summary = "Confirmed: " + r->text;
        log_event(ev);
        return Status::Ok;
    }

    std::vector<ReminderRecord> all_reminders() override {
        return query_reminders("SELECT id,text,due,recurrence,acknowledged FROM reminders "
                               "ORDER BY due;", /*now unused*/ 0, /*bind_now=*/false);
    }

    // --- Memory-query -------------------------------------------------------

    std::string answer_query(const std::string& question, UnixTime now) override {
        if (!db_) return {};
        const std::string q = lower(question);

        // Medication: "did I take my medication/meds/pills today?"
        static const char* kMedWords[] = {"medication", "medicine", "meds", "pill", "pills", "tablet"};
        bool asks_med = false;
        for (const char* w : kMedWords) if (q.find(w) != std::string::npos) asks_med = true;
        if (asks_med) {
            std::string answer = medication_answer(now);
            if (!answer.empty()) return answer;
        }

        // Generic: "what reminders do I have / what's next?"
        if (q.find("remind") != std::string::npos || q.find("what's next") != std::string::npos ||
            q.find("what is next") != std::string::npos || q.find("supposed to") != std::string::npos) {
            auto up = due_reminders(now);
            if (up.empty()) {
                auto all = all_reminders();
                if (all.empty()) return "You don't have any reminders set right now.";
                return "Nothing is due right now. Your next reminder is to " + all.front().text + ".";
            }
            return "Right now you're due to " + up.front().text + ".";
        }

        return {};  // nothing concrete -> caller falls back rather than fabricate
    }

    // --- Event log ----------------------------------------------------------

    Status log_event(const EventRecord& ev) override {
        if (!db_) return Status::NotReady;
        touch();
        Stmt ins(db_, "INSERT INTO events(kind,at,summary,person_id,reminder_id) "
                      "VALUES(?,?,?,?,?);");
        if (!ins) return Status::HardwareError;
        ins.bind_int(1, static_cast<std::int64_t>(ev.kind));
        ins.bind_int(2, ev.at);
        ins.bind_text(3, ev.summary);
        ins.bind_int(4, ev.person_id);
        ins.bind_int(5, ev.reminder_id);
        return ins.step_done() ? Status::Ok : Status::HardwareError;
    }

    std::vector<EventRecord> recent_events(int limit) override {
        std::vector<EventRecord> out;
        if (!db_) return out;
        Stmt q(db_, "SELECT id,kind,at,summary,person_id,reminder_id FROM events "
                    "ORDER BY id DESC LIMIT ?;");
        if (!q) return out;
        q.bind_int(1, limit > 0 ? limit : 50);
        while (q.step_row()) {
            EventRecord e;
            e.id = q.col_int(0);
            e.kind = static_cast<EventKind>(q.col_int(1));
            e.at = q.col_int(2);
            e.summary = q.col_text(3);
            e.person_id = q.col_int(4);
            e.reminder_id = q.col_int(5);
            out.push_back(std::move(e));
        }
        return out;
    }

private:
    static PersonRecord read_person(Stmt& q) {
        PersonRecord p;
        p.id = q.col_int(0);
        p.name = q.col_text(1);
        p.relation = q.col_text(2);
        p.notes = q.col_text(3);
        p.embedding = q.col_blob(4);
        p.last_seen = q.col_int(5);
        return p;
    }

    std::optional<ReminderRecord> get_reminder(ReminderId id) {
        Stmt q(db_, "SELECT id,text,due,recurrence,acknowledged FROM reminders WHERE id=?;");
        if (!q) return std::nullopt;
        q.bind_int(1, id);
        if (!q.step_row()) return std::nullopt;
        return read_reminder(q);
    }

    static ReminderRecord read_reminder(Stmt& q) {
        ReminderRecord r;
        r.id = q.col_int(0);
        r.text = q.col_text(1);
        r.due = q.col_int(2);
        r.recurrence = static_cast<Recurrence>(q.col_int(3));
        r.acknowledged = q.col_int(4) != 0;
        return r;
    }

    std::vector<ReminderRecord> query_reminders(const char* sql, UnixTime now,
                                                bool bind_now = true) {
        std::vector<ReminderRecord> out;
        if (!db_) return out;
        Stmt q(db_, sql);
        if (!q) return out;
        if (bind_now) q.bind_int(1, now);
        while (q.step_row()) out.push_back(read_reminder(q));
        return out;
    }

    // "did I take my medication today" answered from the REAL log, never guessed.
    std::string medication_answer(UnixTime now) {
        const UnixTime day_start = start_of_local_day(now);
        // Find a med-ish reminder.
        ReminderRecord med;
        bool found = false;
        for (const auto& r : all_reminders()) {
            const std::string lt = lower(r.text);
            if (lt.find("medic") != std::string::npos || lt.find("med") != std::string::npos ||
                lt.find("pill") != std::string::npos || lt.find("tablet") != std::string::npos) {
                med = r; found = true; break;
            }
        }
        if (!found) return {};

        // Was it acknowledged today? Read the append-only log — the ground truth.
        Stmt q(db_, "SELECT at FROM events WHERE kind=? AND reminder_id=? AND at>=? "
                    "ORDER BY at DESC LIMIT 1;");
        if (q) {
            q.bind_int(1, static_cast<std::int64_t>(EventKind::ReminderAcknowledged));
            q.bind_int(2, med.id);
            q.bind_int(3, day_start);
            if (q.step_row()) {
                return "Yes — you took your " + med.text + " at " + clock_label(q.col_int(0)) + ".";
            }
        }
        return "Not yet today — your reminder to " + med.text + " is still waiting.";
    }

    bool exec(const char* sql) {
        char* err = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        return rc == SQLITE_OK;
    }

    bool create_schema() {
        return exec(
            "CREATE TABLE IF NOT EXISTS people("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL DEFAULT '',"
            "  relation TEXT NOT NULL DEFAULT '',"
            "  notes TEXT NOT NULL DEFAULT '',"
            "  embedding BLOB,"
            "  last_seen INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS reminders("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  text TEXT NOT NULL,"
            "  due INTEGER NOT NULL DEFAULT 0,"
            "  recurrence INTEGER NOT NULL DEFAULT 0,"
            "  acknowledged INTEGER NOT NULL DEFAULT 0,"
            "  fired INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS events("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  kind INTEGER NOT NULL,"
            "  at INTEGER NOT NULL DEFAULT 0,"
            "  summary TEXT NOT NULL DEFAULT '',"
            "  person_id INTEGER NOT NULL DEFAULT 0,"
            "  reminder_id INTEGER NOT NULL DEFAULT 0);");
    }

    // Owner-only permissions on the *encrypted* file. Defense-in-depth on top of
    // encryption (not a substitute for it). Best-effort; Windows ACLs still apply.
    static void restrict_permissions(const std::string& path) {
        if (path == ":memory:" || path.empty()) return;
        std::error_code ec;
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        if (ec) log_warn("memory", "could not restrict store file permissions");
    }

    void touch() noexcept { dirty_ = true; }

    // Load the on-disk store into the in-memory connection. Handles three cases:
    //   * our encrypted container -> decrypt + deserialize;
    //   * a plaintext Phase-15 SQLite file -> MIGRATE (deserialize as-is, then the
    //     caller's save_to_disk rewrites it encrypted);
    //   * missing/empty -> fresh store.
    // Returns false only when an encrypted file cannot be decrypted (wrong key).
    bool load_from_disk(const std::string& path) {
        auto bytes = read_file_bytes(path);
        if (bytes.empty()) return true;  // first boot: nothing to load

        std::vector<std::uint8_t> image;
        if (starts_with(bytes, kEncMagic, sizeof(kEncMagic))) {
            image = decrypt_image(bytes, key_);
            if (image.empty()) return false;  // wrong key / corrupt
        } else if (bytes.size() >= kSqliteMagic.size() &&
                   std::memcmp(bytes.data(), kSqliteMagic.data(), kSqliteMagic.size()) == 0) {
            // One-time migration of an unencrypted Phase-15 database.
            log_warn("memory", "found UNENCRYPTED Phase-15 store; migrating to encrypted format");
            image = std::move(bytes);
            migrated_ = true;
            dirty_ = true;  // force an encrypted rewrite
        } else {
            log_error("memory", "store file unrecognized (neither encrypted nor SQLite); ignoring");
            return true;  // treat as empty rather than clobber-refuse to boot
        }

        return deserialize_image(image);
    }

    // Copy an image into a sqlite-owned buffer and deserialize it into db_.
    bool deserialize_image(const std::vector<std::uint8_t>& image) {
        void* buf = sqlite3_malloc64(static_cast<sqlite3_uint64>(image.size()));
        if (!buf) return false;
        std::memcpy(buf, image.data(), image.size());
        const int rc = sqlite3_deserialize(
            db_, "main", static_cast<unsigned char*>(buf),
            static_cast<sqlite3_int64>(image.size()), static_cast<sqlite3_int64>(image.size()),
            SQLITE_DESERIALIZE_RESIZEABLE | SQLITE_DESERIALIZE_FREEONCLOSE);
        if (rc != SQLITE_OK) {
            log_error("memory", "could not deserialize store image");
            return false;
        }
        return true;
    }

    // Serialize the in-memory db, encrypt it, and atomically replace the on-disk file.
    void save_to_disk() {
        if (!db_ || db_path_.empty() || db_path_ == ":memory:" || !have_key_) return;
        sqlite3_int64 n = 0;
        unsigned char* image = sqlite3_serialize(db_, "main", &n, 0);
        if (!image || n <= 0) {
            if (image) sqlite3_free(image);
            log_error("memory", "could not serialize store; on-disk copy not updated");
            return;
        }
        auto enc = encrypt_image(image, static_cast<std::size_t>(n), key_);
        sqlite3_free(image);

        // Atomic-ish replace: write a temp then rename over the target.
        const std::string tmp = db_path_ + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) { log_error("memory", "could not write encrypted store temp"); return; }
            out.write(reinterpret_cast<const char*>(enc.data()),
                      static_cast<std::streamsize>(enc.size()));
            if (!out) { log_error("memory", "short write on encrypted store temp"); return; }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, db_path_, ec);
        if (ec) {  // some filesystems refuse cross-handle rename onto an existing file
            std::filesystem::remove(db_path_, ec);
            std::filesystem::rename(tmp, db_path_, ec);
        }
        if (ec) { log_error("memory", "could not replace encrypted store file"); return; }
        restrict_permissions(db_path_);
        dirty_ = false;
    }

    // ECHO_MEMORY_MAX_EVENTS / _EVENT_AGE_DAYS / _NOTES override the defaults.
    void load_retention_env() {
        auto envi = [](const char* k, int fallback) {
            const char* v = std::getenv(k);
            if (!v || !*v) return fallback;
            char* end = nullptr;
            const long parsed = std::strtol(v, &end, 10);
            return (end && *end == '\0' && parsed >= 0) ? static_cast<int>(parsed) : fallback;
        };
        retention_.max_events           = envi("ECHO_MEMORY_MAX_EVENTS", retention_.max_events);
        retention_.max_event_age_days   = envi("ECHO_MEMORY_MAX_EVENT_AGE_DAYS", retention_.max_event_age_days);
        retention_.max_notes_per_person = envi("ECHO_MEMORY_MAX_NOTES", retention_.max_notes_per_person);
    }

    // Bound the append-only event log by age then by count. Pruning only touches the
    // events table; a reminder's acknowledged/fired state lives in the reminders
    // table and is untouched, so retained records keep their exact status.
    void prune_events(UnixTime now) {
        if (retention_.max_event_age_days > 0) {
            const UnixTime cutoff = now - static_cast<UnixTime>(retention_.max_event_age_days) * 86400;
            Stmt del(db_, "DELETE FROM events WHERE at < ?;");
            if (del) { del.bind_int(1, cutoff); if (del.step_done()) touch(); }
        }
        if (retention_.max_events > 0) {
            // Keep the newest N by id; delete the rest.
            Stmt del(db_, "DELETE FROM events WHERE id NOT IN "
                          "(SELECT id FROM events ORDER BY id DESC LIMIT ?);");
            if (del) { del.bind_int(1, retention_.max_events); if (del.step_done()) touch(); }
        }
    }

    // Bound each person's notes to the newest N "; "-separated segments (oldest-first
    // eviction). Notes are NOT pruned by age — they are the long-term value of the
    // store — only capped so a single person's notes can't grow without bound.
    void prune_all_notes() {
        const int cap = retention_.max_notes_per_person;
        if (cap <= 0) return;
        for (const auto& p : all_people()) {
            const std::string capped = cap_notes(p.notes, cap);
            if (capped != p.notes) {
                Stmt up(db_, "UPDATE people SET notes=? WHERE id=?;");
                if (up) { up.bind_text(1, capped); up.bind_int(2, p.id);
                          if (up.step_done()) touch(); }
            }
        }
    }

    // Keep the last `cap` "; "-separated segments of `notes`.
    static std::string cap_notes(const std::string& notes, int cap) {
        if (cap <= 0 || notes.empty()) return notes;
        std::vector<std::string> segs;
        std::size_t pos = 0;
        while (pos <= notes.size()) {
            const std::size_t next = notes.find("; ", pos);
            if (next == std::string::npos) { segs.push_back(notes.substr(pos)); break; }
            segs.push_back(notes.substr(pos, next - pos));
            pos = next + 2;
        }
        if (static_cast<int>(segs.size()) <= cap) return notes;
        std::string out;
        for (std::size_t i = segs.size() - static_cast<std::size_t>(cap); i < segs.size(); ++i) {
            if (!out.empty()) out += "; ";
            out += segs[i];
        }
        return out;
    }

    sqlite3*        db_ = nullptr;
    std::string     db_path_;
    std::string     key_path_;
    crypto::Key256  key_{};
    bool            have_key_ = false;
    bool            dirty_ = false;
    bool            migrated_ = false;
    RetentionPolicy retention_{};
};

}  // namespace

std::unique_ptr<IMemoryEngine> make_memory_engine() {
    return std::make_unique<SqliteMemory>();
}

std::string recall_sentence(const PersonRecord& p, UnixTime now) {
    std::string name = p.name.empty() ? "someone you know" : p.name;
    std::string s = "That's " + name;
    if (!p.relation.empty()) s += ", " + p.relation;
    s += ".";
    const char* who = p.relation.empty() ? "them" : pronoun_for(p.relation);
    s += " You last saw " + std::string(who) + " " + elapsed_phrase(p.last_seen, now) + ".";
    if (!p.notes.empty()) s += " " + p.notes + ".";
    return s;
}

}  // namespace echo::memory
