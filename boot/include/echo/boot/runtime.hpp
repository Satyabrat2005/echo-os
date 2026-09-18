// ECHO OS — the runtime: boot sequence + the core perception-to-response loop.
//
// This module owns every other module and wires them into the single hot path:
//
//   sensors --> perception --> cognitive core --> voice UI
//                                   |
//                                   +--> companion sync (alerts/status)
//                power-mgmt observes the whole loop and sets the profile.
//
// Boot is optimized for fast cold-start to Ready: initialize in dependency order,
// warm the models, then park in Idle awaiting a wake-word.
//
// Phase 17 — fault tolerance. Every engine boundary now runs through an EngineGuard
// (see engine_guard.hpp) so a throw or a hang in wake-word / ASR / vision / LLM /
// TTS / memory is CONTAINED at the call site instead of unwinding through this loop
// or wedging the device. Each engine has a deliberate degraded-mode behaviour, and a
// watchdog on this same loop attempts in-process recovery of a hung engine, escalating
// to a physical-reboot request only when in-process recovery is exhausted. What that
// watchdog can and cannot recover is documented honestly in docs/ARCHITECTURE.md.
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include "echo/sensor/sensor_source.hpp"
#include "echo/memory/memory_engine.hpp"
#include "echo/perception/perception_engine.hpp"
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/voice/voice_ui.hpp"
#include "echo/companion/companion_sync.hpp"
#include "echo/power/power_manager.hpp"
#include "echo/power/power_source.hpp"
#include "echo/power/power_policy.hpp"
#include "echo/safety/safety_source.hpp"
#include "echo/safety/safety_policy.hpp"

#include "echo/boot/engine_guard.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace echo::boot {

// Watchdog / containment tuning. Budgets are HANG-detection thresholds — generously
// above real per-stage latency (a laptop LLM turn is ~1 s), not the 120 ms latency
// budget. A call that outruns its budget is treated as hung and abandoned. Defaults
// come from default_watchdog_config() (env-overridable); tests pass small budgets so
// an injected hang trips quickly.
struct WatchdogConfig {
    std::chrono::milliseconds perception_budget{750};
    std::chrono::milliseconds cognitive_budget{2500};
    std::chrono::milliseconds voice_budget{750};
    std::chrono::milliseconds memory_budget{750};
    // Phase 22. The caregiver transport gets its own budget, generously large: it is the
    // one boundary that talks to something outside the device, and a peer that is slow is
    // NOT a device fault. It is separate from memory_budget for a reason that matters —
    // a wedged radio must never be recorded against the memory engine, because memory
    // failures escalate to recovery and eventually to a reboot, and rebooting a device
    // because a caregiver's phone went out of range would be an absurd thing to do to
    // the person wearing it.
    std::chrono::milliseconds companion_budget{2000};
    int max_consecutive_failures = 3;  // failures before the watchdog treats an engine as degraded
    int max_recoveries           = 3;  // in-process recovery attempts before escalating to reboot
};

// Default budgets, honoring ECHO_WATCHDOG_{PERCEPTION,COGNITIVE,VOICE,MEMORY,COMPANION}_MS.
WatchdogConfig default_watchdog_config();

class Runtime {
public:
    // The six engines the runtime drives. Bundled so tests can inject fault-injecting
    // fakes without the runtime reaching for the real factories (the same seam the
    // fault-injection suite uses to make each boundary fail on demand).
    struct Engines {
        std::unique_ptr<memory::IMemoryEngine>         memory;
        std::unique_ptr<perception::IPerceptionEngine> perception;
        std::unique_ptr<cognitive::ICognitiveCore>     cognitive;
        std::unique_ptr<voice::IVoiceUi>               voice;
        std::unique_ptr<companion::ICompanionSync>     companion;
        std::unique_ptr<power::IPowerManager>          power;
        // Phase 18 power/thermal SENSING boundary. Left null by a caller that doesn't care
        // about power (e.g. the Phase 17 fault-injection suite) — the runtime then fills them
        // with the real documented stubs, so an unset field means "no injected power scenario",
        // not "power management disabled". The power-mgmt suite injects deterministic fakes here.
        std::unique_ptr<power::IPowerSource>           power_source;
        std::unique_ptr<power::IThermalSource>         thermal_source;
        // Phase 23 wandering/distress SENSING boundary. Same "null means no injected
        // scenario, not disabled" convention as power_source/thermal_source above — the
        // runtime fills an unset field with the real documented stub. The safety-mgmt
        // suite injects deterministic fakes here.
        std::unique_ptr<safety::ILocationSource>       location_source;
        std::unique_ptr<safety::IArousalSource>        arousal_source;
    };

    // Production: build the real/stub engines via their factories.
    Runtime();
    // Dependency-injection: caller supplies the engines and watchdog tuning. Used by
    // the fault-injection suite; also the natural seam for any future host that wants
    // to compose the runtime differently.
    Runtime(Engines engines, WatchdogConfig watchdog);
    ~Runtime();

    Runtime(const Runtime&)            = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&)                 = delete;
    Runtime& operator=(Runtime&&)      = delete;

    // Bring the system from cold to Ready: init every module in dependency order,
    // warm models, start sensor capture. Returns non-Ok if any critical stage
    // fails (the caller should surface a hardware fault, not spin).
    Status boot();

    // Run one iteration of the core loop: drain a frame, perceive, decide, speak,
    // emit any caregiver alert, and run the watchdog. Separated from run() so it can
    // be unit-tested and single-stepped. Returns the current runtime state.
    RuntimeState tick();

    // Run the loop until stop() is requested. Blocking.
    void run();

    // Request the loop to exit at the next tick boundary (signal-safe).
    void request_stop() noexcept;

    // Orderly shutdown of every module.
    void shutdown();

    RuntimeState state() const noexcept { return state_; }

    // --- Test / host seams (Phase 17) ---------------------------------------

    // Enqueue a frame for the next tick to drain. This is the hook a real sensor ISR
    // would call; the fault-injection suite uses it to feed specific modalities.
    // Returns false if the capture queue was full (frame dropped).
    bool offer_frame(const SensorFrame& frame) noexcept;

    // Override the wall clock the reminder scheduler reads (defaults to memory::unix_now).
    // The store's methods already take `now` as a parameter so recurrence is deterministic
    // under test; this closes the last gap — the runtime itself used to read the real clock
    // in deliver_due_reminders(), which made simulated-time testing of the scheduler
    // impossible. The longevity-soak harness injects a virtual clock here to compress a
    // multi-day run into thousands of fast ticks; production never calls this. Set before
    // ticking; read on the tick thread, so no synchronization is needed.
    void set_clock(std::function<memory::UnixTime()> clock) noexcept {
        if (clock) clock_ = std::move(clock);
    }

    // True once the watchdog has exhausted in-process recovery of a hung engine and a
    // physical restart is the only remaining option (see docs/ARCHITECTURE.md).
    [[nodiscard]] bool reboot_required() const noexcept { return reboot_required_; }

    // Read-only health for assertions/telemetry.
    [[nodiscard]] const EngineGuard& perception_guard() const noexcept { return perception_guard_; }
    [[nodiscard]] const EngineGuard& cognitive_guard()  const noexcept { return cognitive_guard_; }
    [[nodiscard]] const EngineGuard& voice_guard()      const noexcept { return voice_guard_; }
    [[nodiscard]] const EngineGuard& memory_guard()     const noexcept { return memory_guard_; }
    [[nodiscard]] const EngineGuard& companion_guard()  const noexcept { return companion_guard_; }

private:
    static Engines default_engines();

    RuntimeState tick_impl();
    void set_state(RuntimeState s) noexcept;

    // One frame through perception -> cognitive -> voice, each stage contained.
    void process_frame(const SensorFrame& frame);
    void handle_response(const cognitive::Response& response);

    // Degraded-mode policy (deliberate, per engine — never a crash, never a fabrication).
    void on_perception_failure(Modality modality, Status s);
    void on_cognitive_failure(Status s);
    void speak_guarded(const voice::Utterance& utterance);

    // Check the memory engine for due reminders and speak them. Runs on the same
    // core tick as everything else — no second timer loop (constraint #4).
    void deliver_due_reminders();

    // --- Power & thermal management (Phase 18), on the SAME tick (constraint #3/#4) ---

    // Read the power/thermal sources and derive the decision. Contained: a source that
    // throws must not take the loop down, and must NEVER yield a fabricated charge — on a
    // read fault we return the last-known-good decision and raise one caregiver heads-up.
    power::PowerDecision poll_power() noexcept;

    // Apply a decision: relax the cognitive hang budget for the thermal throttle, and raise
    // (edge-triggered) low-battery / critical-battery / thermal caregiver alerts through the
    // SAME Phase-17 EngineDegraded path — never a second device-health channel (constraint #2).
    void apply_power_decision(const power::PowerDecision& decision);

    // Vision duty-cycle gate: true if this camera frame should be processed under the current
    // cadence. When it returns false the frame is simply DROPPED (slower recognition), never
    // run through a fabricated result (constraint #1). Audio frames are never duty-cycled.
    bool should_process_camera_frame() noexcept;

    // Edge-triggered when the battery first crosses the critical floor: a best-effort final
    // reminder-delivery pass before a possible shutdown. Honestly speculative without real
    // battery hardware to predict imminent shutdown — see docs/STATE.md.
    void on_battery_critical();

    // A power/thermal caregiver heads-up, reusing the Phase-17 EngineDegraded alert path.
    void flag_power_condition(const std::string& note);

    // --- Wandering & distress detection (Phase 23), on the SAME tick ---------

    // Read the location/arousal sources and derive the decision. Contained exactly like
    // poll_power(): a source that throws must not take the loop down, and must NEVER yield
    // a fabricated zone/arousal state — on a read fault we return the last-known-good
    // decision and raise one caregiver heads-up on the EngineDegraded channel (a sensing
    // fault is a device-health condition, not a wandering/distress event).
    safety::SafetyDecision poll_safety() noexcept;

    // Apply a decision: edge-triggered AlertKind::Wandering / AlertKind::Distress alerts
    // (their first-ever production producers) when a risk becomes Confirmed. Unlike power's
    // decision this gates nothing else in the tick — its only job is alerting.
    void apply_safety_decision(const safety::SafetyDecision& decision);

    // Raise a wandering/distress caregiver alert through the existing send_alert_guarded()
    // choke point — the same one every other alert producer in this runtime uses.
    void flag_wandering_distress(companion::AlertKind kind, const std::string& note);

    // --- Watchdog (Phase 17), run on the SAME tick (constraint #4) -----------
    struct RecoveryState { int attempts = 0; bool alerted = false; };

    void watchdog_tick();
    template <typename Reinit>
    void supervise(EngineGuard& guard, RecoveryState& recovery, Reinit&& reinit);

    Status reinit_perception();
    Status reinit_cognitive();
    Status reinit_voice();
    Status reinit_memory();

    // Caregiver notifications for subsystem faults (best-effort, contained).
    void send_alert_guarded(const companion::Alert& alert) noexcept;
    void flag_engine_degraded(std::string_view engine, bool needs_reboot);

    // --- Phase 22: the caregiver boundary ------------------------------------

    // Record a countable event for the digest (safe-mode engagements, abstentions).
    // Guarded and best-effort: a store that can't take a counter must never be able
    // to break the turn that produced it.
    void log_digest_event(memory::EventKind kind, const char* summary) noexcept;

    // Re-push consent, drain validated inbound commands, and push a digest when the
    // heartbeat is due. Every call into memory/companion/voice from here goes
    // through the existing EngineGuards, so a hung radio or a wedged store degrades
    // exactly like every other engine fault (Phase 17) instead of stalling the tick
    // that owes the wearer their reminders.
    void caregiver_tick();

    // Fixed, known-good utterance spoken when the cognitive engine itself does not
    // respond (throw/hang). Distinct from the safe-mode confidence line — this is the
    // engine-not-responding case, NOT the low-confidence gate (constraint #1).
    static constexpr const char* kEngineFaultResponse =
        "Give me a moment — I'm having a little trouble just now.";
    // Spoken when ASR/wake fails on an audio frame: ask again rather than sit silent.
    static constexpr const char* kAsrRetryResponse =
        "Sorry, I didn't catch that. Could you say it again?";

    // --- Repeated-abstention trend (Phase 21) --------------------------------
    // One "I don't have a record of that" is ECHO working correctly. Several in a
    // row is a signal about the device or the wearer that a caregiver should see:
    // a store that failed to open, a store that was never populated, or someone
    // asking the same unanswerable question repeatedly. That is a TREND, which is
    // why it lives on the runtime (which sees every turn) rather than in the
    // cognitive core (which sees one).
    //
    // It rides AlertKind::LowConfidenceTrend — an enumerator that has sat unused in
    // companion-sync since Phase 1 and describes exactly this. Same discipline as
    // ADR-17 reusing EngineDegraded for power conditions: extend the existing
    // caregiver vocabulary, never grow a parallel one.
    static constexpr int kUnverifiedTrendThreshold = 3;
    int unverified_streak_ = 0;

    // Declaration order matters. The guards are declared AFTER the engines so that at
    // shutdown they destruct FIRST — reaping/joining any abandoned worker threads while
    // the engines those workers reference are still alive. watchdog_ precedes the guards
    // because their budgets are read from it during construction.
    WatchdogConfig watchdog_;

    sensor::SensorPipeline                         sensors_;
    // Memory engine is constructed first and injected into the cognitive core, so it
    // must be declared before cognitive_ (members destroy in reverse order).
    std::unique_ptr<memory::IMemoryEngine>         memory_;
    std::unique_ptr<perception::IPerceptionEngine> perception_;
    std::unique_ptr<cognitive::ICognitiveCore>     cognitive_;
    std::unique_ptr<voice::IVoiceUi>               voice_;
    std::unique_ptr<companion::ICompanionSync>     companion_;
    std::unique_ptr<power::IPowerManager>          power_;
    std::unique_ptr<power::IPowerSource>           power_source_;    // Phase 18
    std::unique_ptr<power::IThermalSource>         thermal_source_;  // Phase 18
    std::unique_ptr<safety::ILocationSource>       location_source_; // Phase 23
    std::unique_ptr<safety::IArousalSource>        arousal_source_;  // Phase 23

    EngineGuard perception_guard_;
    EngineGuard cognitive_guard_;
    EngineGuard voice_guard_;
    EngineGuard memory_guard_;
    // Phase 22. Deliberately NOT supervised by watchdog_tick(): there is nothing to
    // reinitialize (the peer is not ours to restart) and an unreachable caregiver is a
    // normal condition, not a degraded device. The guard is here for containment and
    // hang-bounding only — so a transport that never returns costs one tick's caregiver
    // work, not the tick itself.
    EngineGuard companion_guard_;

    RecoveryState perception_recovery_;
    RecoveryState cognitive_recovery_;
    RecoveryState voice_recovery_;
    RecoveryState memory_recovery_;

    RuntimeState state_ = RuntimeState::Booting;
    bool         stop_requested_ = false;
    bool         reboot_required_ = false;

    // --- Power & thermal state (Phase 18) -----------------------------------
    power::BatteryReading last_battery_;                         // last good read (defaults 100%/discharging)
    power::ThermalReading last_thermal_;                         // last good read (defaults nominal)
    power::PowerDecision  last_power_decision_;                  // last-known-good decision (fail to this, never fabricate)
    std::chrono::milliseconds base_cognitive_budget_{};          // un-throttled cognitive hang budget (thermal scales from this)
    unsigned vision_tick_counter_ = 0;                           // camera-frame counter for the reduced-cadence gate
    // Edge-trigger latches so a standing condition alerts the caregiver ONCE, not every tick.
    bool power_sense_alerted_      = false;
    bool battery_low_alerted_      = false;
    bool battery_critical_alerted_ = false;
    bool thermal_alerted_          = false;

    // --- Wandering & distress state (Phase 23) -------------------------------
    safety::LocationReading last_location_;                       // last good read (defaults Home/0s)
    safety::ArousalReading  last_arousal_;                        // last good read (defaults Calm/0s)
    safety::SafetyDecision  last_safety_decision_;                // last-known-good decision (fail to this, never fabricate)
    // Edge-trigger latches so a standing condition alerts the caregiver ONCE, not every tick.
    bool safety_sense_alerted_ = false;  // sensing-fault heads-up (rides EngineDegraded)
    bool wandering_alerted_    = false;  // AlertKind::Wandering
    bool distress_alerted_     = false;  // AlertKind::Distress

    std::string  memory_db_path_;  // remembered so the watchdog can reopen the store

    // --- Phase 22: the caregiver boundary ------------------------------------
    // How often a digest goes out when consent exists. Once an hour: frequent enough
    // that "the 9am reminder was acknowledged" is answerable within the hour,
    // infrequent enough that the feed is a status check rather than a live trace of
    // someone's day. The counts themselves cover a rolling 24h window.
    static constexpr memory::UnixTime kDigestIntervalSeconds = 3600;
    memory::UnixTime last_digest_at_ = 0;

    // The wall clock the reminder scheduler reads. Defaults to the real Unix clock;
    // overridable via set_clock() so a soak harness can drive simulated time.
    std::function<memory::UnixTime()> clock_{memory::unix_now};

    // The occurrence of each reminder already spoken this process session, keyed by
    // reminder id -> the `due` time of the delivered occurrence. Backstop so a reminder
    // the wearer has already heard does not repeat every tick when its mark_fired write
    // FAILED (disk full/corruption) and it therefore stays "pending" in the store.
    //
    // Keying by OCCURRENCE (the due time), not just the id, is deliberate and fixes a
    // longevity bug found by the Phase 20 soak: a recurring reminder that is acknowledged
    // re-arms to a NEW due time (memory_engine acknowledge_reminder), and an id-only set
    // would suppress that legitimately-new occurrence forever — so after its first
    // delivery a daily reminder would go silent until reboot. Comparing the stored due to
    // the pending occurrence's due lets a re-armed occurrence through while still
    // squelching the SAME occurrence repeating because its write failed. A single entry
    // per id (overwritten each occurrence) keeps this bounded by the reminder count, not
    // by uptime.
    std::unordered_map<memory::ReminderId, memory::UnixTime> delivered_occurrence_;
};

}  // namespace echo::boot
