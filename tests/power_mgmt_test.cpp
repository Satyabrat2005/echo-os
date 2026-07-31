// ECHO OS — power & thermal management test (Phase 18).
//
// Two layers, both in the dependency-free stub build (constraint #3 — no real sensor):
//
//   1. PURE POLICY: decide()/duty-cycle/throttle as total functions of scripted battery &
//      thermal readings — the threshold bands, the vision cadence, and the latency-budget
//      relaxation, asserted directly with no runtime in the loop.
//   2. THROUGH THE REAL RUNTIME: the same fault-injection-style DI seam Phase 17 built, now
//      also injecting deterministic FAKE power/thermal sources. Drive them across the defined
//      threshold transitions and assert the runtime's behaviour: vision is duty-cycled (and
//      NEVER fabricates a result for a dropped frame), the thermal throttle RELAXES the Phase
//      17 cognitive hang budget, low/critical-battery and thermal conditions surface through
//      the SAME EngineDegraded caregiver channel (not a parallel one), a critical battery
//      still delivers a due reminder, and a source read-fault neither crashes the loop nor
//      fabricates a charge level.
#include "echo/boot/runtime.hpp"

#include "echo/power/power_policy.hpp"
#include "echo/power/fake_power_source.hpp"

#include "fake_engines.hpp"
#include "check.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

using namespace echo;
using echo::test::FakePerception;
using echo::test::FakeCognitive;
using echo::test::FakeVoice;
using echo::test::FakeCompanion;
using echo::test::FakePower;
using echo::test::FakeMemory;

namespace {

// ===========================================================================
// Layer 1 — the pure policy. No runtime, no hardware: total functions of inputs.
// ===========================================================================

power::BatteryReading batt(std::uint8_t pct, bool charging = false) {
    power::BatteryReading b; b.percent = pct; b.charging = charging; return b;
}
power::ThermalReading therm(power::ThermalState s) {
    power::ThermalReading t; t.state = s; return t;
}

void test_battery_duty_cycle_bands() {
    using power::VisionDuty;
    // Full above 40%, Reduced 15–40%, Off below 15% (voice-only) — the brief's bands.
    CHECK(power::vision_duty_for_battery(batt(100)) == VisionDuty::Full);
    CHECK(power::vision_duty_for_battery(batt(41))  == VisionDuty::Full);
    CHECK(power::vision_duty_for_battery(batt(40))  == VisionDuty::Full);
    CHECK(power::vision_duty_for_battery(batt(39))  == VisionDuty::Reduced);
    CHECK(power::vision_duty_for_battery(batt(20))  == VisionDuty::Reduced);
    CHECK(power::vision_duty_for_battery(batt(15))  == VisionDuty::Reduced);
    CHECK(power::vision_duty_for_battery(batt(14))  == VisionDuty::Off);
    CHECK(power::vision_duty_for_battery(batt(3))   == VisionDuty::Off);
    // On a charger there is no need to conserve — full-rate regardless of charge.
    CHECK(power::vision_duty_for_battery(batt(3, /*charging=*/true)) == VisionDuty::Full);
}

void test_thermal_throttle_levels() {
    using power::Throttle;
    CHECK(power::throttle_for_thermal(power::ThermalState::Nominal) == Throttle::None);
    CHECK(power::throttle_for_thermal(power::ThermalState::Warm)    == Throttle::Warm);
    CHECK(power::throttle_for_thermal(power::ThermalState::Hot)     == Throttle::Hot);

    // The throttle only ever RELAXES the hang budget (scale >= 1.0), never tightens it.
    CHECK(power::latency_budget_scale(Throttle::None) == 1.0);
    CHECK(power::latency_budget_scale(Throttle::Warm) > 1.0);
    CHECK(power::latency_budget_scale(Throttle::Hot)  > power::latency_budget_scale(Throttle::Warm));

    const std::chrono::milliseconds base{200};
    CHECK(power::scaled_budget(base, Throttle::None) == std::chrono::milliseconds(200));
    CHECK(power::scaled_budget(base, Throttle::Warm) == std::chrono::milliseconds(300));
    CHECK(power::scaled_budget(base, Throttle::Hot)  == std::chrono::milliseconds(400));
    // Saturation: rounding must never shorten below the base.
    CHECK(power::scaled_budget(std::chrono::milliseconds(1), Throttle::Warm).count() >= 1);
}

void test_vision_sample_interval() {
    CHECK(power::vision_sample_interval(power::VisionDuty::Full)    == 1);
    CHECK(power::vision_sample_interval(power::VisionDuty::Reduced) == power::kReducedVisionSampleInterval);
    CHECK(power::vision_sample_interval(power::VisionDuty::Off)     == 0);
}

void test_combined_decision() {
    using power::VisionDuty;
    using power::Throttle;
    // Full battery but a HOT temple: thermal forces at-least-Reduced vision + Hot throttle,
    // even though the battery alone would run Full.
    {
        const auto d = power::decide(batt(100), therm(power::ThermalState::Hot));
        CHECK(d.vision == VisionDuty::Reduced);   // thermal floor, not battery
        CHECK(d.inference == Throttle::Hot);
        CHECK(d.thermal_elevated);
        CHECK(!d.battery_low);
        CHECK(!d.battery_critical);
    }
    // Critical battery AND warm: vision fully OFF (only a critical battery does that), and the
    // warm throttle still applies. Both flags set.
    {
        const auto d = power::decide(batt(4), therm(power::ThermalState::Warm));
        CHECK(d.vision == VisionDuty::Off);
        CHECK(d.inference == Throttle::Warm);
        CHECK(d.battery_low);
        CHECK(d.battery_critical);
        CHECK(d.thermal_elevated);
    }
    // Reduced-band battery, nominal thermal: Reduced vision, no throttle, low (not critical).
    {
        const auto d = power::decide(batt(30), therm(power::ThermalState::Nominal));
        CHECK(d.vision == VisionDuty::Reduced);
        CHECK(d.inference == Throttle::None);
        CHECK(d.battery_low);
        CHECK(!d.battery_critical);
        CHECK(!d.thermal_elevated);
    }
    // On a charger, a low reading raises NO conserve/shutdown flags.
    {
        const auto d = power::decide(batt(4, /*charging=*/true), therm(power::ThermalState::Nominal));
        CHECK(d.vision == VisionDuty::Full);
        CHECK(!d.battery_low);
        CHECK(!d.battery_critical);
    }
}

// ===========================================================================
// Layer 2 — through the REAL runtime with injected fake sources.
// ===========================================================================

// Non-owning handles to everything we injected.
struct Fakes {
    FakePerception*          perception     = nullptr;
    FakeCognitive*           cognitive      = nullptr;
    FakeVoice*               voice          = nullptr;
    FakeCompanion*           companion      = nullptr;
    FakeMemory*              memory         = nullptr;
    power::FakePowerSource*  power_source   = nullptr;
    power::FakeThermalSource* thermal_source = nullptr;
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
    auto perception    = std::make_unique<FakePerception>();
    auto cognitive     = std::make_unique<FakeCognitive>();
    auto voice         = std::make_unique<FakeVoice>();
    auto companion     = std::make_unique<FakeCompanion>();
    auto memory        = std::make_unique<FakeMemory>();
    auto power_mgr     = power::make_power_manager();  // real DVFS actor; not under test here
    auto power_source  = std::make_unique<power::FakePowerSource>();
    auto thermal_source = std::make_unique<power::FakeThermalSource>();

    out.perception     = perception.get();
    out.cognitive      = cognitive.get();
    out.voice          = voice.get();
    out.companion      = companion.get();
    out.memory         = memory.get();
    out.power_source   = power_source.get();
    out.thermal_source = thermal_source.get();

    boot::Runtime::Engines engines;
    engines.memory        = std::move(memory);
    engines.perception    = std::move(perception);
    engines.cognitive     = std::move(cognitive);
    engines.voice         = std::move(voice);
    engines.companion     = std::move(companion);
    engines.power         = std::move(power_mgr);
    engines.power_source  = std::move(power_source);
    engines.thermal_source = std::move(thermal_source);

    auto rt = std::make_unique<boot::Runtime>(std::move(engines), test_watchdog());
    CHECK(rt->boot() == Status::Ok);
    return rt;
}

SensorFrame frame_of(Modality m) {
    static std::uint8_t dummy[4] = {0, 0, 0, 0};
    SensorFrame f;
    f.modality    = m;
    f.data        = dummy;
    f.size        = sizeof(dummy);
    f.width       = (m == Modality::Camera) ? 4 : 0;
    f.height      = (m == Modality::Camera) ? 1 : 0;
    f.sample_rate = (m == Modality::Microphone) ? 16000 : 0;
    return f;
}

void tick_with(boot::Runtime& rt, Modality m) {
    CHECK(rt.offer_frame(frame_of(m)));
    rt.tick();
}

bool voice_said_contains(const FakeVoice& v, const std::string& needle) {
    for (const auto& u : v.spoken())
        if (u.text.find(needle) != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------------------
// 1. Full battery, nominal thermal: EVERY camera frame is processed (full-rate).
void test_full_rate_processes_every_camera_frame() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.power_source->set(100);
    fk.thermal_source->set(power::ThermalState::Nominal);

    for (int i = 0; i < 6; ++i) tick_with(*rt, Modality::Camera);
    CHECK(fk.perception->process_calls.load() == 6);  // sampled every frame
}

// ---------------------------------------------------------------------------
// 2. Reduced band (15–40%): the camera is sampled 1-in-N — slower recognition — and a
//    DROPPED frame produces NO fabricated result (cognitive is never consulted, nothing
//    is spoken for it). This is the Phase-17 "never guess" rule applied to duty-cycling.
void test_reduced_rate_subsamples_and_never_fabricates() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.power_source->set(30);  // reduced band, discharging
    fk.thermal_source->set(power::ThermalState::Nominal);

    const int cog_before = fk.cognitive->respond_calls.load();
    const auto spoke_before = fk.voice->spoken().size();

    // 8 camera frames at a 1-in-4 cadence → exactly 2 processed (frames 0 and 4).
    for (int i = 0; i < 8; ++i) tick_with(*rt, Modality::Camera);
    CHECK(fk.perception->process_calls.load() == 2);

    // The 6 DROPPED frames fabricated nothing: cognitive ran only for the 2 processed frames,
    // and no extra utterance was invented for a frame that was never perceived.
    CHECK(fk.cognitive->respond_calls.load() == cog_before + 2);
    // (Processed empty frames speak the fake's "ACK"; dropped frames add nothing beyond that.)
    CHECK(fk.voice->spoken().size() == spoke_before + 2);
}

// ---------------------------------------------------------------------------
// 3. Below the critical floor: vision is OFF (voice-only). Camera frames are never
//    processed, but MICROPHONE frames still are — the device stays responsive to speech.
void test_vision_off_is_voice_only() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.power_source->set(3);  // below critical floor → vision Off
    fk.thermal_source->set(power::ThermalState::Nominal);
    fk.memory->has_due = false;

    for (int i = 0; i < 5; ++i) tick_with(*rt, Modality::Camera);
    CHECK(fk.perception->process_calls.load() == 0);  // camera never processed
    CHECK(fk.cognitive->respond_calls.load() == 0);   // and nothing fabricated from it

    // A mic frame is still processed — voice-only, not fully deaf.
    tick_with(*rt, Modality::Microphone);
    CHECK(fk.perception->process_calls.load() == 1);
}

// ---------------------------------------------------------------------------
// 4. Thermal throttle integrates with the Phase-17 watchdog: a hot temple RELAXES the
//    cognitive hang budget (a legitimately slower throttled turn is not abandoned as hung),
//    and it returns to baseline when the temple cools.
void test_thermal_throttle_relaxes_cognitive_budget() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.power_source->set(100);

    const auto base = std::chrono::milliseconds(150);  // == test_watchdog cognitive budget

    fk.thermal_source->set(power::ThermalState::Nominal);
    rt->tick();
    CHECK(rt->cognitive_guard().budget() == base);

    fk.thermal_source->set(power::ThermalState::Hot);
    rt->tick();
    CHECK(rt->cognitive_guard().budget() == power::scaled_budget(base, power::Throttle::Hot));  // 300 ms
    CHECK(rt->cognitive_guard().budget() > base);

    fk.thermal_source->set(power::ThermalState::Warm);
    rt->tick();
    CHECK(rt->cognitive_guard().budget() == power::scaled_budget(base, power::Throttle::Warm));  // 225 ms

    fk.thermal_source->set(power::ThermalState::Nominal);
    rt->tick();
    CHECK(rt->cognitive_guard().budget() == base);  // scaling never compounds — back to baseline
}

// ---------------------------------------------------------------------------
// 5. Low battery and high thermal surface through the SAME Phase-17 EngineDegraded channel
//    (constraint #4 — no parallel device-health mechanism), and each is EDGE-TRIGGERED
//    (a standing condition alerts once, then re-arms when it clears).
void test_conditions_use_engine_degraded_channel_edge_triggered() {
    Fakes fk;
    auto rt = make_runtime(fk);

    // Low battery (30%), nominal thermal.
    fk.power_source->set(30);
    fk.thermal_source->set(power::ThermalState::Nominal);
    rt->tick();
    rt->tick();  // still low: must NOT re-alert every tick
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "Low battery") == 1);

    // Recover to full, then drop low again: the latch re-arms → a second alert.
    fk.power_source->set(100);
    rt->tick();
    fk.power_source->set(30);
    rt->tick();
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "Low battery") == 2);

    // Thermal warm rides the same channel, also once.
    fk.power_source->set(100);
    fk.thermal_source->set(power::ThermalState::Warm);
    rt->tick();
    rt->tick();
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "Thermal warm") == 1);
}

// ---------------------------------------------------------------------------
// 6. Critical battery still DELIVERS a due reminder (the low-battery reminder-priority
//    policy) and raises a critical-battery caregiver alert — a low battery must never
//    silence a medication reminder, even with vision off.
void test_critical_battery_still_delivers_reminder() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.memory->has_due = true;            // a medication reminder is due
    fk.power_source->set(3);              // critical, discharging → vision Off
    fk.thermal_source->set(power::ThermalState::Nominal);

    rt->tick();

    CHECK(voice_said_contains(*fk.voice, "take medication"));  // delivered despite critical power
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "Critical battery") >= 1);
    // Delivered exactly once this session even though the fake keeps it "pending".
    int reminders = 0;
    for (const auto& u : fk.voice->spoken())
        if (u.text.find("take medication") != std::string::npos) ++reminders;
    CHECK(reminders == 1);
    CHECK(rt->state() != RuntimeState::Shutdown);
}

// ---------------------------------------------------------------------------
// 7. A source read-fault neither crashes the loop nor fabricates a charge: the runtime
//    holds the last-known-good (default healthy) decision, raises one sensing alert, and
//    keeps running — vision is NOT crippled on a transient gauge glitch.
void test_source_fault_is_contained_and_never_fabricates() {
    Fakes fk;
    auto rt = make_runtime(fk);
    fk.power_source->fault = true;   // gauge read throws
    fk.thermal_source->set(power::ThermalState::Nominal);

    tick_with(*rt, Modality::Camera);
    tick_with(*rt, Modality::Camera);

    CHECK(rt->state() != RuntimeState::Shutdown);   // contained, no crash
    // Held last-known-good (healthy default): vision still full-rate, both camera frames run.
    CHECK(fk.perception->process_calls.load() == 2);
    // One sensing heads-up, edge-triggered (not one per tick).
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "sensing unavailable") == 1);

    // Recovery: the fault clears and sensing resumes without a lingering alert storm.
    fk.power_source->fault = false;
    fk.power_source->set(100);
    rt->tick();
    CHECK(fk.companion->count_note_contains(companion::AlertKind::EngineDegraded, "sensing unavailable") == 1);
}

}  // namespace

int main() {
    // Layer 1 — pure policy.
    test_battery_duty_cycle_bands();
    test_thermal_throttle_levels();
    test_vision_sample_interval();
    test_combined_decision();
    // Layer 2 — through the real runtime.
    test_full_rate_processes_every_camera_frame();
    test_reduced_rate_subsamples_and_never_fabricates();
    test_vision_off_is_voice_only();
    test_thermal_throttle_relaxes_cognitive_budget();
    test_conditions_use_engine_degraded_channel_edge_triggered();
    test_critical_battery_still_delivers_reminder();
    test_source_fault_is_contained_and_never_fabricates();
    return echo::test::report("power-mgmt");
}
