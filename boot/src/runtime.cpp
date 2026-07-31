#include "echo/boot/runtime.hpp"
#include "echo/config.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

#include <cstdlib>
#include <exception>
#include <stdexcept>
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
    // Phase 18: the real power/thermal sources (documented stubs on this target — see
    // power_source.cpp). Tests inject deterministic fakes here instead.
    e.power_source   = power::make_power_source();
    e.thermal_source = power::make_thermal_source();
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
      power_source_(std::move(engines.power_source)),
      thermal_source_(std::move(engines.thermal_source)),
      perception_guard_("perception", watchdog_.perception_budget),
      cognitive_guard_("cognitive",   watchdog_.cognitive_budget),
      voice_guard_("voice",           watchdog_.voice_budget),
      memory_guard_("memory",         watchdog_.memory_budget) {
    // A caller that supplied no power/thermal sources (the Phase 17 fault-injection suite
    // constructs Engines without them) gets the real documented stubs, so power sensing is
    // always present — an unset field means "no injected power scenario", not "disabled".
    if (!power_source_)   power_source_   = power::make_power_source();
    if (!thermal_source_) thermal_source_ = power::make_thermal_source();
    // The thermal throttle scales the cognitive hang budget UP from this base; capture it
    // once so repeated scaling never compounds.
    base_cognitive_budget_ = cognitive_guard_.budget();
}

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
    // the (heavier) model warm-ups. Power/thermal SENSING is best-effort: a source that
    // fails to init leaves the device running full-rate (the policy fails safe to healthy)
    // rather than refusing to boot — losing power management must not brick the glasses.
    if (power_->initialize()      != Status::Ok) return Status::HardwareError;
    if (power_source_->initialize()   != Status::Ok)
        log_warn("boot", "battery source unavailable; running without battery-aware duty-cycling");
    if (thermal_source_->initialize() != Status::Ok)
        log_warn("boot", "thermal source unavailable; running without thermal-aware throttling");
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

// --- power & thermal management (Phase 18) ----------------------------------

power::PowerDecision Runtime::poll_power() noexcept {
    // Read both sources inside a local backstop. A source read is a cheap register/sysfs
    // read, not something that hangs, so a light try/catch (like the companion channel) is
    // enough — no worker-thread hang bound needed. On a fault we do NOT fabricate a reading:
    // we keep the last-known-good battery/thermal values and the decision derived from them,
    // and raise one caregiver heads-up. Failing to the last-known-good (default: healthy) is
    // deliberate — a transient gauge glitch must not cripple vision, but a persistent one is
    // surfaced, not hidden.
    try {
        auto battery = power_source_->read();
        auto thermal = thermal_source_->read();
        if (battery && thermal) {
            last_battery_  = battery.value();
            last_thermal_  = thermal.value();
            last_power_decision_ = power::decide(last_battery_, last_thermal_);
            if (power_sense_alerted_) {  // recovered
                log_info("power", "power/thermal sensing recovered");
                power_sense_alerted_ = false;
            }
            return last_power_decision_;
        }
        // A source returned a non-Ok Result (not a throw). Treat like a sensing fault below.
        throw std::runtime_error("power/thermal source returned a non-Ok read");
    } catch (const std::exception& e) {
        if (!power_sense_alerted_) {
            log_warn("power", std::string("power/thermal sensing unavailable (") + e.what() +
                                  "); holding last-known-good, not fabricating a reading");
            flag_power_condition("Power/thermal sensing unavailable");
            power_sense_alerted_ = true;
        }
        return last_power_decision_;  // last-known-good; never a guessed charge
    }
}

void Runtime::apply_power_decision(const power::PowerDecision& decision) {
    last_power_decision_ = decision;

    // Thermal throttle: relax the cognitive (LLM/inference) hang budget so a legitimately
    // slower throttled turn is not abandoned as if it were hung (integrates with the Phase 17
    // watchdog rather than fighting it). Scaling always starts from the captured base budget,
    // so it never compounds tick-over-tick.
    cognitive_guard_.set_budget(power::scaled_budget(base_cognitive_budget_, decision.inference));

    // Caregiver heads-ups through the SAME Phase-17 EngineDegraded alert path (constraint #2 —
    // no second device-health channel). Each is EDGE-TRIGGERED so a standing condition alerts
    // once, and the latch clears when the condition lifts.
    if (decision.thermal_elevated) {
        if (!thermal_alerted_) {
            flag_power_condition(std::string("Thermal ") + power::to_string(last_thermal_.state) +
                                 " — throttling inference (" + power::to_string(decision.inference) + ")");
            thermal_alerted_ = true;
        }
    } else {
        thermal_alerted_ = false;
    }

    if (decision.battery_low) {
        if (!battery_low_alerted_) {
            flag_power_condition("Low battery: " + std::to_string(last_battery_.percent) +
                                 "% — reducing vision sampling (" +
                                 power::to_string(decision.vision) + ")");
            battery_low_alerted_ = true;
        }
    } else {
        battery_low_alerted_ = false;
    }

    if (decision.battery_critical) {
        if (!battery_critical_alerted_) {
            on_battery_critical();
            battery_critical_alerted_ = true;
        }
    } else {
        battery_critical_alerted_ = false;
    }
}

bool Runtime::should_process_camera_frame() noexcept {
    const int interval = power::vision_sample_interval(last_power_decision_.vision);
    if (interval <= 0) return false;   // Off: voice-only, camera never processed
    if (interval == 1) return true;    // Full: every frame
    // Reduced: process 1 in `interval`. Counting frames (not fabricating skipped ones) is the
    // whole point — recognition just happens less often.
    const bool process = (vision_tick_counter_ % static_cast<unsigned>(interval)) == 0;
    ++vision_tick_counter_;
    return process;
}

void Runtime::on_battery_critical() {
    // A best-effort FINAL reminder-delivery pass before a possible shutdown: a critical
    // medication reminder should get one last chance to be spoken while there is still power.
    // deliver_due_reminders() is idempotent within a session (its in-session dedup), so this
    // never double-speaks a reminder already delivered this tick.
    //
    // HONEST SCOPE: without real battery hardware we cannot actually predict imminent
    // shutdown — this fires on a low-charge THRESHOLD, not a real "about to die" signal, and
    // there is no guaranteed post-alert power budget. The policy (deliver-before-loss, never
    // silence a reminder for power) is real and tested; the shutdown PREDICTION it rests on is
    // speculative until hardware exists (see docs/STATE.md).
    log_warn("power", "battery critical (" + std::to_string(last_battery_.percent) +
                          "%): best-effort final reminder delivery before possible shutdown");
    flag_power_condition("Critical battery: " + std::to_string(last_battery_.percent) +
                         "% — device may shut down soon");
    deliver_due_reminders();
}

void Runtime::flag_power_condition(const std::string& note) {
    // Reuse the Phase-17 caregiver alert path and its EngineDegraded kind — a low battery or
    // a hot temple is a device-health condition the caregiver should see, and the brief is
    // explicit that it must ride the EXISTING alert vocabulary, not a parallel mechanism.
    send_alert_guarded(companion::Alert{companion::AlertKind::EngineDegraded, now(), note});
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
    // Phase 18: read the real (sensed) battery/thermal constraints and apply the
    // duty-cycle / throttle policy — relax the cognitive hang budget under thermal
    // pressure, raise low-battery/thermal caregiver heads-ups, and run the critical-battery
    // reminder pass. Done before anything else so the decision governs this whole tick.
    const power::PowerDecision decision = poll_power();
    apply_power_decision(decision);

    // Let the legacy DVFS actor veto toward Throttled — now fed the REAL sensed values
    // (was a hardcoded placeholder). Thermal state maps to a representative °C when the
    // backend exposes no numeric sensor (the realistic state-only glasses case).
    const float temp_c = last_thermal_.soc_temp_c.value_or(
        last_thermal_.state == power::ThermalState::Hot  ? 75.0F :
        last_thermal_.state == power::ThermalState::Warm ? 60.0F : 35.0F);
    power_->evaluate(power::PowerState{last_battery_.percent, temp_c, last_battery_.charging});

    // Reminders on this same core tick (constraint #4). Runs every tick, before the
    // frame drain, so a due reminder fires even when no sensor frame arrived. Reminder
    // delivery is NEVER duty-cycled or throttled away — it is the one thing a low battery
    // must not silence.
    deliver_due_reminders();

    if (auto frame = sensors_.next_frame()) {
        // Battery/thermal vision duty-cycling: under a reduced/off cadence some camera
        // frames are DROPPED (slower, less-frequent recognition) — never run through a
        // fabricated result (constraint #1). Audio frames are never duty-cycled, so a
        // voice-only (vision-off) device stays fully responsive to speech.
        if (frame->modality == Modality::Camera && !should_process_camera_frame()) {
            log_info("power", std::string("vision duty-cycle: camera frame dropped (") +
                                  power::to_string(last_power_decision_.vision) + " cadence)");
        } else {
            process_frame(*frame);
        }
    }

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
    if (thermal_source_) thermal_source_->shutdown();
    if (power_source_)   power_source_->shutdown();
    if (power_)     power_->shutdown();
    if (memory_)    memory_->close();  // flush + release the store after the core stops using it
    log_info("runtime", "shutdown complete");
}

}  // namespace echo::boot
