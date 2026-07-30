#include "echo/boot/runtime.hpp"
#include "echo/config.hpp"
#include "echo/latency.hpp"
#include "echo/log.hpp"

namespace echo::boot {

Runtime::Runtime()
    : memory_(memory::make_memory_engine()),
      perception_(perception::make_perception_engine()),
      // The cognitive core gets a NON-OWNING pointer to the memory engine. The
      // pointer is stable for the runtime's lifetime; the store is opened in boot().
      cognitive_(cognitive::make_cognitive_core({}, memory_.get())),
      voice_(voice::make_voice_ui()),
      companion_(companion::make_companion_sync()),
      power_(power::make_power_manager()) {}

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

    // Open the on-device memory store. Best-effort: a store that fails to open
    // leaves the device running without recall (better than refusing to boot), and
    // the cognitive core simply skips its memory branches (it checks is_open()).
    if (memory_->open(config::memory_db()) != Status::Ok)
        log_warn("boot", "memory store unavailable; running without recall");

    // Initialize in dependency order. Power first so throttling is available
    // during the (heavier) model warm-ups.
    if (power_->initialize()      != Status::Ok) return Status::HardwareError;
    if (perception_->initialize() != Status::Ok) return Status::HardwareError;
    if (cognitive_->initialize()  != Status::Ok) return Status::HardwareError;
    if (voice_->initialize()      != Status::Ok) return Status::HardwareError;

    // Companion link is best-effort: the device must work offline, so a failed
    // connect does not fail boot.
    if (companion_->connect(companion::Transport::Ble) != Status::Ok) {
        log_warn("boot", "companion link unavailable; continuing offline");
    }

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

void Runtime::handle_response(const cognitive::Response& response) {
    // Speak it.
    voice_->speak(voice::to_utterance(response));

    // If the cognitive core wasn't sure, flag the caregiver (principle #5).
    if (response.flag_caregiver) {
        companion::Alert alert{
            companion::AlertKind::SafeModeEngaged,
            now(),
            "System deferred to safe mode."};
        companion_->send_alert(alert);
        set_state(RuntimeState::SafeMode);
    }
}

void Runtime::deliver_due_reminders() {
    if (!memory_ || !memory_->is_open()) return;
    const memory::UnixTime now = memory::unix_now();
    for (const auto& r : memory_->pending_deliveries(now)) {
        // Delivered through the SAME voice path as any other response — reminders
        // aren't a special output channel, just another thing ECHO says.
        voice_->speak(voice::Utterance{"Reminder: " + r.text + ".", voice::Tone::Alert});
        memory_->mark_fired(r.id, now);
    }
}

RuntimeState Runtime::tick() {
    // Let power-mgmt veto toward Throttled based on current constraints.
    power_->evaluate(power::PowerState{/*battery*/90, /*temp*/32.0f, /*charging*/false});

    // Reminders are checked on this same core tick (constraint #3: reuse the loop,
    // don't add a competing timer). This runs every tick, before the frame drain,
    // so a due reminder fires even when no sensor frame arrived this iteration.
    deliver_due_reminders();

    auto frame = sensors_.next_frame();
    if (!frame) {
        // Nothing to do this iteration; stay parked in the current low-power state.
        return state_;
    }

    set_state(RuntimeState::Active);

    // sensors --> perception
    auto perceived = perception_->process(*frame);
    if (!perceived) {
        log_warn("runtime", "perception failed; skipping frame");
        return state_;
    }

    // perception --> cognitive core (this applies the safe-mode gate)
    auto response = cognitive_->respond(perceived.value());
    if (!response) {
        log_warn("runtime", "cognitive core failed; skipping frame");
        return state_;
    }

    // cognitive core --> voice UI (+ companion alert if unsure)
    handle_response(response.value());

    if (state_ != RuntimeState::SafeMode) set_state(RuntimeState::Ready);
    return state_;
}

void Runtime::run() {
    while (!stop_requested_) {
        tick();
        // On device the loop is event-driven off the capture ISR. For the
        // scaffold, a single tick is enough to demonstrate the wiring; break so
        // the reference binary exits cleanly instead of spinning.
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
    if (perception_)perception_->shutdown();
    if (power_)     power_->shutdown();
    if (memory_)    memory_->close();  // flush + release the store after the core stops using it
    log_info("runtime", "shutdown complete");
}

}  // namespace echo::boot
