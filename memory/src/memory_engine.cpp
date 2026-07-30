// ECHO OS — memory & recall engine implementation (SQLite-backed).
//
// The store is a single on-device SQLite database file. SQLite is the honest
// choice for an embedded record store (ADR-13): real ACID persistence across
// restarts, safe concurrent access from the perception/cognitive/reminder paths,
// and — as the whole product turns on this data never leaking — a self-contained,
// dependency-free single-file build that keeps the stub build green.
//
// What this file achieves for privacy is file-PERMISSION restriction (owner-only,
// best-effort), NOT encryption-at-rest. That honest distinction is documented in
// docs/ARCHITECTURE.md and docs/DECISIONS.md; SQLCipher would be the follow-up.
#include "echo/memory/memory_engine.hpp"

#include "echo/log.hpp"

#include "sqlite3.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
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

class SqliteMemory final : public IMemoryEngine {
public:
    Status open(const std::string& db_path) override {
        close();
        if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
            log_error("memory", "could not open store; memory disabled");
            if (db_) { sqlite3_close(db_); db_ = nullptr; }
            return Status::HardwareError;
        }
        // Durable + concurrency-friendly. WAL lets a reader (a future companion-
        // sync export) not block the perception writer.
        exec("PRAGMA journal_mode=WAL;");
        exec("PRAGMA synchronous=NORMAL;");
        exec("PRAGMA foreign_keys=ON;");
        if (!create_schema()) {
            log_error("memory", "schema init failed; memory disabled");
            close();
            return Status::HardwareError;
        }
        restrict_permissions(db_path);
        log_info("memory", "store ready");
        return Status::Ok;
    }

    bool is_open() const noexcept override { return db_ != nullptr; }

    void close() override {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
    }

    ~SqliteMemory() override { close(); }

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
        auto p = get_person(id);
        if (!p) return Status::Unavailable;
        std::string merged = p->notes;
        if (!merged.empty() && !note.empty()) merged += "; ";
        merged += note;
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

    // Best-effort owner-only permissions on the DB file. This is file-PERMISSION
    // restriction, not encryption (see ADR-13). On POSIX this is chmod 0600; on
    // Windows std::filesystem maps this loosely (ACL inheritance still applies), so
    // we do not overstate the guarantee. A ":memory:" store has no file to chmod.
    static void restrict_permissions(const std::string& db_path) {
        if (db_path == ":memory:" || db_path.empty()) return;
        std::error_code ec;
        std::filesystem::permissions(
            db_path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        if (ec) log_warn("memory", "could not restrict store file permissions");
    }

    sqlite3* db_ = nullptr;
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
