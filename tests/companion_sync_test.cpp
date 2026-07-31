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

#include <cstdio>
#include <string>
#include <type_traits>

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
