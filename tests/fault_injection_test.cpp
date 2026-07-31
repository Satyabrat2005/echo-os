// ECHO OS — fault injection test (Phase 17).
//
// This is the test that makes "fault tolerant" a claim rather than an aspiration.
// It builds the REAL runtime (boot/runtime.cpp) with fault-INJECTING fake engines,
// deliberately makes each engine boundary fail — throw, return a non-Ok Status, or
// HANG past its watchdog budget — and asserts three things at every boundary:
//
//   1. the process does not crash (containment): the runtime keeps ticking;
//   2. the DEFINED degraded behaviour actually happens (vision -> voice-only with no
//      fabricated face; ASR -> a spoken retry; LLM not responding -> a calm engine-
//      fault line, distinct from the low-confidence safe-mode gate; a failed memory
//      write -> the reminder is still delivered this session and not dropped);
//   3. the pipeline RECOVERS on the next turn once the fault clears, and a genuinely
//      hung engine that cannot be recovered in process escalates to a reboot request.
//
// It runs entirely in the dependency-free stub build (fakes, no real engines), the
// same discipline every other module keeps — fault injection needs no hardware.
#include "echo/boot/runtime.hpp"

#include "fake_engines.hpp"   // the shared deterministic fake engine set (Phase 17 + 18)
#include "check.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace echo;

namespace {

// The six deterministic fakes (FakePerception/Cognitive/Voice/Companion/Power/Memory), the
// Fault enum, and the hang helper all live in fake_engines.hpp now, shared with the Phase 18
// power/thermal suite. Bring them into scope so the harness below reads unchanged.
using namespace echo::test;

// --- harness -----------------------------------------------------------------

// Non-owning handles to the fakes we injected, so a test can drive faults and read
// what the runtime did. The unique_ptrs themselves live inside the Runtime.
struct Fakes {
    FakePerception* perception = nullptr;
    FakeCognitive*  cognitive  = nullptr;
    FakeVoice*      voice      = nullptr;
    FakeCompanion*  companion  = nullptr;
    FakeMemory*     memory     = nullptr;
};

boot::WatchdogConfig test_watchdog() {
    boot::WatchdogConfig c;
    // Small budgets so an injected hang (600 ms) trips the watchdog fast, but large
    // enough (150 ms) that a healthy call is never falsely flagged on a loaded runner.
    c.perception_budget = std::chrono::milliseconds(150);
    c.cognitive_budget  = std::chrono::milliseconds(150);
    c.voice_budget      = std::chrono::milliseconds(150);
    c.memory_budget     = std::chrono::milliseconds(150);
    c.max_consecutive_failures = 3;
    c.max_recoveries           = 3;
    return c;
}

// Build a booted runtime wired to fresh fakes. `out` receives the raw handles.
std::unique_ptr<boot::Runtime> make_runtime(Fakes& out) {
    auto perception = std::make_unique<FakePerception>();
    auto cognitive  = std::make_unique<FakeCognitive>();
    auto voice      = std::make_unique<FakeVoice>();
    auto companion  = std::make_unique<FakeCompanion>();
    auto memory     = std::make_unique<FakeMemory>();
    auto power      = std::make_unique<FakePower>();

    out.perception = perception.get();
    out.cognitive  = cognitive.get();
    out.voice      = voice.get();
    out.companion  = companion.get();
    out.memory     = memory.get();

    boot::Runtime::Engines engines;
    engines.memory     = std::move(memory);
    engines.perception = std::move(perception);
    engines.cognitive  = std::move(cognitive);
    engines.voice      = std::move(voice);
    engines.companion  = std::move(companion);
    engines.power      = std::move(power);

    auto rt = std::make_unique<boot::Runtime>(std::move(engines), test_watchdog());
    CHECK(rt->boot() == Status::Ok);  // fakes are healthy at boot
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

// Offer one frame of the given modality and run a tick.
void tick_with(boot::Runtime& rt, Modality m) {
    CHECK(rt.offer_frame(frame_of(m)));
    rt.tick();
}

const char* kEngineFaultLine = "Give me a moment — I'm having a little trouble just now.";
const char* kAsrRetryLine    = "Sorry, I didn't catch that. Could you say it again?";

bool voice_said(const FakeVoice& v, const std::string& text) {
    for (const auto& u : v.spoken()) if (u.text == text) return true;
    return false;
}

// ---------------------------------------------------------------------------
// 1. Every engine boundary, every fault type: the process survives and keeps
//    ticking. A single transient never triggers a reboot.
void test_no_fault_crashes_the_process() {
    for (const Fault f : {Fault::Throw, Fault::Error, Fault::Hang}) {
        Fakes fk;
        auto rt = make_runtime(fk);

        fk.perception->fault = f;               tick_with(*rt, Modality::Camera);
        fk.perception->fault = Fault::None;
        fk.cognitive->fault = f;                tick_with(*rt, Modality::Microphone);
        fk.cognitive->fault = Fault::None;
        fk.voice->fault = f;                    tick_with(*rt, Modality::Microphone);
        fk.voice->fault = Fault::None;
        fk.memory->has_due = true;
        fk.memory->write_fault = f;             rt->tick();
        fk.memory->pending_fault = f;           rt->tick();
        fk.memory->retention_fault = f;         rt->tick();

        // Survived every boundary fault. A single transient of each is not a reboot.
        CHECK(rt->state() != RuntimeState::Shutdown);
        CHECK(!rt->reboot_required());
    }
}

// ---------------------------------------------------------------------------
// 2. Vision failure -> voice-only, and NO fabricated face. When perception fails on
//    a camera frame, the cognitive core is never consulted and nothing is spoken:
//    we degrade to voice-only silently rather than invent a recognized person.
void test_vision_failure_is_voice_only_no_fabrication() {
    for (const Fault f : {Fault::Throw, Fault::Error, Fault::Hang}) {
        Fakes fk;
        auto rt = make_runtime(fk);

        const int cog_before   = fk.cognitive->respond_calls.load();
        const auto spoken_before = fk.voice->spoken().size();

        fk.perception->fault = f;
        tick_with(*rt, Modality::Camera);

        // No crash, no reboot for a single camera fault.
        CHECK(!rt->reboot_required());
        // Cognitive was NOT called (no fabricated observation reached it).
        CHECK(fk.cognitive->respond_calls.load() == cog_before);
        // Nothing was spoken — no fabricated "I see <person>" (never guess).
        CHECK(fk.voice->spoken().size() == spoken_before);
    }
}

// ---------------------------------------------------------------------------
// 3. ASR failure -> a calm spoken retry, not silence and not a crash.
void test_asr_failure_speaks_retry() {
    for (const Fault f : {Fault::Throw, Fault::Error, Fault::Hang}) {
        Fakes fk;
        auto rt = make_runtime(fk);

        fk.perception->fault = f;
        tick_with(*rt, Modality::Microphone);

        CHECK(voice_said(*fk.voice, kAsrRetryLine));
        CHECK(!rt->reboot_required());
        // Spoken reassuringly.
        bool reassuring = false;
        for (const auto& u : fk.voice->spoken())
            if (u.text == kAsrRetryLine && u.tone == voice::Tone::Reassuring) reassuring = true;
        CHECK(reassuring);
    }
}

// ---------------------------------------------------------------------------
// 4. LLM not responding (throw/hang) -> a calm engine-fault line + a caregiver
//    EngineDegraded alert. This is DISTINCT from the low-confidence safe-mode gate,
//    which is an Ok response that speaks its own line and raises SafeModeEngaged
//    (constraint #1: the two failure modes must not be conflated).
void test_llm_not_responding_is_distinct_from_safe_mode_gate() {
    // (a) The confidence gate: cognitive RESPONDS (Ok) with a safe-mode result.
    {
        Fakes fk;
        auto rt = make_runtime(fk);
        fk.cognitive->simulate_gate = true;
        tick_with(*rt, Modality::Microphone);

        CHECK(voice_said(*fk.voice, FakeCognitive::kGateLine));       // spoke the gate line
        CHECK(!voice_said(*fk.voice, kEngineFaultLine));             // NOT the engine-fault line
        CHECK(fk.companion->count_of(companion::AlertKind::SafeModeEngaged) >= 1);
        CHECK(fk.companion->count_of(companion::AlertKind::EngineDegraded) == 0);
        CHECK(rt->state() == RuntimeState::SafeMode);
    }
    // (b) The engine not responding: cognitive THROWS / HANGS.
    for (const Fault f : {Fault::Throw, Fault::Hang}) {
        Fakes fk;
        auto rt = make_runtime(fk);
        fk.cognitive->fault = f;
        tick_with(*rt, Modality::Microphone);

        CHECK(voice_said(*fk.voice, kEngineFaultLine));              // spoke the engine-fault line
        CHECK(!voice_said(*fk.voice, FakeCognitive::kGateLine));     // NOT the gate line
        CHECK(fk.companion->count_of(companion::AlertKind::EngineDegraded) >= 1);
        CHECK(rt->state() == RuntimeState::SafeMode);
        CHECK(!rt->reboot_required());  // one transient is not a reboot
    }
}

// ---------------------------------------------------------------------------
// 5. Memory write failure -> the reminder is STILL delivered this session and is NOT
//    dropped, the process does not crash, and it is not repeated every tick even
//    though it stays "pending" because the write failed.
void test_memory_write_failure_still_delivers_once() {
    for (const Fault f : {Fault::Throw, Fault::Error}) {
        Fakes fk;
        auto rt = make_runtime(fk);
        fk.memory->has_due    = true;
        fk.memory->write_fault = f;   // mark_fired fails, so the store keeps returning it

        rt->tick();  // tick 1: should speak the reminder despite the failing write
        rt->tick();  // tick 2: must NOT repeat it (in-session dedup backstop)

        int reminders = 0;
        for (const auto& u : fk.voice->spoken())
            if (u.text.find("take medication") != std::string::npos) ++reminders;
        CHECK(reminders == 1);          // delivered exactly once, not dropped, not repeated
        CHECK(!rt->reboot_required());
        CHECK(rt->state() != RuntimeState::Shutdown);
    }
}

// ---------------------------------------------------------------------------
// 6. Watchdog: a transient fault clears and the pipeline recovers on the next turn;
//    a persistent HANG that cannot be re-initialized escalates to a reboot request.
void test_watchdog_recovers_transient_and_escalates_persistent_hang() {
    // (a) transient: one perception throw, then healthy -> next turn is normal, no reboot.
    {
        Fakes fk;
        auto rt = make_runtime(fk);

        fk.perception->fault = Fault::Throw;
        tick_with(*rt, Modality::Microphone);       // fails -> ASR retry, contained
        const int cog_before = fk.cognitive->respond_calls.load();

        fk.perception->fault = Fault::None;         // fault clears
        tick_with(*rt, Modality::Microphone);       // next turn proceeds all the way through

        CHECK(fk.cognitive->respond_calls.load() == cog_before + 1);  // pipeline flows again
        CHECK(voice_said(*fk.voice, "ACK"));                          // a normal turn happened
        CHECK(!rt->reboot_required());
        CHECK(fk.perception->fault.load() == Fault::None);
    }
    // (b) persistent hang: perception hangs forever -> in-process recovery is exhausted
    //     -> reboot required. Bounded number of ticks (max_recoveries + slack).
    {
        Fakes fk;
        auto rt = make_runtime(fk);
        fk.perception->fault = Fault::Hang;

        for (int i = 0; i < 6 && !rt->reboot_required(); ++i)
            tick_with(*rt, Modality::Camera);

        CHECK(rt->reboot_required());  // escalated to a physical-restart request
        // The hang detector actually detected a hang (a worker was abandoned).
        CHECK(rt->perception_guard().health().hung);
    }
}

}  // namespace

int main() {
    test_no_fault_crashes_the_process();
    test_vision_failure_is_voice_only_no_fabrication();
    test_asr_failure_speaks_retry();
    test_llm_not_responding_is_distinct_from_safe_mode_gate();
    test_memory_write_failure_still_delivers_once();
    test_watchdog_recovers_transient_and_escalates_persistent_hang();
    return echo::test::report("fault-injection");
}
