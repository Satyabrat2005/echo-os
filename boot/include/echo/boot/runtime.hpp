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

#include <memory>

namespace echo::boot {

class Runtime {
public:
    Runtime();
    ~Runtime();

    // Bring the system from cold to Ready: init every module in dependency order,
    // warm models, start sensor capture. Returns non-Ok if any critical stage
    // fails (the caller should surface a hardware fault, not spin).
    Status boot();

    // Run one iteration of the core loop: drain a frame, perceive, decide, speak,
    // and emit any caregiver alert. Separated from run() so it can be unit-tested
    // and single-stepped. Returns the current runtime state.
    RuntimeState tick();

    // Run the loop until stop() is requested. Blocking.
    void run();

    // Request the loop to exit at the next tick boundary (signal-safe).
    void request_stop() noexcept;

    // Orderly shutdown of every module.
    void shutdown();

    RuntimeState state() const noexcept { return state_; }

private:
    void set_state(RuntimeState s) noexcept;
    void handle_response(const cognitive::Response& response);
    // Check the memory engine for due reminders and speak them. Runs on the same
    // core tick as everything else — no second timer loop (constraint #3).
    void deliver_due_reminders();

    RuntimeState state_ = RuntimeState::Booting;
    bool         stop_requested_ = false;

    sensor::SensorPipeline                    sensors_;
    // Memory engine is constructed first and injected into the cognitive core, so
    // it must be declared before cognitive_ (members destroy in reverse order).
    std::unique_ptr<memory::IMemoryEngine>         memory_;
    std::unique_ptr<perception::IPerceptionEngine> perception_;
    std::unique_ptr<cognitive::ICognitiveCore>     cognitive_;
    std::unique_ptr<voice::IVoiceUi>               voice_;
    std::unique_ptr<companion::ICompanionSync>     companion_;
    std::unique_ptr<power::IPowerManager>          power_;
};

}  // namespace echo::boot
