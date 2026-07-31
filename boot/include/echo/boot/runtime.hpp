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

#include "echo/boot/engine_guard.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

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
    int max_consecutive_failures = 3;  // failures before the watchdog treats an engine as degraded
    int max_recoveries           = 3;  // in-process recovery attempts before escalating to reboot
};

// Default budgets, honoring ECHO_WATCHDOG_{PERCEPTION,COGNITIVE,VOICE,MEMORY}_MS.
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

    // True once the watchdog has exhausted in-process recovery of a hung engine and a
    // physical restart is the only remaining option (see docs/ARCHITECTURE.md).
    [[nodiscard]] bool reboot_required() const noexcept { return reboot_required_; }

    // Read-only health for assertions/telemetry.
    [[nodiscard]] const EngineGuard& perception_guard() const noexcept { return perception_guard_; }
    [[nodiscard]] const EngineGuard& cognitive_guard()  const noexcept { return cognitive_guard_; }
    [[nodiscard]] const EngineGuard& voice_guard()      const noexcept { return voice_guard_; }
    [[nodiscard]] const EngineGuard& memory_guard()     const noexcept { return memory_guard_; }

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

    // Fixed, known-good utterance spoken when the cognitive engine itself does not
    // respond (throw/hang). Distinct from the safe-mode confidence line — this is the
    // engine-not-responding case, NOT the low-confidence gate (constraint #1).
    static constexpr const char* kEngineFaultResponse =
        "Give me a moment — I'm having a little trouble just now.";
    // Spoken when ASR/wake fails on an audio frame: ask again rather than sit silent.
    static constexpr const char* kAsrRetryResponse =
        "Sorry, I didn't catch that. Could you say it again?";

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

    EngineGuard perception_guard_;
    EngineGuard cognitive_guard_;
    EngineGuard voice_guard_;
    EngineGuard memory_guard_;

    RecoveryState perception_recovery_;
    RecoveryState cognitive_recovery_;
    RecoveryState voice_recovery_;
    RecoveryState memory_recovery_;

    RuntimeState state_ = RuntimeState::Booting;
    bool         stop_requested_ = false;
    bool         reboot_required_ = false;

    std::string  memory_db_path_;  // remembered so the watchdog can reopen the store

    // Reminders already spoken this process session. Backstop so a reminder the wearer
    // has already heard does not repeat every tick when its mark_fired write FAILED
    // (disk full/corruption) and it therefore stays "pending" in the store.
    std::unordered_set<memory::ReminderId> delivered_this_session_;
};

}  // namespace echo::boot
