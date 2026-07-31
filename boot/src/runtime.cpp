#include "echo/boot/runtime.hpp"
#include "echo/config.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace echo::boot {
namespace {

// Read a millisecond budget from an env var, falling back to `fallback`.
std::chrono::milliseconds env_ms(const char* var, std::chrono::milliseconds fallback) {
    const char* v = std::getenv(var);  // NOLINT(concurrency-mt-unsafe) — read once at startup
    if (v == nullptr || *v == '\0') return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(v, &end, 10);
    if (end == v || parsed <= 0) return fallback;
    return std::chrono::milliseconds(parsed);
}

}  // namespace

WatchdogConfig default_watchdog_config() {
    WatchdogConfig c;
    c.perception_budget = env_ms("ECHO_WATCHDOG_PERCEPTION_MS", c.perception_budget);
    c.cognitive_budget  = env_ms("ECHO_WATCHDOG_COGNITIVE_MS",  c.cognitive_budget);
    c.voice_budget      = env_ms("ECHO_WATCHDOG_VOICE_MS",      c.voice_budget);
    c.memory_budget     = env_ms("ECHO_WATCHDOG_MEMORY_MS",     c.memory_budget);
    return c;
}

Runtime::Engines Runtime::default_engines() {
    Engines e;
    e.memory     = memory::make_memory_engine();
    e.perception = perception::make_perception_engine();
    // The cognitive core gets a NON-OWNING pointer to the memory engine. Moving the
    // unique_ptr into the bundle below does not change the pointee's address, so this
    // pointer stays valid for the runtime's lifetime.
    e.cognitive  = cognitive::make_cognitive_core({}, e.memory.get());
    e.voice      = voice::make_voice_ui();
    e.companion  = companion::make_companion_sync();
    e.power      = power::make_power_manager();
    return e;
}

Runtime::Runtime() : Runtime(default_engines(), default_watchdog_config()) {}

Runtime::Runtime(Engines engines, WatchdogConfig watchdog)
    : watchdog_(watchdog),
      memory_(std::move(engines.memory)),
      perception_(std::move(engines.perception)),
      cognitive_(std::move(engines.cognitive)),
      voice_(std::move(engines.voice)),
      companion_(std::move(engines.companion)),
      power_(std::move(engines.power)),
      perception_guard_("perception", watchdog_.perception_budget),
      cognitive_guard_("cognitive",   watchdog_.cognitive_budget),
      voice_guard_("voice",           watchdog_.voice_budget),
      memory_guard_("memory",         watchdog_.memory_budget) {}

Runtime::~Runtime() { shutdown(); }

void Runtime::set_state(RuntimeState s) noexcept {
    if (s != state_) {
        state_ = s;
        log_info("runtime", to_string(s));
    }
}

Status Runtime::boot() {
    set_state(RuntimeState::Booting);
    log_info("boot", "ECHO OS cold-boot sequence starting");

    // Open the on-device memory store. Best-effort: a store that fails to open leaves
    // the device running without recall (better than refusing to boot); the cognitive
    // core simply skips its memory branches (it checks is_open()).
    memory_db_path_ = config::memory_db();
    if (memory_->open(memory_db_path_) != Status::Ok)
        log_warn("boot", "memory store unavailable; running without recall");

    // Initialize in dependency order. Power first so throttling is available during
    // the (heavier) model warm-ups.
    if (power_->initialize()      != Status::Ok) return Status::HardwareError;
    if (perception_->initialize() != Status::Ok) return Status::HardwareError;
    if (cognitive_->initialize()  != Status::Ok) return Status::HardwareError;
    if (voice_->initialize()      != Status::Ok) return Status::HardwareError;

    // Companion link is best-effort: the device must work offline, so a failed connect
    // does not fail boot.
    if (companion_->connect(companion::Transport::Ble) != Status::Ok)
        log_warn("boot", "companion link unavailable; continuing offline");

    // Register sensor frontends and start capture.
    sensors_.add_source(sensor::make_camera_source());
    sensors_.add_source(sensor::make_microphone_source());
    sensors_.add_source(sensor::make_eeg_source());
    if (sensors_.start_all() != Status::Ok) return Status::HardwareError;

    set_state(RuntimeState::Ready);
    voice_->play_earcon("ready");
    log_info("boot", "ECHO OS ready");
    return Status::Ok;
}

bool Runtime::offer_frame(const SensorFrame& frame) noexcept {
    return sensors_.queue().push(frame);
}

// --- degraded-mode voice output ---------------------------------------------

void Runtime::speak_guarded(const voice::Utterance& utterance) {
    // Copy the utterance into the guarded call so a hung TTS worker (which we abandon)
    // never dereferences a caller-owned string that has since gone away.
    const Status s = voice_guard_.call_status([this, u = utterance]() { return voice_->speak(u); });
    if (s != Status::Ok) {
        // TTS itself is degraded. Best-effort audible cue so the wearer isn't left in
        // total silence; if even the earcon fails we log and carry on (can't do more).
        log_warn("runtime", "voice output degraded; attempting earcon fallback");
        (void)voice_guard_.call_status([this]() { return voice_->play_earcon("error"); });
    }
}

void Runtime::handle_response(const cognitive::Response& response) {
    speak_guarded(voice::to_utterance(response));

    // If the cognitive core wasn't sure, flag the caregiver (principle #5). This is the
    // UNCHANGED low-confidence path — the engine responded, just conservatively.
    if (response.flag_caregiver) {
        send_alert_guarded(companion::Alert{
            companion::AlertKind::SafeModeEngaged, now(), "System deferred to safe mode."});
        set_state(RuntimeState::SafeMode);
    }
}

void Runtime::on_perception_failure(Modality modality, Status s) {
    // The perception ENGINE failed (threw / hung / returned non-Ok) — not "recognized
    // nothing", which is a successful empty Perception handled downstream. Degrade by
    // the modality that failed:
    if (modality == Modality::Microphone) {
        // Wake-word / ASR path failed. Don't leave the wearer in silence and don't
        // crash the turn — ask them to repeat, calmly.
        log_warn("runtime", std::string("perception failed on audio (") + to_string(s) +
                                "); ASR degraded -> spoken retry");
        speak_guarded(voice::Utterance{kAsrRetryResponse, voice::Tone::Reassuring});
    } else {
        // Vision path failed. Fall back to voice-only for this frame. Crucially we do
        // NOT fabricate a face match (never guess) — we simply skip visual recall.
        log_warn("runtime", std::string("perception failed on camera (") + to_string(s) +
                                "); vision degraded -> voice-only, no recall");
    }
}

void Runtime::on_cognitive_failure(Status s) {
    // The cognitive ENGINE did not respond (threw or hung). This is a DIFFERENT failure
    // from "responded but unsure": the low-confidence safe-mode gate lives inside
    // respond() and returns a normal Ok result, so it never reaches here (constraint #1).
    // Here the call itself failed, so we speak a known-good line and flag the caregiver —
    // and we never fabricate an answer.
    log_warn("runtime", std::string(s == Status::Timeout ? "cognitive engine hung"
                                                          : "cognitive engine failed") +
                            " -> engine-fault fallback");
    speak_guarded(voice::Utterance{kEngineFaultResponse, voice::Tone::Reassuring});
    send_alert_guarded(companion::Alert{
        companion::AlertKind::EngineDegraded, now(),
        std::string("Cognitive engine fault (") + to_string(s) + ")"});
    set_state(RuntimeState::SafeMode);
}

void Runtime::process_frame(const SensorFrame& frame) {
    set_state(RuntimeState::Active);

    // sensors --> perception (contained). Capture the frame BY VALUE so an abandoned
    // hung worker can't dereference a frame view that has gone out of scope.
    auto perceived = perception_guard_.call(
        [this, frame]() { return perception_->process(frame); });
    if (!perceived) {
        on_perception_failure(frame.modality, perceived.status());
        return;
    }

    // perception --> cognitive (contained). The safe-mode confidence gate is INSIDE
    // respond() and untouched; this guard only catches the engine NOT responding.
    auto response = cognitive_guard_.call(
        [this, obs = perceived.value()]() { return cognitive_->respond(obs); });
    if (!response) {
        on_cognitive_failure(response.status());
        return;
    }

    handle_response(response.value());
}

void Runtime::deliver_due_reminders() {
    if (!memory_ || !memory_->is_open()) return;
    const memory::UnixTime now = memory::unix_now();

    // Read due reminders through the guard: even though the store claims non-throwing,
    // a real disk/corruption fault could surface here, and it must not crash the loop.
    auto pending = memory_guard_.call([this, now]() {
        return Result<std::vector<memory::ReminderRecord>>::ok(memory_->pending_deliveries(now));
    });
    if (!pending) {
        log_warn("runtime", "memory read (pending reminders) failed; no delivery this tick");
    } else {
        for (const auto& r : pending.value()) {
            // Deliver for THIS session even if persistence later fails (degraded != drop).
            // The in-session set stops a reminder repeating every tick when its mark_fired
            // write failed and it therefore stays "pending" in the store.
            if (delivered_this_session_.count(r.id) != 0) continue;
            speak_guarded(voice::Utterance{"Reminder: " + r.text + ".", voice::Tone::Alert});
            delivered_this_session_.insert(r.id);

            const Status s = memory_guard_.call_status(
                [this, id = r.id, now]() { return memory_->mark_fired(id, now); });
            if (s != Status::Ok)
                log_warn("runtime",
                         "memory write (mark_fired) failed; reminder delivered but not persisted");
        }
    }

    // Retention on the SAME tick (constraint #4). Contained: a failed write must not
    // crash the loop — the store simply isn't pruned this pass.
    const Status rs = memory_guard_.call_void([this, now]() { memory_->enforce_retention(now); });
    if (rs != Status::Ok)
        log_warn("runtime", "memory retention pass failed; store not pruned this tick");
}

// --- watchdog ---------------------------------------------------------------

Status Runtime::reinit_perception() {
    return perception_guard_.call_status([this]() {
        perception_->shutdown();
        return perception_->initialize();
    });
}
Status Runtime::reinit_cognitive() {
    return cognitive_guard_.call_status([this]() {
        cognitive_->shutdown();
        return cognitive_->initialize();
    });
}
Status Runtime::reinit_voice() {
    return voice_guard_.call_status([this]() {
        voice_->shutdown();
        return voice_->initialize();
    });
}
Status Runtime::reinit_memory() {
    return memory_guard_.call_status([this]() {
        memory_->close();
        return memory_->open(memory_db_path_);
    });
}

template <typename Reinit>
void Runtime::supervise(EngineGuard& guard, RecoveryState& recovery, Reinit&& reinit) {
    const EngineHealth& h = guard.health();
    const bool degraded =
        h.hung || h.consecutive_failures >= watchdog_.max_consecutive_failures;
    if (!degraded) {
        // Healthy again: forget this episode's recovery bookkeeping.
        recovery.attempts = 0;
        recovery.alerted  = false;
        return;
    }

    // In-process recovery exhausted for this episode.
    if (recovery.attempts >= watchdog_.max_recoveries) {
        // Only a genuinely HUNG engine (a call that never returns and could not be
        // re-initialized) escalates to a physical reboot — that is the class the
        // watchdog cannot fix in process. A merely-throwing but responsive engine keeps
        // serving degraded-mode fallbacks indefinitely rather than reboot the device.
        if (h.hung && !reboot_required_) {
            reboot_required_ = true;
            log_error("watchdog", std::string(guard.name()) +
                                      ": hung and unrecoverable in-process; physical reboot required");
            flag_engine_degraded(guard.name(), /*needs_reboot=*/true);
        }
        return;
    }

    ++recovery.attempts;
    log_warn("watchdog", std::string(guard.name()) + ": degraded -> in-process recovery attempt " +
                             std::to_string(recovery.attempts) + "/" +
                             std::to_string(watchdog_.max_recoveries));
    if (!recovery.alerted) {
        flag_engine_degraded(guard.name(), /*needs_reboot=*/false);
        recovery.alerted = true;
    }

    const Status s = reinit();
    if (s == Status::Ok) {
        guard.reset_health();  // fresh chance next turn
        log_info("watchdog", std::string(guard.name()) + ": in-process re-init returned Ok");
    } else {
        log_warn("watchdog", std::string(guard.name()) + ": re-init did not recover (" +
                                 to_string(s) + ")");
    }
}

void Runtime::watchdog_tick() {
    supervise(perception_guard_, perception_recovery_, [this]() { return reinit_perception(); });
    supervise(cognitive_guard_,  cognitive_recovery_,  [this]() { return reinit_cognitive(); });
    supervise(voice_guard_,      voice_recovery_,      [this]() { return reinit_voice(); });
    supervise(memory_guard_,     memory_recovery_,     [this]() { return reinit_memory(); });
}

void Runtime::send_alert_guarded(const companion::Alert& alert) noexcept {
    if (!companion_) return;
    // Companion is off the hot path and best-effort; a small local guard keeps even the
    // alert channel from taking the runtime down. No timeout here — a slow radio is not a
    // safety event the way a wedged engine is.
    try {
        (void)companion_->send_alert(alert);
    } catch (const std::exception& e) {
        log_warn("runtime", std::string("companion send_alert failed: ") + e.what());
    } catch (...) {
        log_warn("runtime", "companion send_alert failed (non-standard exception)");
    }
}

void Runtime::flag_engine_degraded(std::string_view engine, bool needs_reboot) {
    std::string note = (needs_reboot ? "Subsystem unrecoverable, restart needed: "
                                     : "Subsystem degraded: ");
    note += std::string(engine);
    send_alert_guarded(companion::Alert{companion::AlertKind::EngineDegraded, now(), std::move(note)});
}

// --- the tick ---------------------------------------------------------------

RuntimeState Runtime::tick() {
    // Final backstop (defense in depth): every engine call is already contained by its
    // guard, but nothing — power, sensors, our own wiring — is allowed to unwind out of
    // the loop and take the process down (the wearer relies on the device all day).
    try {
        return tick_impl();
    } catch (const std::exception& e) {
        log_error("runtime", std::string("uncontained exception in tick (backstop): ") + e.what());
        return state_;
    } catch (...) {
        log_error("runtime", "uncontained non-standard exception in tick (backstop)");
        return state_;
    }
}

RuntimeState Runtime::tick_impl() {
    // Let power-mgmt veto toward Throttled based on current constraints.
    power_->evaluate(power::PowerState{/*battery*/ 90, /*temp*/ 32.0F, /*charging*/ false});

    // Reminders on this same core tick (constraint #4). Runs every tick, before the
    // frame drain, so a due reminder fires even when no sensor frame arrived.
    deliver_due_reminders();

    if (auto frame = sensors_.next_frame())
        process_frame(*frame);

    // Watchdog runs on this SAME loop (constraint #4): detect hung/degraded engines,
    // attempt in-process recovery, escalate to a reboot request if unrecoverable.
    watchdog_tick();

    if (reboot_required_) return state_;  // leave the state as-is; run() honors the request

    if (state_ != RuntimeState::SafeMode) set_state(RuntimeState::Ready);
    return state_;
}

void Runtime::run() {
    while (!stop_requested_) {
        tick();
        if (reboot_required_) {
            // In-process recovery was exhausted for a hung engine. The honest fallback is
            // a supervised process/device restart (see RUNBOOK); the scaffold surfaces the
            // request and exits the loop cleanly rather than spin a wedged pipeline.
            log_error("runtime", "watchdog requested reboot; exiting run loop for supervised restart");
            break;
        }
        // On device the loop is event-driven off the capture ISR. For the scaffold a
        // single tick is enough to demonstrate the wiring; break so the reference binary
        // exits cleanly instead of spinning.
        break;
    }
}

void Runtime::request_stop() noexcept { stop_requested_ = true; }

void Runtime::shutdown() {
    if (state_ == RuntimeState::Shutdown) return;
    set_state(RuntimeState::Shutdown);
    sensors_.stop_all();
    if (companion_) companion_->disconnect();
    if (voice_)     voice_->shutdown();
    if (cognitive_) cognitive_->shutdown();
    if (perception_) perception_->shutdown();
    if (power_)     power_->shutdown();
    if (memory_)    memory_->close();  // flush + release the store after the core stops using it
    log_info("runtime", "shutdown complete");
}

}  // namespace echo::boot
