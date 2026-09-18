// ECHO OS — companion-sync fixture tests.
//
// Phase 10, deliverable #5. Two things:
//   (a) exercise the REAL alert/status/firmware message construction and the
//       connect→send→disconnect lifecycle through make_companion_sync(); and
//   (b) PROVE — with actual compile-time assertions, not a comment — Phase 2's
//       structural privacy guarantee (principle #4): the transport interface has
//       no path that accepts a SensorFrame. If someone ever adds a
//       send(SensorFrame)-shaped overload or makes a payload swallow one, these
//       static_asserts stop compiling. companion-sync was at 0 % before this phase.
#include "echo/companion/companion_sync.hpp"
#include "echo/types.hpp"
#include "echo/result.hpp"

#include "check.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

using namespace echo;
using namespace echo::companion;

namespace {

// ---- (b) STRUCTURAL PRIVACY GUARANTEE (compile-time) ------------------------
// Can send_alert / send_status be called with these argument types?
template <class... Args>
inline constexpr bool alert_invocable_with =
    std::is_invocable_v<decltype(&ICompanionSync::send_alert), ICompanionSync&, Args...>;
template <class... Args>
inline constexpr bool status_invocable_with =
    std::is_invocable_v<decltype(&ICompanionSync::send_status), ICompanionSync&, Args...>;

// Sanity: the transport DOES accept its intended payloads...
static_assert(alert_invocable_with<const Alert&>,
              "send_alert must accept an Alert");
static_assert(status_invocable_with<const StatusReport&>,
              "send_status must accept a StatusReport");

// ...and it does NOT accept raw sensor data through any of its send paths.
static_assert(!alert_invocable_with<SensorFrame>,
              "PRIVACY: no send_alert path may accept a SensorFrame");
static_assert(!alert_invocable_with<const SensorFrame&>,
              "PRIVACY: no send_alert path may accept a SensorFrame");
static_assert(!status_invocable_with<SensorFrame>,
              "PRIVACY: no send_status path may accept a SensorFrame");
static_assert(!status_invocable_with<const SensorFrame&>,
              "PRIVACY: no send_status path may accept a SensorFrame");

// The payloads themselves cannot be built out of, or made to carry, a SensorFrame:
// they are categories + timestamps + scalars, never sensor evidence.
static_assert(!std::is_constructible_v<Alert, SensorFrame>,
              "PRIVACY: an Alert must not be constructible from a SensorFrame");
static_assert(!std::is_constructible_v<StatusReport, SensorFrame>,
              "PRIVACY: a StatusReport must not be constructible from a SensorFrame");
// Alert's evidence field is a short human string, not a raw buffer/pointer.
static_assert(std::is_same_v<decltype(Alert::note), std::string>,
              "Alert::note must be a human summary string, not raw data");

// ---- Phase 22: the same proof, extended to the caregiver digest -------------
//
// The digest is the first thing this device sends on a SCHEDULE rather than in
// response to an event, and it is the first thing sent to a named human rather
// than to a maintenance channel. Both of those make it the highest-risk payload
// in the module, so it gets the strictest proof in the module.
//
// Note what is deliberately NOT symmetric with the block above. Alert::note is
// allowed to be a std::string: an alert is a one-off, it fires when something has
// gone wrong, and a caregiver reading "battery critical" needs the words. The
// digest is a periodic feed of someone's daily life. A free-text field there would
// be filled, over time, with exactly the content the rest of this proof exists to
// keep on the device — not by anyone deciding to leak it, but by the ordinary
// pressure of "it would be so much more useful if it also said...". So the digest
// gets no string, no buffer, and no affordance for one.
template <class... Args>
inline constexpr bool digest_invocable_with =
    std::is_invocable_v<decltype(&ICompanionSync::send_digest), ICompanionSync&, Args...>;

// Sanity: the path exists and takes its intended payload.
static_assert(digest_invocable_with<const CaregiverDigest&>,
              "send_digest must accept a CaregiverDigest");

// The third send path is held to the same rule as the first two.
static_assert(!digest_invocable_with<SensorFrame>,
              "PRIVACY: no send_digest path may accept a SensorFrame");
static_assert(!digest_invocable_with<const SensorFrame&>,
              "PRIVACY: no send_digest path may accept a SensorFrame");

// The digest cannot be built out of raw evidence, out of prose, or out of a
// buffer. `Embedding` is `std::vector<float>` over in the memory engine — a type
// this module cannot name, and deliberately so (see the cross-module half of this
// proof in memory_engine_test.cpp, which links both modules and can). Naming the
// shape here covers it structurally regardless: nothing vector-shaped, nothing
// string-shaped, nothing pointer-shaped can become a digest.
static_assert(!std::is_constructible_v<CaregiverDigest, SensorFrame>,
              "PRIVACY: a CaregiverDigest must not be constructible from a SensorFrame");
static_assert(!std::is_constructible_v<CaregiverDigest, std::string>,
              "PRIVACY: a CaregiverDigest must not be constructible from a string");
static_assert(!std::is_constructible_v<CaregiverDigest, const char*>,
              "PRIVACY: a CaregiverDigest must not be constructible from raw text");
static_assert(!std::is_constructible_v<CaregiverDigest, std::vector<float>>,
              "PRIVACY: a CaregiverDigest must not be constructible from an embedding-shaped vector");
static_assert(!std::is_constructible_v<CaregiverDigest, std::vector<std::uint8_t>>,
              "PRIVACY: a CaregiverDigest must not be constructible from a byte buffer");
static_assert(!std::is_constructible_v<CaregiverDigest, Alert>,
              "PRIVACY: a CaregiverDigest must not be constructible from an Alert (which carries a note)");

// Every field, one at a time. The predicate is written with redundant clauses ON
// PURPOSE: is_arithmetic_v already excludes pointers, arrays and class types, so
// each extra clause is technically implied. They are spelled out so that (a) a
// reader can see that buffers, strings and pointers were each considered and
// refused by name, and (b) if a future change ever loosens the first clause —
// "trivially copyable would be more flexible" — the rest still bite. A proof that
// only holds because of one carefully-chosen trait is one edit from holding
// nothing.
template <class T>
inline constexpr bool digest_field_ok =
    (std::is_arithmetic_v<T> || std::is_enum_v<T>) &&
    !std::is_class_v<T> && !std::is_union_v<T> && !std::is_pointer_v<T> &&
    !std::is_array_v<T> && !std::is_reference_v<T> &&
    !std::is_same_v<std::decay_t<T>, std::string> &&
    !std::is_same_v<std::decay_t<T>, const char*>;

// The field list comes from the SAME X-macro the struct is generated from, so this
// proof cannot silently fall out of date. Add a field, and it is asserted here
// whether or not anyone remembered to come and update this file — which is the
// entire reason the struct is written that way (see caregiver_digest.hpp).
#define ECHO_TEST_ASSERT_DIGEST_FIELD(type, name)                                  \
    static_assert(digest_field_ok<type>,                                           \
                  "PRIVACY: CaregiverDigest::" #name                               \
                  " must be a scalar or an enum — never a string, buffer, "        \
                  "pointer or object. Minimization is the default: if a "          \
                  "caregiver's question can be answered with a count or a state, " \
                  "it must not be answered with a name or a string.");             \
    static_assert(digest_field_ok<decltype(CaregiverDigest::name)>,                \
                  "PRIVACY: CaregiverDigest::" #name                               \
                  " declared type must match its asserted type");
ECHO_CAREGIVER_DIGEST_FIELDS(ECHO_TEST_ASSERT_DIGEST_FIELD)
#undef ECHO_TEST_ASSERT_DIGEST_FIELD

// And the whole aggregate: no vtable, no owned heap, nothing that could hold a
// pointer to something bigger than itself.
static_assert(std::is_trivially_copyable_v<CaregiverDigest>,
              "PRIVACY: a CaregiverDigest must be a flat bag of scalars");
static_assert(std::is_standard_layout_v<CaregiverDigest>,
              "PRIVACY: a CaregiverDigest must have no vtable and no private state");

// ---- Phase 22: the INBOUND direction ----------------------------------------
// The naming rule, as a compile-time fact rather than a code review. Phase 15's
// rule is that a face is bound to a name only by the wearer, on-device, in the
// moment. An inbound command therefore has nowhere to PUT an embedding — not a
// field that is validated and rejected, but no field at all. This is what makes
// "no inbound command may ever create a person from an embedding" enforceable by
// the compiler rather than by whoever reviews the next patch.
static_assert(!std::is_constructible_v<InboundCommand, std::vector<float>>,
              "PRIVACY: an InboundCommand must not be constructible from an embedding");
static_assert(!std::is_constructible_v<InboundCommand, std::vector<std::uint8_t>>,
              "PRIVACY: an InboundCommand must not be constructible from a raw buffer");
static_assert(!std::is_constructible_v<InboundCommand, SensorFrame>,
              "PRIVACY: an InboundCommand must not be constructible from a SensorFrame");

// A run-time restatement so the guarantee also shows up as a passing test line.
void test_privacy_guarantee_is_structural() {
    CHECK((!alert_invocable_with<SensorFrame>));
    CHECK((!status_invocable_with<SensorFrame>));
    CHECK((!std::is_constructible_v<Alert, SensorFrame>));
    std::printf("[companion-sync] structural privacy guarantee holds: no transport "
                "path accepts a SensorFrame (enforced at compile time)\n");
}

// ---- (a) REAL message construction + lifecycle ------------------------------

// Offline: alerts/status are refused until a transport is connected.
void test_offline_refuses_sends() {
    auto sync = make_companion_sync();
    CHECK(!sync->connected());
    CHECK(sync->send_alert(Alert{AlertKind::Distress, now(), "distress"}) ==
          Status::Unavailable);
    StatusReport st; st.state = RuntimeState::Ready; st.battery_percent = 80;
    CHECK(sync->send_status(st) == Status::Unavailable);
}

// Connected: every alert kind and a status heartbeat construct and send.
void test_connected_sends_all_alert_kinds() {
    auto sync = make_companion_sync();
    CHECK(sync->connect(Transport::Ble) == Status::Ok);
    CHECK(sync->connected());

    for (auto kind : {AlertKind::SafeModeEngaged, AlertKind::Distress,
                      AlertKind::Wandering, AlertKind::LowConfidenceTrend,
                      AlertKind::EngineDegraded}) {
        Alert a{kind, now(), to_string(kind)};
        CHECK(sync->send_alert(a) == Status::Ok);
        CHECK(std::string(to_string(kind)) != "unknown");  // every kind maps
    }

    StatusReport st;
    st.state = RuntimeState::Active;
    st.battery_percent = 72;
    st.rssi_dbm = -55;
    st.uptime_seconds = 3600;
    CHECK(sync->send_status(st) == Status::Ok);

    sync->disconnect();
    CHECK(!sync->connected());
    // After disconnect, sends are refused again.
    CHECK(sync->send_alert(Alert{AlertKind::Wandering, now(), "left area"}) ==
          Status::Unavailable);
}

// Firmware: the scaffold transport carries the update path but has no update to
// apply, so it reports Unavailable (not an error, not a fabricated success).
void test_firmware_poll_reports_none() {
    auto sync = make_companion_sync();
    CHECK(sync->connect(Transport::Wifi) == Status::Ok);
    CHECK(sync->poll_firmware_update() == Status::Unavailable);
}

}  // namespace

int main() {
    test_privacy_guarantee_is_structural();
    test_offline_refuses_sends();
    test_connected_sends_all_alert_kinds();
    test_firmware_poll_reports_none();
    return echo::test::report("companion-sync");
}
