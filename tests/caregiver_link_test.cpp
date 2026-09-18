// ECHO OS — the caregiver boundary (Phase 22).
//
// This suite exists to answer one question with evidence rather than assurance:
// when a consented link is opened to someone else, does the privacy proof still
// hold?
//
// It drives the REAL pieces end to end — the real SQLite memory engine, the real
// AES-256-CTR + CMAC secure channel, the real hardened JSON decoder, the real
// rate limiter, the real CaregiverLink coordinator — and asserts, at run time,
// the things the static_asserts assert at compile time. Both matter. The
// compile-time proof says a name CANNOT be put in a digest; these tests say a
// name IS NOT in the digest that the real code, holding real names, actually
// produced. A type system proves the shape; only a test proves the wiring.
//
// Everything here runs in the dependency-free stub build: the transports are
// in-process, there is no radio, no libcurl, no network, and no BLE stack.
#include "echo/boot/caregiver_link.hpp"
#include "echo/companion/caregiver_digest.hpp"
#include "echo/companion/companion_sync.hpp"
#include "echo/companion/inbound.hpp"
#include "echo/companion/secure_channel.hpp"
#include "echo/companion/transport.hpp"
#include "echo/consent.hpp"
#include "echo/crypto/aes256.hpp"
#include "echo/memory/memory_engine.hpp"
#include "echo/voice/voice_ui.hpp"

#include "check.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace echo;
using echo::boot::CaregiverLink;
using echo::companion::CaregiverDigest;
using echo::companion::CommandKind;
using echo::companion::CommandReject;
using echo::companion::CommandRepeat;
using echo::companion::Direction;
using echo::companion::Frame;
using echo::companion::FrameReject;
using echo::companion::FrameType;
using echo::companion::InboundCommand;
using echo::companion::SecureChannel;

namespace {

// --- fixtures ----------------------------------------------------------------

std::string temp_db_path(const char* tag) {
    auto p = std::filesystem::temp_directory_path() /
             ("echo_caregiver_test_" + std::string(tag) + ".db");
    std::error_code ec;
    std::filesystem::remove(p, ec);
    std::filesystem::remove(std::filesystem::path(p) += "-wal", ec);
    std::filesystem::remove(std::filesystem::path(p) += "-shm", ec);
    std::filesystem::remove(std::filesystem::path(p) += ".key", ec);
    std::filesystem::remove(std::filesystem::path(p) += ".pairing.key", ec);
    return p.string();
}

memory::Embedding fixture_embedding(int seed, std::size_t dim = 128) {
    memory::Embedding e(dim, 0.0f);
    e[static_cast<std::size_t>(seed) % dim] = 1.0f;
    for (std::size_t i = 0; i < dim; ++i) e[i] += 0.01f * static_cast<float>((seed + i) % 3);
    return e;
}

crypto::Key256 fixture_key(std::uint8_t salt = 0) {
    crypto::Key256 k{};
    for (std::size_t i = 0; i < k.size(); ++i)
        k[i] = static_cast<std::uint8_t>(0x40 + i * 3 + salt);
    return k;
}

// A voice that just records. The runtime's FakeVoice would do, but this suite
// links neither echo::runtime nor its fault-injection machinery — the point here
// is the boundary, not engine faults.
class RecordingVoice final : public voice::IVoiceUi {
public:
    Status initialize() override { return Status::Ok; }
    Status speak(const voice::Utterance& u) override {
        spoken.push_back(u.text);
        return Status::Ok;
    }
    Status play_earcon(std::string_view) override { return Status::Ok; }
    void   barge_in() override {}
    void   shutdown() override {}

    bool said_anything_containing(const std::string& needle) const {
        for (const auto& s : spoken)
            if (s.find(needle) != std::string::npos) return true;
        return false;
    }

    std::vector<std::string> spoken;
};

// The identifying content this suite plants in the store and then hunts for in
// everything that leaves the device. Chosen to be unmistakable: if any of these
// byte sequences shows up on the wire, something leaked, and no amount of
// "well, that field is only used for..." changes it.
const char* const kNeedles[] = {
    "Margaret",              // a person's name
    "granddaughter",         // a relation
    "blue heart pill",       // reminder text — a medication, i.e. health data
    "did not recognize",     // an event summary
    "Wednesday lunch club",  // another reminder
};

bool contains_bytes(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string to_string_bytes(const Frame& f) {
    return std::string(reinterpret_cast<const char*>(f.data()), f.size());
}

// Populate a store with a rich, identifying day: people with names, reminders
// with medication text, events with summaries. Everything a digest could possibly
// be tempted to include.
void populate_identifying_day(memory::IMemoryEngine& db, memory::UnixTime now) {
    const auto margaret = db.remember_person("Margaret", "granddaughter",
                                             fixture_embedding(1), now - 7200);
    CHECK(margaret.is_ok());
    (void)db.remember_person("Tom", "neighbour", fixture_embedding(2), now - 7200);

    const auto pill = db.add_reminder("blue heart pill", now - 3600, memory::Recurrence::Daily);
    CHECK(pill.is_ok());
    const auto lunch = db.add_reminder("Wednesday lunch club", now - 1800, memory::Recurrence::Weekly);
    CHECK(lunch.is_ok());

    if (pill.is_ok()) {
        (void)db.mark_fired(pill.value(), now - 3500);
        (void)db.acknowledge_reminder(pill.value(), now - 3400);
    }
    if (lunch.is_ok()) (void)db.mark_fired(lunch.value(), now - 1700);

    memory::EventRecord ev;
    ev.kind    = memory::EventKind::SafeModeEngaged;
    ev.at      = now - 1200;
    ev.summary = "did not recognize Margaret";
    (void)db.log_event(ev);

    ev.kind    = memory::EventKind::UnverifiedAnswer;
    ev.at      = now - 900;
    ev.summary = "declined to guess about the blue heart pill";
    (void)db.log_event(ev);

    // Phase 23: same "how the wearer's day went" bucket as the two events above.
    ev.kind    = memory::EventKind::WanderingFlagged;
    ev.at      = now - 700;
    ev.summary = "wandering flagged";
    (void)db.log_event(ev);

    ev.kind    = memory::EventKind::DistressFlagged;
    ev.at      = now - 600;
    ev.summary = "distress flagged";
    (void)db.log_event(ev);
}

// --- 1. consent gates the digest, in both directions ------------------------

void test_digest_requires_consent() {
    const std::string path = temp_db_path("consent");
    auto db = memory::make_memory_engine();
    CHECK(db->open(path) == Status::Ok);

    const memory::UnixTime now = 1'700'000'000;
    populate_identifying_day(*db, now);

    RecordingVoice voice;
    auto sync = companion::make_companion_sync();
    CaregiverLink link(db.get(), sync.get(), &voice);

    // Default state: nothing granted, so nothing is built and nothing is sent.
    // Specifically NOT an empty digest — see the assertion below, which is the
    // whole point of this test.
    auto without = link.build_digest(now);
    CHECK(!without.is_ok());
    CHECK(without.status() == Status::Unavailable);

    // A caregiver in a supervised pairing session grants it.
    CHECK(link.grant(ConsentScope::Digest, ConsentGrantor::CaregiverSupervised, now) == Status::Ok);

    auto with = link.build_digest(now);
    CHECK(with.is_ok());
    if (with.is_ok()) {
        const CaregiverDigest& d = with.value();
        CHECK(d.window_hours == boot::kDigestWindowHours);
        CHECK(d.people_known == 2);
        CHECK(d.reminders_delivered == 2);
        CHECK(d.reminders_acknowledged == 1);
        CHECK(d.safe_mode_engagements == 1);
        CHECK(d.unverified_answers == 1);
        CHECK(d.wandering_flags == 1);
        CHECK(d.distress_flags == 1);
        CHECK(d.generated_at == now);
        CHECK(d.scope == ConsentScope::Digest);
    }

    // The grant survives a close/reopen. Consent is a record, not a session flag —
    // a device that forgot consent on reboot would silently stop working, and a
    // device that forgot a REVOCATION on reboot would silently start again.
    db->close();
    CHECK(db->open(path) == Status::Ok);
    const memory::ConsentRecord after_reboot = db->consent();
    CHECK(after_reboot.active());
    CHECK(after_reboot.scope == ConsentScope::Digest);
    CHECK(after_reboot.grantor == ConsentGrantor::CaregiverSupervised);
    CHECK(after_reboot.granted_at == now);

    db->close();
    std::printf("[caregiver] a digest is built only under recorded consent, and the "
                "record survives a reboot\n");
}

void test_revocation_is_immediate() {
    const std::string path = temp_db_path("revoke");
    auto db = memory::make_memory_engine();
    CHECK(db->open(path) == Status::Ok);

    const memory::UnixTime now = 1'700'000'000;
    populate_identifying_day(*db, now);

    RecordingVoice voice;
    auto sync = companion::make_companion_sync();
    CaregiverLink link(db.get(), sync.get(), &voice);

    CHECK(link.grant(ConsentScope::DigestAndCommands, ConsentGrantor::Wearer, now) == Status::Ok);
    CHECK(sync->consent_scope() == ConsentScope::DigestAndCommands);
    CHECK(link.build_digest(now).is_ok());

    // Revoke. Not "at the next sync", not "once the queue drains" — in this call.
    CHECK(link.revoke(now + 60) == Status::Ok);

    // Three independent things must go cold, and all three are checked because a
    // revocation that only takes effect in one of them is the bug this test exists
    // to catch.
    CHECK(sync->consent_scope() == ConsentScope::None);              // enforcement side
    CHECK(!db->consent().active());                                  // persisted side
    CHECK(!link.build_digest(now + 60).is_ok());                     // construction side
    CHECK(link.build_digest(now + 60).status() == Status::Unavailable);
    CHECK(link.push_digest(now + 60) == Status::Unavailable);

    // And a digest handed directly to companion-sync, bypassing the link entirely,
    // is still refused. Defence in depth: two modules, two checks, and neither one
    // trusts the other to have done it.
    CaregiverDigest smuggled;
    smuggled.window_hours = 24;
    smuggled.people_known = 2;
    CHECK(sync->send_digest(smuggled) == Status::Unavailable);

    // The wearer was told, in both directions. A link being switched on or off is
    // never a silent change on someone's own device.
    CHECK(voice.said_anything_containing("caregiver link is now active"));
    CHECK(voice.said_anything_containing("caregiver link is now off"));

    // It survives a reboot as revoked. This is the direction that must never fail
    // open: a device that comes back up having forgotten a withdrawal is a device
    // that resumed sending without being asked.
    db->close();
    CHECK(db->open(path) == Status::Ok);
    CHECK(!db->consent().active());
    CHECK(db->consent().revoked_at == now + 60);

    db->close();
    std::printf("[caregiver] revocation is immediate on all three sides "
                "(store, enforcement, construction) and survives a reboot\n");
}

// --- 2. the digest carries nothing identifying — proven on the actual bytes --

void test_digest_carries_no_identifying_content() {
    const std::string path = temp_db_path("content");
    auto db = memory::make_memory_engine();
    CHECK(db->open(path) == Status::Ok);

    const memory::UnixTime now = 1'700'000'000;
    populate_identifying_day(*db, now);

    // Real transport, real pairing key, real channel. The peer end is the test
    // double for the caregiver's phone — see transport.hpp on why an in-process
    // loopback is an honest stand-in for the protocol and not for the radio.
    auto pair = companion::make_loopback_transport();
    auto sync = companion::make_companion_sync(std::move(pair.device), fixture_key(),
                                               companion::make_null_verifier());
    RecordingVoice voice;
    CaregiverLink link(db.get(), sync.get(), &voice);

    CHECK(sync->connect(companion::Transport::Ble) == Status::Ok);
    CHECK(link.grant(ConsentScope::Digest, ConsentGrantor::Wearer, now) == Status::Ok);
    CHECK(link.push_digest(now) == Status::Ok);

    // What actually arrived at the other end.
    CHECK(pair.peer->open() == Status::Ok);
    const std::vector<Frame> received = pair.peer->receive();
    CHECK(received.size() == 1);
    if (received.empty()) { db->close(); return; }

    // (a) On the wire, before decryption: none of the identifying strings appear.
    //     This is the weakest of the three checks — ciphertext would hide them
    //     anyway — but it is the one that would catch a header or a "helpful"
    //     plaintext label added alongside the payload.
    const std::string wire = to_string_bytes(received[0]);
    for (const char* needle : kNeedles) CHECK(!contains_bytes(wire, needle));

    // (b) After decryption, in the plaintext the caregiver's app would actually
    //     read. This is the real test. If a name were in the digest it would be
    //     here, in the clear, exactly as the recipient sees it.
    SecureChannel peer(fixture_key(), Direction::CaregiverToDevice);
    companion::OpenedFrame opened;
    FrameReject reason = FrameReject::None;
    CHECK(peer.open(received[0], opened, reason));
    CHECK(reason == FrameReject::None);
    CHECK(opened.type == FrameType::Digest);
    for (const char* needle : kNeedles) CHECK(!contains_bytes(opened.payload, needle));

    // (c) The payload is a FIXED SIZE — 9 u32 counts (Phase 23 added wandering_flags/
    //     distress_flags to the original 7) + 2 i64 timestamps + 2 enum bytes = 54
    //     bytes — regardless of how many people are enrolled, how long their names
    //     are, or what any reminder says. A variable-length digest is how content
    //     sneaks in; a constant-length one cannot carry any. This single assertion
    //     is the strongest run-time statement in the suite.
    const std::size_t kExpectedDigestBytes = 9 * 4 + 2 * 8 + 2;
    CHECK(opened.payload.size() == kExpectedDigestBytes);

    // (d) Field by field, at run time, over the same X-macro list the struct and
    //     the static_asserts are generated from. The compile-time proof says the
    //     type cannot hold a string; this says the VALUES are what we think they
    //     are and are bounded — a count, not an identifier.
    auto built = link.build_digest(now);
    CHECK(built.is_ok());
    if (built.is_ok()) {
        const CaregiverDigest& d = built.value();
        // Each field is a plain number or a small enum, and every count is bounded
        // by what actually happened rather than being a handle into the store.
        CHECK(d.window_hours == 24);
        CHECK(d.reminders_due <= 4);
        CHECK(d.reminders_delivered == 2);
        CHECK(d.reminders_acknowledged == 1);
        CHECK(d.safe_mode_engagements == 1);
        CHECK(d.unverified_answers == 1);
        CHECK(d.people_known == 2);
        CHECK(d.wandering_flags == 1);
        CHECK(d.distress_flags == 1);
        CHECK(d.generated_at == now);
        CHECK(d.state == RuntimeState::Ready);
        CHECK(d.scope == ConsentScope::Digest);
        // people_known is a COUNT. It is deliberately not a person id, and there is
        // no field anywhere in this struct from which a specific person could be
        // recovered — the compile-time proof in companion_sync_test.cpp says so for
        // the type, and the byte-size assertion above says so for the encoding.
        //
        // Bound loosened from 64 to 96 when Phase 23 added two uint32_t fields
        // (wandering_flags/distress_flags): the WIRE size is pinned exactly by
        // kExpectedDigestBytes above (54 bytes, no padding — encode_digest is a
        // flat put_u32/put_i64 sequence), but this in-memory sizeof() also carries
        // compiler struct-padding around the two int64_t/enum members, which is not
        // something worth hand-computing here — the bound just needs to keep proving
        // "small and constant", not track the padded size to the byte.
        CHECK(sizeof(CaregiverDigest) < 96);
    }

    // (e) Ten times as much identifying data does not make the digest one byte
    //     bigger. This is the property stated as an experiment rather than as a
    //     claim: content that scales with the store is content that is being sent.
    for (int i = 0; i < 20; ++i) {
        (void)db->remember_person("Person With A Very Long Name Number " + std::to_string(i),
                                  "a rather lengthy description of the relationship",
                                  fixture_embedding(10 + i), now);
    }
    CHECK(link.push_digest(now + 1) == Status::Ok);
    const std::vector<Frame> received2 = pair.peer->receive();
    CHECK(received2.size() == 1);
    if (!received2.empty()) {
        CHECK(received2[0].size() == received[0].size());
        companion::OpenedFrame opened2;
        CHECK(peer.open(received2[0], opened2, reason));
        CHECK(opened2.payload.size() == kExpectedDigestBytes);
        for (const char* needle : kNeedles) CHECK(!contains_bytes(opened2.payload, needle));
        CHECK(!contains_bytes(opened2.payload, "Person With A Very Long Name"));
    }

    db->close();
    std::printf("[caregiver] the digest on the wire is %zu bytes of counts and enums, "
                "identical in size with 2 people or 22, and contains none of the "
                "names, relations, medications or summaries in the store\n",
                kExpectedDigestBytes);
}

// --- 3. the sealed channel treats the wire as hostile ------------------------

void test_secure_channel_rejects_tampering() {
    SecureChannel device(fixture_key(), Direction::DeviceToCaregiver);
    SecureChannel peer(fixture_key(), Direction::CaregiverToDevice);

    // Happy path first, so the rejections below mean something.
    const Frame good = device.seal(FrameType::Digest, "hello");
    companion::OpenedFrame out;
    FrameReject reason = FrameReject::None;
    CHECK(peer.open(good, out, reason));
    CHECK(out.payload == "hello");
    CHECK(out.type == FrameType::Digest);
    CHECK(out.seq == 1);

    // A replay of the exact same frame — bit-identical, valid MAC — is refused.
    // The MAC proves the frame came from the holder of the key; only the sequence
    // number proves it is not one the device already acted on.
    SecureChannel peer2(fixture_key(), Direction::CaregiverToDevice);
    CHECK(peer2.open(good, out, reason));
    CHECK(!peer2.open(good, out, reason));
    CHECK(reason == FrameReject::Replay);

    // Every single byte position, flipped. Not a spot check: the MAC must cover
    // the header, the IV, the length, and the ciphertext, and the only way to say
    // that with confidence is to try all of them.
    int accepted_mutations = 0;
    for (std::size_t i = 0; i < good.size(); ++i) {
        Frame bad = good;
        bad[i] = static_cast<std::uint8_t>(bad[i] ^ 0x01);
        SecureChannel fresh(fixture_key(), Direction::CaregiverToDevice);
        companion::OpenedFrame o;
        FrameReject r = FrameReject::None;
        if (fresh.open(bad, o, r)) ++accepted_mutations;
    }
    CHECK(accepted_mutations == 0);

    // A frame sealed under a DIFFERENT pairing key is refused. Pairing binds the
    // two endpoints; a device that accepted this would be accepting anyone.
    SecureChannel stranger(fixture_key(0x11), Direction::DeviceToCaregiver);
    const Frame foreign = stranger.seal(FrameType::Digest, "hello");
    SecureChannel fresh_peer(fixture_key(), Direction::CaregiverToDevice);
    CHECK(!fresh_peer.open(foreign, out, reason));
    CHECK(reason == FrameReject::BadMac);

    // A frame the device sent, reflected straight back at it. Without the
    // direction byte in the MAC'd header this would verify perfectly — the device
    // would accept its own digest as an inbound command.
    SecureChannel device2(fixture_key(), Direction::DeviceToCaregiver);
    const Frame own = device2.seal(FrameType::Digest, "hello");
    SecureChannel device3(fixture_key(), Direction::DeviceToCaregiver);
    CHECK(!device3.open(own, out, reason));
    CHECK(reason == FrameReject::WrongDirection);

    // Truncation at every length. A short read must be a rejection, never an
    // over-read: this is the loop that would catch the classic parser bug.
    for (std::size_t n = 0; n < good.size(); ++n) {
        Frame truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n));
        SecureChannel fresh(fixture_key(), Direction::CaregiverToDevice);
        companion::OpenedFrame o;
        FrameReject r = FrameReject::None;
        CHECK(!fresh.open(truncated, o, r));
    }

    // Oversized. Refused on the declared length before a byte of it is processed.
    Frame huge(companion::kFrameOverhead + companion::kMaxPayloadBytes + 1024, 0u);
    SecureChannel fresh2(fixture_key(), Direction::CaregiverToDevice);
    CHECK(!fresh2.open(huge, out, reason));
    CHECK(reason == FrameReject::TooLong);

    // Empty, and a lone zero byte.
    CHECK(!fresh2.open(Frame{}, out, reason));
    CHECK(!fresh2.open(Frame{0u}, out, reason));

    std::printf("[caregiver] the sealed channel refuses every single-bit mutation "
                "(%zu positions), every truncation, a foreign key, a reflected "
                "frame, and a replay\n", good.size());
}

// --- 4. the inbound command decoder, treated as hostile input ---------------

std::string command_json(const char* body) { return std::string(body); }

void test_inbound_commands_are_validated() {
    const std::int64_t now = 1'700'000'000;

    // A well-formed reminder is accepted, and comes out as validated fields —
    // never as the raw document.
    auto ok = companion::decode_command(
        command_json(R"({"v":1,"seq":1,"cmd":"add_reminder","text":"take the blue pill",)"
                     R"("due":1700003600,"repeat":"daily"})"),
        now, 0);
    CHECK(ok.ok);
    CHECK(ok.reason == CommandReject::None);
    CHECK(ok.command.kind == CommandKind::AddReminder);
    CHECK(ok.command.text == "take the blue pill");
    CHECK(ok.command.repeat == CommandRepeat::Daily);
    CHECK(ok.command.seq == 1);

    // Every rejection is a named reason, because "it didn't work" is not a thing
    // this device should ever have to debug from a caregiver's phone call.
    struct Case {
        const char*   payload;
        CommandReject expect;
        const char*   why;
    };

    // The oversize case is built rather than written out.
    const std::string oversized =
        R"({"v":1,"seq":2,"cmd":"add_reminder","due":1700003600,"text":")" +
        std::string(companion::kMaxCommandBytes + 512, 'a') + R"("})";

    const std::string over_long_text =
        R"({"v":1,"seq":2,"cmd":"add_reminder","due":1700003600,"text":")" +
        std::string(companion::kMaxReminderChars + 5, 'b') + R"("})";

    // Deep nesting — the Phase 6 stack-overflow bug, aimed at the new decoder.
    // Well past the parser's depth cap, which is the point.
    std::string deep;
    for (int i = 0; i < 600; ++i) deep += "[";
    for (int i = 0; i < 600; ++i) deep += "]";

    const Case cases[] = {
        {R"({"v":1,"seq":2,"cmd":"add_reminder","due":1700003600,"text":"hi",)"
         R"("embedding":[0.1,0.2]})",
         CommandReject::ForbiddenField,
         "THE NAMING RULE: a payload that so much as mentions an embedding is "
         "refused before anything in it is read"},
        {R"({"v":1,"seq":2,"cmd":"enrol_name","name":"Margaret","descriptor":"..."})",
         CommandReject::ForbiddenField,
         "the same, under a different name for the same thing"},
        {R"({"v":1,"seq":2,"cmd":"enrol_name","name":"Margaret","person_id":7})",
         CommandReject::ForbiddenField,
         "a caregiver may not address an existing person by id"},
        {R"({"v":1,"seq":2,"cmd":"add_reminder","due":1700003600,)"
         R"("text":"hi\r\nBcc: someone@example.com"})",
         CommandReject::BadField,
         "the Gmail header-injection lesson: control characters in text, refused"},
        {R"({"v":9,"seq":2,"cmd":"add_reminder","due":1700003600,"text":"hi"})",
         CommandReject::BadVersion, "an unknown schema version is refused, not guessed at"},
        {R"({"v":1,"seq":2,"cmd":"format_disk"})", CommandReject::UnknownCommand,
         "the command set is an allowlist"},
        {R"({"v":1,"seq":2,"cmd":"add_reminder","due":1700003600})",
         CommandReject::MissingField, "a reminder with no text"},
        {R"({"v":1,"seq":2,"cmd":"add_reminder","text":"hi"})", CommandReject::MissingField,
         "a reminder with no due time"},
        {R"({"v":1,"seq":2,"cmd":"add_reminder","text":"hi","due":9999999999})",
         CommandReject::BadField, "a due time years away is not a reminder"},
        {R"({"v":1,"seq":2,"cmd":"add_reminder","text":"hi","due":1})",
         CommandReject::BadField, "nor is one in 1970"},
        // Present-but-empty is BadField, not MissingField: the key was sent, so the
        // sender has a value problem, not an omission. The distinction is diagnostic
        // only — both are refused — but a reason code that lies is worse than none.
        {R"({"v":1,"seq":2,"cmd":"enrol_name","name":""})", CommandReject::BadField,
         "an empty name"},
        {R"([1,2,3])", CommandReject::NotAnObject, "a document that is not an object"},
        {R"({"v":1,"seq":2,)", CommandReject::Malformed, "truncated JSON"},
        {R"(not json at all)", CommandReject::Malformed, "not JSON at all"},
        {R"()", CommandReject::Malformed, "nothing at all"},
        {R"({"v":1,"seq":1,"cmd":"request_digest"})", CommandReject::Malformed,
         "placeholder — replaced below by the replay case"},
    };

    for (const Case& c : cases) {
        // The last entry is the replay case; it needs a non-zero last_seq.
        const bool is_replay = (std::strcmp(c.why, "placeholder — replaced below by the replay case") == 0);
        const auto r = companion::decode_command(c.payload, now, is_replay ? 5u : 0u);
        CHECK(!r.ok);
        if (!is_replay) CHECK(r.reason == c.expect);
        // Name the case on failure. A table-driven test that only says "line 541" makes
        // the reader diff twenty payloads by eye to find the one that moved.
        if (r.ok || (!is_replay && r.reason != c.expect)) {
            std::fprintf(stderr, "  %s -> accepted=%d reason=%s (expected %s)\n", c.why,
                         static_cast<int>(r.ok), companion::to_string(r.reason),
                         companion::to_string(c.expect));
        }
    }

    // An embedded NUL inside the reminder text. This one has to be BUILT rather than
    // written as a literal in the table above: a raw 0x00 byte in a source file is not
    // portable, and MSVC quietly drops it — the case then compiles to the harmless text
    // "ab" and passes while testing nothing at all. (It did exactly that, silently,
    // until the decoder disagreed with the expected reason and gave it away.) Splicing
    // the byte in at run time is the only way the decoder ever actually sees one.
    //
    // It matters because a NUL is where C and JSON disagree about where a string ends:
    // anything downstream that touches .c_str() would see a truncated reminder.
    std::string nul_text = R"({"v":1,"seq":2,"cmd":"add_reminder","due":1700003600,"text":"a)";
    nul_text.push_back('\0');
    nul_text += R"(b"})";
    // Assert the byte survived into the payload, so this can never quietly revert to
    // testing "ab" again.
    CHECK(nul_text.find('\0') != std::string::npos);
    CHECK(std::strlen(nul_text.c_str()) < nul_text.size());
    const auto nul = companion::decode_command(nul_text, now, 0);
    CHECK(!nul.ok);
    CHECK(nul.reason == CommandReject::BadField);

    // The built cases.
    const auto too_big = companion::decode_command(oversized, now, 0);
    CHECK(!too_big.ok);
    CHECK(too_big.reason == CommandReject::TooLarge);

    const auto too_wordy = companion::decode_command(over_long_text, now, 0);
    CHECK(!too_wordy.ok);
    CHECK(too_wordy.reason == CommandReject::BadField);

    const auto too_deep = companion::decode_command(deep, now, 0);
    CHECK(!too_deep.ok);
    CHECK(too_deep.reason == CommandReject::Malformed);

    // A replay: sequence at or below the high-water mark.
    const auto replayed = companion::decode_command(
        R"({"v":1,"seq":3,"cmd":"request_digest"})", now, 5);
    CHECK(!replayed.ok);

    std::printf("[caregiver] the inbound decoder refuses forbidden fields, control "
                "characters, oversize, over-deep, malformed, out-of-range and "
                "replayed payloads — each with a named reason\n");
}

// A deterministic hostile-input sweep. The Phase 6 fuzz corpus lives behind
// ECHO_BUILD_FUZZERS and libFuzzer; this is the part of it that can run in the
// dependency-free CI build on every commit — a few thousand mutations of valid
// and invalid payloads, asserting only what the module promises: decode_command
// always terminates, never throws, and never partially applies.
void test_inbound_decoder_survives_hostile_input() {
    const std::int64_t now = 1'700'000'000;

    const std::string seeds[] = {
        R"({"v":1,"seq":1,"cmd":"add_reminder","text":"pill","due":1700003600})",
        R"({"v":1,"seq":1,"cmd":"enrol_name","name":"Margaret","relation":"granddaughter"})",
        R"({"v":1,"seq":1,"cmd":"request_digest"})",
        R"({"v":1,"seq":1,"cmd":"add_reminder","text":"x","due":1700003600,"repeat":"weekly"})",
        R"({})",
        R"([])",
        R"("")",
        R"(null)",
    };

    std::uint32_t rng = 0x9E3779B9u;  // deterministic: a failing seed is reproducible
    auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    int accepted = 0;
    int total    = 0;
    for (const std::string& seed : seeds) {
        for (int trial = 0; trial < 400; ++trial) {
            std::string m = seed;
            const int edits = 1 + static_cast<int>(next() % 6);
            for (int e = 0; e < edits && !m.empty(); ++e) {
                const std::size_t at = next() % m.size();
                switch (next() % 5) {
                    case 0: m[at] = static_cast<char>(next() % 256); break;   // random byte
                    case 1: m.erase(at, 1); break;                            // truncate
                    case 2: m.insert(at, 1, static_cast<char>(next() % 256)); break;
                    case 3: m.insert(at, std::string(1 + next() % 64, '[')); break;  // nest
                    case 4: m.insert(at, "\xff\xfe\x00\x01", 4); break;       // invalid UTF-8 + NUL
                }
            }
            // The contract: this returns. It does not throw, does not read past the
            // end, and does not half-apply a command. Anything else is a finding.
            const auto r = companion::decode_command(m, now, 0);
            ++total;
            if (r.ok) {
                ++accepted;
                // Anything it DOES accept must still satisfy every invariant — a
                // mutation must never be able to produce an over-long field or an
                // out-of-range time by slipping past a check.
                CHECK(r.command.kind != CommandKind::Unknown);
                CHECK(r.command.text.size() <= companion::kMaxReminderChars);
                CHECK(r.command.name.size() <= companion::kMaxNameChars);
                CHECK(r.command.relation.size() <= companion::kMaxRelationChars);
                if (r.command.kind == CommandKind::AddReminder) {
                    CHECK(r.command.due <= now + companion::kMaxDueAheadSeconds);
                    CHECK(r.command.due >= now - companion::kMaxDueBehindSeconds);
                    CHECK(!r.command.text.empty());
                }
                if (r.command.kind == CommandKind::EnrolName) CHECK(!r.command.name.empty());
            }
        }
    }

    std::printf("[caregiver] %d mutated payloads decoded without a crash "
                "(%d accepted, all satisfying every field invariant)\n", total, accepted);
}

void test_rate_limiting() {
    companion::RateLimiter limiter(5, 60, 50);
    const std::int64_t now = 1'700'000'000;

    // The burst is real: a caregiver setting three reminders in a row is normal use.
    for (int i = 0; i < 5; ++i) CHECK(limiter.allow(now));
    // The sixth is refused. Not dropped silently — counted.
    CHECK(!limiter.allow(now));
    CHECK(limiter.refused() == 1);
    CHECK(limiter.accepted_today() == 5);

    // A minute later the bucket has refilled.
    CHECK(limiter.allow(now + 60));

    // The daily ceiling holds even for a peer with a valid key, sending slowly and
    // patiently for hours. Authentication is not a licence to flood.
    companion::RateLimiter patient(5, 60, 50);
    int accepted = 0;
    for (int minute = 0; minute < 600; ++minute)
        if (patient.allow(now + minute * 60)) ++accepted;
    CHECK(accepted == 50);
    CHECK(patient.accepted_today() == 50);

    // The next day it starts again — a ceiling, not a lifetime quota.
    CHECK(patient.allow(now + 90000));

    std::printf("[caregiver] the rate limiter absorbs a burst, refuses a flood, and "
                "holds a daily ceiling against a patient authenticated peer\n");
}

// --- 5. the whole inbound loop: validated, rate-limited, announced, applied --

void test_inbound_command_is_announced_and_applied() {
    const std::string path = temp_db_path("inbound");
    auto db = memory::make_memory_engine();
    CHECK(db->open(path) == Status::Ok);

    const memory::UnixTime now = 1'700'000'000;

    auto pair = companion::make_loopback_transport();
    auto sync = companion::make_companion_sync(std::move(pair.device), fixture_key(),
                                               companion::make_null_verifier());
    RecordingVoice voice;
    CaregiverLink link(db.get(), sync.get(), &voice);

    CHECK(sync->connect(companion::Transport::Ble) == Status::Ok);
    CHECK(pair.peer->open() == Status::Ok);
    CHECK(link.grant(ConsentScope::DigestAndCommands, ConsentGrantor::CaregiverSupervised, now) ==
          Status::Ok);

    // The caregiver's end seals a real frame and puts it on the real wire.
    SecureChannel peer(fixture_key(), Direction::CaregiverToDevice);
    const std::string payload =
        R"({"v":1,"seq":1,"cmd":"add_reminder","text":"blue heart pill",)"
        R"("due":1700003600,"repeat":"daily"})";
    CHECK(pair.peer->send(peer.seal(FrameType::Command, payload)) == Status::Ok);

    const int applied = link.drain_inbound(now);
    CHECK(applied == 1);
    CHECK(link.applied_commands() == 1);

    // Applied: the reminder is in the store, with its recurrence mapped across the
    // module seam.
    const auto all = db->all_reminders();
    bool found = false;
    for (const auto& r : all)
        if (r.text == "blue heart pill" && r.recurrence == memory::Recurrence::Daily) found = true;
    CHECK(found);

    // Announced: the wearer heard about it. A caregiver-set reminder is a WRITE to
    // someone else's device, and this device does not change under them silently.
    CHECK(voice.said_anything_containing("A caregiver added a reminder"));
    CHECK(voice.said_anything_containing("blue heart pill"));

    // THE NAMING RULE, end to end. A caregiver may pre-register a NAME. What comes
    // back has an EMPTY embedding, so recognition still cannot match it — the face
    // is bound to the name only by the wearer, on-device, in the moment.
    const std::string enrol =
        R"({"v":1,"seq":2,"cmd":"enrol_name","name":"Margaret","relation":"granddaughter"})";
    CHECK(pair.peer->send(peer.seal(FrameType::Command, enrol)) == Status::Ok);
    CHECK(link.drain_inbound(now + 1) == 1);
    CHECK(voice.said_anything_containing("A caregiver added Margaret"));

    bool margaret_has_no_face = false;
    for (const auto& p : db->all_people())
        if (p.name == "Margaret") margaret_has_no_face = p.embedding.empty();
    CHECK(margaret_has_no_face);

    // And she is still not recognizable: a pre-registered name matches no face.
    const memory::PersonMatch m = db->recognize(fixture_embedding(1));
    CHECK(!m.matched || m.person.name != "Margaret");

    // A replayed frame — the identical bytes, sent again — changes nothing.
    const Frame replay = peer.seal(FrameType::Command, R"({"v":1,"seq":3,"cmd":"request_digest"})");
    CHECK(pair.peer->send(replay) == Status::Ok);
    CHECK(link.drain_inbound(now + 2) == 1);
    const std::size_t reminders_after = db->all_reminders().size();
    CHECK(pair.peer->send(replay) == Status::Ok);
    (void)link.drain_inbound(now + 3);
    CHECK(db->all_reminders().size() == reminders_after);
    CHECK(sync->rejected_frames() >= 1);

    // Without consent for COMMANDS, an otherwise perfect frame is refused. The
    // digest scope and the command scope are different permissions, and being
    // allowed to look is not being allowed to write.
    CHECK(link.grant(ConsentScope::Digest, ConsentGrantor::Wearer, now + 4) == Status::Ok);
    const std::size_t before = db->all_reminders().size();
    CHECK(pair.peer->send(peer.seal(FrameType::Command,
                                    R"({"v":1,"seq":9,"cmd":"add_reminder",)"
                                    R"("text":"unauthorised","due":1700003600})")) == Status::Ok);
    CHECK(link.drain_inbound(now + 5) == 0);
    CHECK(db->all_reminders().size() == before);

    // With no consent at all, likewise — and the store is untouched.
    CHECK(link.revoke(now + 6) == Status::Ok);
    CHECK(pair.peer->send(peer.seal(FrameType::Command,
                                    R"({"v":1,"seq":10,"cmd":"add_reminder",)"
                                    R"("text":"still unauthorised","due":1700003600})")) ==
          Status::Ok);
    CHECK(link.drain_inbound(now + 7) == 0);
    CHECK(db->all_reminders().size() == before);

    db->close();
    std::printf("[caregiver] an inbound command is validated, replay-checked, "
                "scope-checked, announced to the wearer and only then applied — and "
                "a pre-registered name carries no face\n");
}

// --- 6. firmware fails closed -----------------------------------------------

void test_firmware_fails_closed() {
    auto verifier = companion::make_null_verifier();

    // The honest statement of what this device can verify today: nothing. It says
    // so in its own scheme name rather than in a comment somewhere.
    CHECK(std::string(verifier->scheme()) == "none (fail-closed)");

    companion::FirmwareImage unsigned_image;
    unsigned_image.version = "1.2.3";
    unsigned_image.bytes   = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(!verifier->verify(unsigned_image));

    // A signature that is merely PRESENT proves nothing, and is treated as proving
    // nothing. There is no key to check it against, so the only defensible answer
    // is no — an update path that half-verifies is worse than one that refuses,
    // because it looks like it works.
    companion::FirmwareImage signed_image = unsigned_image;
    signed_image.signature = std::vector<std::uint8_t>(64, 0xAB);
    CHECK(!verifier->verify(signed_image));

    // Empty everything.
    CHECK(!verifier->verify(companion::FirmwareImage{}));

    // And through the real module: an offered update is refused, so poll_firmware_update()
    // never reports one available.
    auto pair = companion::make_loopback_transport();
    auto sync = companion::make_companion_sync(std::move(pair.device), fixture_key(),
                                               companion::make_null_verifier());
    CHECK(sync->connect(companion::Transport::Ble) == Status::Ok);
    CHECK(pair.peer->open() == Status::Ok);

    SecureChannel peer(fixture_key(), Direction::CaregiverToDevice);
    CHECK(pair.peer->send(peer.seal(FrameType::Firmware, R"({"version":"9.9.9"})")) == Status::Ok);
    (void)sync->poll_inbound(1'700'000'000);
    CHECK(sync->poll_firmware_update() != Status::Ok);

    std::printf("[caregiver] firmware verification fails closed: no signature scheme "
                "is implemented, so every image — signed-looking or not — is refused\n");
}

// --- 7. the transports ------------------------------------------------------

void test_transports() {
    // The fake: deterministic, scriptable, no I/O of any kind.
    auto fake = companion::make_fake_transport();
    CHECK(fake->kind() == companion::TransportKind::Fake);
    CHECK(!fake->is_open());
    CHECK(fake->open() == Status::Ok);
    CHECK(fake->is_open());
    CHECK(fake->send(Frame{1, 2, 3}) == Status::Ok);
    CHECK(fake->sent().size() == 1);
    fake->inject(Frame{4, 5, 6});
    CHECK(fake->receive().size() == 1);
    CHECK(fake->receive().empty());   // drained, not re-delivered
    fake->fail_next_send(true);
    CHECK(fake->send(Frame{7}) != Status::Ok);
    CHECK(fake->send(Frame{7}) == Status::Ok);   // one failure, not a stuck state

    // The loopback pair: two endpoints, one wire, everything in-process. It runs
    // the real framing, the real AES-CTR and the real CMAC — so it proves the
    // PROTOCOL. It proves nothing whatsoever about a radio, and this suite does
    // not pretend otherwise.
    auto pair = companion::make_loopback_transport();
    CHECK(pair.device->kind() == companion::TransportKind::Loopback);
    CHECK(pair.device->open() == Status::Ok);
    CHECK(pair.peer->open() == Status::Ok);
    CHECK(pair.device->send(Frame{9, 9}) == Status::Ok);
    const auto at_peer = pair.peer->receive();
    CHECK(at_peer.size() == 1);
    CHECK(pair.device->receive().empty());   // a device does not hear its own sends
    CHECK(pair.peer->send(Frame{8}) == Status::Ok);
    CHECK(pair.device->receive().size() == 1);

    // The BLE backend is a DOCUMENTED STUB. It does not pretend to connect, and it
    // does not silently succeed — it returns Unavailable, which is the truth. There
    // is no BLE stack in this build and there is no caregiver app to connect to.
    auto ble = companion::make_ble_transport();
    CHECK(ble->kind() == companion::TransportKind::Ble);
    CHECK(ble->open() != Status::Ok);
    CHECK(!ble->is_open());
    CHECK(ble->send(Frame{1}) != Status::Ok);
    CHECK(ble->receive().empty());

    std::printf("[caregiver] fake and loopback transports work with no network, no "
                "libcurl and no BLE stack; the real BLE backend honestly reports "
                "Unavailable\n");
}

// --- 8. consent is logged narrowly, and nothing more ------------------------

void test_consent_events_are_narrow() {
    const std::string path = temp_db_path("events");
    auto db = memory::make_memory_engine();
    CHECK(db->open(path) == Status::Ok);

    const memory::UnixTime now = 1'700'000'000;
    RecordingVoice voice;
    auto sync = companion::make_companion_sync();
    CaregiverLink link(db.get(), sync.get(), &voice);

    CHECK(link.grant(ConsentScope::DigestAndCommands, ConsentGrantor::CaregiverSupervised, now) ==
          Status::Ok);
    CHECK(link.revoke(now + 300) == Status::Ok);

    CHECK(db->count_events(memory::EventKind::ConsentGranted, now - 10, now + 600) == 1);
    CHECK(db->count_events(memory::EventKind::ConsentRevoked, now - 10, now + 600) == 1);

    // The events say WHAT and WHEN — a scope and a role — and nothing else. No
    // caregiver name, no device identifier, no contact details. Recording who the
    // caregiver is would put a second person's data on the wearer's device, which
    // nobody in this exchange consented to.
    for (const auto& ev : db->recent_events(20)) {
        if (ev.kind != memory::EventKind::ConsentGranted &&
            ev.kind != memory::EventKind::ConsentRevoked)
            continue;
        CHECK(ev.summary.size() < 80);
        CHECK(ev.summary.find('@') == std::string::npos);
        CHECK(ev.person_id == 0);
    }

    db->close();
    std::printf("[caregiver] granting and revoking consent each log exactly one narrow "
                "event: a scope and a role, never a caregiver's identity\n");
}

}  // namespace

int main() {
    test_digest_requires_consent();
    test_revocation_is_immediate();
    test_digest_carries_no_identifying_content();
    test_secure_channel_rejects_tampering();
    test_inbound_commands_are_validated();
    test_inbound_decoder_survives_hostile_input();
    test_rate_limiting();
    test_inbound_command_is_announced_and_applied();
    test_firmware_fails_closed();
    test_transports();
    test_consent_events_are_narrow();
    return echo::test::report("caregiver-link");
}
