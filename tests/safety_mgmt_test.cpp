// ECHO OS — wandering & distress detection test (Phase 23).
//
// Two layers, both in the dependency-free stub build, mirroring power_mgmt_test.cpp
// (Phase 18) exactly — the same DI seam, the same edge-triggered-alert discipline, the
// same fault-containment guarantee, applied to a new axis:
//
//   1. PURE POLICY: wandering_risk_for_location()/distress_risk_for_arousal()/decide() as
//      total functions of scripted location/arousal readings — the grace-period bands,
//      asserted directly with no runtime in the loop.
//   2. THROUGH THE REAL RUNTIME: the same fault-injection-style DI seam Phase 17/18 built,
//      now also injecting deterministic FAKE location/arousal sources. Drive them across
//      the defined thresholds and assert the runtime's behaviour: a Confirmed wandering or
//      distress risk raises AlertKind::Wandering / AlertKind::Distress (their first-ever
//      production producers) exactly once per episode (edge-triggered, re-arms after
//      recovery), and a source read-fault neither crashes the loop nor fabricates a
//      zone/arousal state — it holds the last-known-good decision and raises a heads-up on
//      the EXISTING EngineDegraded channel instead (a sensing fault is a device-health
//      condition, not a wandering/distress event).
#include "echo/boot/runtime.hpp"

#include "echo/safety/safety_policy.hpp"
#include "echo/safety/fake_safety_source.hpp"
#include "echo/power/power_source.hpp"

#include "fake_engines.hpp"
#include "check.hpp"

#include <chrono>
#include <memory>
#include <string>

using namespace echo;
using echo::test::FakePerception;
using echo::test::FakeCognitive;
using echo::test::FakeVoice;
using echo::test::FakeCompanion;
using echo::test::FakeMemory;

namespace {

// ===========================================================================
// Layer 1 — the pure policy. No runtime, no hardware: total functions of inputs.
// ===========================================================================

safety::LocationReading loc(safety::ZoneState z, std::chrono::seconds dwell) {
    safety::LocationReading r; r.zone = z; r.dwell = dwell; return r;
}
safety::ArousalReading aro(safety::ArousalState s, std::chrono::seconds dwell) {
    safety::ArousalReading r; r.state = s; r.dwell = dwell; return r;
}

void test_wandering_risk_bands() {
    using safety::WanderingRisk;
    using safety::ZoneState;

    // Home never escalates, regardless of dwell.
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Home, std::chrono::seconds{0})) == WanderingRisk::None);
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Home, std::chrono::hours{999})) == WanderingRisk::None);

    // Boundary: None below the grace period, Suspected at/above it — never Confirmed.
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Boundary, safety::kBoundaryGrace - std::chrono::seconds{1})) == WanderingRisk::None);
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Boundary, safety::kBoundaryGrace)) == WanderingRisk::Suspected);
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Boundary, safety::kBoundaryGrace + std::chrono::hours{1})) == WanderingRisk::Suspected);

    // Away: Suspected immediately (being outside the known area at all is concerning),
    // Confirmed once it has held for the grace period.
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Away, std::chrono::seconds{0})) == WanderingRisk::Suspected);
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Away, safety::kAwayGrace - std::chrono::seconds{1})) == WanderingRisk::Suspected);
    CHECK(safety::wandering_risk_for_location(loc(ZoneState::Away, safety::kAwayGrace)) == WanderingRisk::Confirmed);
}

void test_distress_risk_bands() {
    using safety::DistressRisk;
    using safety::ArousalState;

    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::Calm, std::chrono::seconds{0})) == DistressRisk::None);
    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::Calm, std::chrono::hours{999})) == DistressRisk::None);

    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::Elevated, safety::kElevatedGrace - std::chrono::seconds{1})) == DistressRisk::None);
    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::Elevated, safety::kElevatedGrace)) == DistressRisk::Suspected);

    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::High, std::chrono::seconds{0})) == DistressRisk::Suspected);
    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::High, safety::kHighGrace - std::chrono::seconds{1})) == DistressRisk::Suspected);
    CHECK(safety::distress_risk_for_arousal(aro(ArousalState::High, safety::kHighGrace)) == DistressRisk::Confirmed);
}

void test_combined_decision() {
    // The two axes are independent: a confirmed wandering risk implies nothing about
    // distress, and vice versa. Only Confirmed sets the *_alert flags.
    {
        const auto d = safety::decide(loc(safety::ZoneState::Away, safety::kAwayGrace),
                                       aro(safety::ArousalState::Calm, std::chrono::seconds{0}));
        CHECK(d.wandering == safety::WanderingRisk::Confirmed);
        CHECK(d.wandering_alert);
        CHECK(d.distress == safety::DistressRisk::None);
        CHECK(!d.distress_alert);
    }
    {
        const auto d = safety::decide(loc(safety::ZoneState::Home, std::chrono::seconds{0}),
                                       aro(safety::ArousalState::High, safety::kHighGrace));
        CHECK(!d.wandering_alert);
        CHECK(d.distress_alert);
    }
    {
        const auto d = safety::decide(loc(safety::ZoneState::Boundary, safety::kBoundaryGrace),
                                       aro(safety::ArousalState::Elevated, safety::kElevatedGrace));
        // Both Suspected, neither Confirmed — no alert yet.
        CHECK(d.wandering == safety::WanderingRisk::Suspected);
        CHECK(d.distress == safety::DistressRisk::Suspected);
        CHECK(!d.wandering_alert);
        CHECK(!d.distress_alert);
    }
}

// ===========================================================================
// Layer 2 — through the REAL runtime with injected fake sources.
// ===========================================================================

// Non-owning handles to everything we injected.
struct Fakes {
    FakePerception*             perception      = nullptr;
    FakeCognitive*              cognitive       = nullptr;
    FakeVoice*                  voice           = nullptr;
    FakeCompanion*              companion       = nullptr;
    FakeMemory*                 memory          = nullptr;
    safety::FakeLocationSource* location_source = nullptr;
    safety::FakeArousalSource*  arousal_source  = nullptr;
};

boot::WatchdogConfig test_watchdog() {
    boot::WatchdogConfig c;
    c.perception_budget = std::chrono::milliseconds(150);
    c.cognitive_budget  = std::chrono::milliseconds(150);
    c.voice_budget      = std::chrono::milliseconds(150);
    c.memory_budget     = std::chrono::milliseconds(150);
    c.max_consecutive_failures = 3;
    c.max_recoveries           = 3;
    return c;
}

std::unique_ptr<boot::Runtime> make_runtime(Fakes& out) {
    auto perception      = std::make_unique<FakePerception>();
    auto cognitive       = std::make_unique<FakeCognitive>();
    auto voice           = std::make_unique<FakeVoice>();
    auto companion       = std::make_unique<FakeCompanion>();
    auto memory          = std::make_unique<FakeMemory>();
    auto power_mgr       = power::make_power_manager();  // real DVFS actor; not under test here
    auto location_source = std::make_unique<safety::FakeLocationSource>();
    auto arousal_source  = std::make_unique<safety::FakeArousalSource>();

    out.perception      = perception.get();
    out.cognitive       = cognitive.get();
    out.voice           = voice.get();
    out.companion       = companion.get();
    out.memory          = memory.get();
    out.location_source = location_source.get();
    out.arousal_source  = arousal_source.get();

    boot::Runtime::Engines engines;
    engines.memory          = std::move(memory);
    engines.perception      = std::move(perception);
    engines.cognitive       = std::move(cognitive);
    engines.voice           = std::move(voice);
    engines.companion       = std::move(companion);
    engines.power           = std::move(power_mgr);
    // power_source/thermal_source left null: boot() falls back to the real (documented-stub,
    // always-healthy) backends, exactly the "unset means use the real stub" convention this
    // module isn't exercising — only location/arousal are under test here.
    engines.location_source = std::move(location_source);
    engines.arousal_source  = std::move(arousal_source);

    auto rt = std::make_unique<boot::Runtime>(std::move(engines), test_watchdog());
    CHECK(rt->boot() == Status::Ok);
    return rt;
}

// ---------------------------------------------------------------------------
// 1. A Confirmed wandering risk raises AlertKind::Wandering — its first-ever production
//    producer.
void test_wandering_confirmed_raises_wandering_alert() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.location_source->set(safety::ZoneState::Away, safety::kAwayGrace);

    rt->tick();

    CHECK(fk.companion->count_of(companion::AlertKind::Wandering) == 1);
    CHECK(fk.companion->count_note_contains(companion::AlertKind::Wandering, "Wandering suspected") == 1);
}

// ---------------------------------------------------------------------------
// 2. A Confirmed distress risk raises AlertKind::Distress — its first-ever production
//    producer.
void test_distress_confirmed_raises_distress_alert() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.arousal_source->set(safety::ArousalState::High, safety::kHighGrace);

    rt->tick();

    CHECK(fk.companion->count_of(companion::AlertKind::Distress) == 1);
    CHECK(fk.companion->count_note_contains(companion::AlertKind::Distress, "Distress suspected") == 1);
}

// ---------------------------------------------------------------------------
// 3. Both conditions are EDGE-TRIGGERED: a standing condition alerts once, then re-arms
//    after recovery — mirrors power-mgmt's EngineDegraded latch behaviour exactly, now
//    over the dedicated Wandering/Distress kinds.
void test_conditions_are_edge_triggered() {
    Fakes fk;
    auto rt = make_runtime(fk);

    fk.location_source->set(safety::ZoneState::Away, safety::kAwayGrace);
    rt->tick();
    rt->tick();  // still away: must NOT re-alert every tick
    CHECK(fk.companion->count_of(companion::AlertKind::Wandering) == 1);

    // Recover to Home, then wander again: the latch re-arms -> a second alert.
    fk.location_source->set(safety::ZoneState::Home, std::chrono::seconds{0});
    rt->tick();
    fk.location_source->set(safety::ZoneState::Away, safety::kAwayGrace);
    rt->tick();
    CHECK(fk.companion->count_of(companion::AlertKind::Wandering) == 2);
}

// ---------------------------------------------------------------------------
// 4. A source read-fault neither crashes the loop nor fabricates a zone/arousal state: the
//    runtime holds the last-known-good (default safe) decision, raises ONE sensing heads-up
//    on the EXISTING EngineDegraded channel (never AlertKind::Wandering/::Distress — a
//    sensing fault is a device-health condition, not a wandering/distress event), and keeps
//    running.
void test_source_fault_is_contained_and_never_fabricates() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.location_source->fault = true;  // receiver read throws

    rt->tick();
    rt->tick();

    CHECK(rt->state() != RuntimeState::Shutdown);  // contained, no crash
    // Held last-known-good (default Home/0s): no wandering/distress alert fabricated.
    CHECK(fk.companion->count_of(companion::AlertKind::Wandering) == 0);
    CHECK(fk.companion->count_of(companion::AlertKind::Distress) == 0);
    // One sensing heads-up, edge-triggered (not one per tick), on the existing channel.
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "sensing unavailable") == 1);

    // Recovery: the fault clears and sensing resumes without a lingering alert storm.
    fk.location_source->fault = false;
    fk.location_source->set(safety::ZoneState::Home, std::chrono::seconds{0});
    rt->tick();
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "sensing unavailable") == 1);
}

}  // namespace

int main() {
    // Layer 1 — pure policy.
    test_wandering_risk_bands();
    test_distress_risk_bands();
    test_combined_decision();
    // Layer 2 — through the real runtime.
    test_wandering_confirmed_raises_wandering_alert();
    test_distress_confirmed_raises_distress_alert();
    test_conditions_are_edge_triggered();
    test_source_fault_is_contained_and_never_fabricates();
    return echo::test::report("safety-mgmt");
}
