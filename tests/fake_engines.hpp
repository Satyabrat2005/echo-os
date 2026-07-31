// ECHO OS tests — shared, deterministic FAKE engines for runtime-level tests.
//
// Extracted from the Phase 17 fault-injection suite so the Phase 18 power/thermal suite
// can drive the SAME real runtime through the SAME dependency-injection seam without a
// second copy of the fakes. Every fake here is deterministic and scriptable: an engine can
// be made to misbehave (throw / return a non-Ok status / hang past its watchdog budget), and
// what it did (spoken lines, caregiver alerts) can be read back.
//
// These are the six engines the runtime drives. Power/thermal SOURCES are faked separately
// with the reusable fakes shipped in the module itself (echo/power/fake_power_source.hpp).
#pragma once

#include "echo/memory/memory_engine.hpp"
#include "echo/perception/perception_engine.hpp"
#include "echo/cognitive/cognitive_core.hpp"
#include "echo/voice/voice_ui.hpp"
#include "echo/companion/companion_sync.hpp"
#include "echo/power/power_manager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace echo::test {

// How a fake engine call should misbehave.
enum class Fault { None, Throw, Error, Hang };

// A hung call sleeps well past the test watchdog budget (150 ms), then returns — so the
// abandoned worker eventually finishes and the test never blocks at shutdown, while still
// exercising the real timeout/abandon path. The 4x margin (600 vs 150) keeps a genuinely
// healthy call — which returns in microseconds — from ever being mistaken for a hang on a
// slow/loaded CI runner.
inline constexpr std::chrono::milliseconds kHang{600};

inline void maybe_hang(Fault f) {
    if (f == Fault::Hang) std::this_thread::sleep_for(kHang);
}

// --- fake perception ---------------------------------------------------------
class FakePerception final : public perception::IPerceptionEngine {
public:
    std::atomic<Fault> fault{Fault::None};
    std::atomic<int>   process_calls{0};
    // How many more Fault::Hang calls should actually hang before the engine behaves
    // healthily again. -1 (default) = hang persistently, preserving the Phase 17
    // escalation-to-reboot behaviour. A positive value models a TRANSIENT hang the
    // watchdog can genuinely recover in-process: the guarded call is abandoned once,
    // then the very next call (the watchdog's re-init) succeeds — which is exactly the
    // repeatable recovery the Phase 20 soak exercises thousands of times. Decremented
    // atomically so the process-call worker and the re-init worker can't both hang.
    std::atomic<int> hang_budget{-1};

    Status initialize() override {
        if (take_hang()) std::this_thread::sleep_for(kHang);
        if (fault.load() == Fault::Throw) throw std::runtime_error("perception init boom");
        if (fault.load() == Fault::Error) return Status::NotReady;
        return Status::Ok;
    }
    Result<perception::Perception> process(const SensorFrame&) override {
        ++process_calls;
        const Fault f = fault.load();
        if (f == Fault::Throw) throw std::runtime_error("perception boom");
        if (take_hang()) std::this_thread::sleep_for(kHang);
        if (f == Fault::Error) return Result<perception::Perception>::fail(Status::HardwareError);
        return Result<perception::Perception>::ok(perception::Perception{});
    }
    void shutdown() override {}

private:
    // True iff this Fault::Hang call should sleep. Consumes one unit of a finite
    // hang_budget; an unset budget (-1) hangs every time (persistent).
    bool take_hang() noexcept {
        if (fault.load() != Fault::Hang) return false;
        int b = hang_budget.load();
        if (b < 0) return true;  // persistent hang
        while (b > 0 && !hang_budget.compare_exchange_weak(b, b - 1)) { /* retry */ }
        return b > 0;
    }
};

// --- fake cognitive core -----------------------------------------------------
class FakeCognitive final : public cognitive::ICognitiveCore {
public:
    std::atomic<Fault> fault{Fault::None};
    std::atomic<bool>  simulate_gate{false};  // emit an Ok safe-mode result (the confidence gate)
    std::atomic<int>   respond_calls{0};

    // The line the (simulated) confidence gate speaks — an Ok response, flag_caregiver set.
    // Deliberately different text from the runtime's engine-fault line so the two paths are
    // visibly distinct (Phase 17 constraint #1).
    static constexpr const char* kGateLine = "I'm not quite sure right now. Let's take a moment.";

    Status initialize() override {
        maybe_hang(fault.load());
        if (fault.load() == Fault::Throw) throw std::runtime_error("cognitive init boom");
        return Status::Ok;
    }
    Result<cognitive::Response> respond(const perception::Perception&) override {
        ++respond_calls;
        const Fault f = fault.load();
        if (f == Fault::Throw) throw std::runtime_error("cognitive boom");
        maybe_hang(f);
        if (f == Fault::Error) return Result<cognitive::Response>::fail(Status::HardwareError);

        cognitive::Response r;
        if (simulate_gate.load()) {
            r.kind = cognitive::ResponseKind::SafeMode;
            r.text = kGateLine;
            r.flag_caregiver = true;
        } else {
            r.kind = cognitive::ResponseKind::Normal;
            r.text = "ACK";
            r.flag_caregiver = false;
        }
        return Result<cognitive::Response>::ok(std::move(r));
    }
    void shutdown() override {}
};

// --- fake voice UI -----------------------------------------------------------
class FakeVoice final : public voice::IVoiceUi {
public:
    std::atomic<Fault> fault{Fault::None};
    // Cumulative count of spoken utterances, independent of forget() (see below). Lets a
    // long soak track total output without unbounded recording.
    std::atomic<std::uint64_t> total_spoken{0};

    Status initialize() override {
        maybe_hang(fault.load());
        if (fault.load() == Fault::Throw) throw std::runtime_error("voice init boom");
        return Status::Ok;
    }
    Status speak(const voice::Utterance& u) override {
        const Fault f = fault.load();
        if (f == Fault::Throw) throw std::runtime_error("tts boom");
        maybe_hang(f);
        if (f == Fault::Error) return Status::HardwareError;
        ++total_spoken;
        {
            const std::lock_guard<std::mutex> lock(mu_);
            spoken_.push_back(u);
        }
        return Status::Ok;
    }
    Status play_earcon(std::string_view name) override {
        const std::lock_guard<std::mutex> lock(mu_);
        earcons_.emplace_back(name);
        return Status::Ok;
    }
    void barge_in() override {}
    void shutdown() override {}

    std::vector<voice::Utterance> spoken() const {
        const std::lock_guard<std::mutex> lock(mu_);
        return spoken_;
    }
    std::vector<std::string> earcons() const {
        const std::lock_guard<std::mutex> lock(mu_);
        return earcons_;
    }

    // Drop the recorded history (NOT the cumulative counters). A long soak calls this
    // periodically so the harness's own recording can't be mistaken for a runtime leak
    // when sampling RSS — the point is to measure the runtime, not this vector.
    void forget() {
        const std::lock_guard<std::mutex> lock(mu_);
        spoken_.clear();
        earcons_.clear();
    }

private:
    mutable std::mutex            mu_;
    std::vector<voice::Utterance> spoken_;
    std::vector<std::string>      earcons_;
};

// --- fake companion sync -----------------------------------------------------
class FakeCompanion final : public companion::ICompanionSync {
public:
    // Cumulative alert count, independent of forget() — a long soak tracks total alerts
    // without unbounded recording.
    std::atomic<std::uint64_t> total_alerts{0};

    Status connect(companion::Transport) override { connected_ = true; return Status::Ok; }
    Status send_alert(const companion::Alert& a) override {
        ++total_alerts;
        const std::lock_guard<std::mutex> lock(mu_);
        alerts_.push_back(a);
        return Status::Ok;
    }
    Status send_status(const companion::StatusReport&) override { return Status::Ok; }
    Status poll_firmware_update() override { return Status::Unavailable; }
    bool   connected() const noexcept override { return connected_; }
    void   disconnect() override { connected_ = false; }

    std::vector<companion::Alert> alerts() const {
        const std::lock_guard<std::mutex> lock(mu_);
        return alerts_;
    }
    int count_of(companion::AlertKind k) const {
        const std::lock_guard<std::mutex> lock(mu_);
        int n = 0;
        for (const auto& a : alerts_) if (a.kind == k) ++n;
        return n;
    }
    // Alerts of a given kind whose note contains `needle` — for asserting a specific
    // power/thermal condition rode the shared EngineDegraded channel.
    int count_note_contains(companion::AlertKind k, const std::string& needle) const {
        const std::lock_guard<std::mutex> lock(mu_);
        int n = 0;
        for (const auto& a : alerts_)
            if (a.kind == k && a.note.find(needle) != std::string::npos) ++n;
        return n;
    }

    // Drop recorded alert history (NOT the cumulative counter) — see FakeVoice::forget().
    void forget() {
        const std::lock_guard<std::mutex> lock(mu_);
        alerts_.clear();
    }

private:
    mutable std::mutex            mu_;
    std::vector<companion::Alert> alerts_;
    bool                          connected_ = false;
};

// --- fake power manager (the DVFS profile actor; not the source) -------------
class FakePower final : public power::IPowerManager {
public:
    Status  initialize() override { return Status::Ok; }
    power::Profile evaluate(const power::PowerState&) override { return power::Profile::Interactive; }
    void    report_latency(Stage, double) override {}
    power::Profile current_profile() const noexcept override { return power::Profile::Interactive; }
    void    shutdown() override {}
};

// --- fake memory engine ------------------------------------------------------
class FakeMemory final : public memory::IMemoryEngine {
public:
    std::atomic<Fault> pending_fault{Fault::None};    // fault on the reminder READ
    std::atomic<Fault> write_fault{Fault::None};      // fault on mark_fired (the WRITE)
    std::atomic<Fault> retention_fault{Fault::None};  // fault on enforce_retention
    std::atomic<bool>  has_due{false};                // a reminder is due to deliver

    Status open(const std::string&) override { open_ = true; return Status::Ok; }
    bool   is_open() const noexcept override { return open_; }
    void   close() override { open_ = false; }

    void set_retention(const memory::RetentionPolicy&) override {}
    void enforce_retention(memory::UnixTime) override {
        const Fault f = retention_fault.load();
        if (f == Fault::Throw) throw std::runtime_error("retention boom");
        maybe_hang(f);
    }

    memory::PersonMatch recognize(const memory::Embedding&) override { return {}; }
    Result<memory::PersonId> remember_person(const std::string&, const std::string&,
                                             const memory::Embedding&, memory::UnixTime) override {
        return Result<memory::PersonId>::ok(1);
    }
    Status mark_seen(memory::PersonId, memory::UnixTime) override { return Status::Ok; }
    Status add_note(memory::PersonId, const std::string&) override { return Status::Ok; }
    std::optional<memory::PersonRecord> get_person(memory::PersonId) override { return std::nullopt; }
    std::vector<memory::PersonRecord>   all_people() override { return {}; }

    Result<memory::ReminderId> add_reminder(const std::string&, memory::UnixTime,
                                            memory::Recurrence) override {
        return Result<memory::ReminderId>::ok(1);
    }
    std::vector<memory::ReminderRecord> due_reminders(memory::UnixTime) override { return {}; }
    std::vector<memory::ReminderRecord> pending_deliveries(memory::UnixTime) override {
        const Fault f = pending_fault.load();
        if (f == Fault::Throw) throw std::runtime_error("pending read boom");
        maybe_hang(f);
        if (!has_due.load()) return {};
        memory::ReminderRecord r;
        r.id = 1;
        r.text = "take medication";
        r.recurrence = memory::Recurrence::Once;
        return {r};  // fake never removes it: models "still pending because the write failed"
    }
    Status mark_fired(memory::ReminderId, memory::UnixTime) override {
        const Fault f = write_fault.load();
        if (f == Fault::Throw) throw std::runtime_error("mark_fired boom");
        maybe_hang(f);
        if (f == Fault::Error) return Status::HardwareError;
        return Status::Ok;
    }
    Status acknowledge_reminder(memory::ReminderId, memory::UnixTime) override { return Status::Ok; }
    std::vector<memory::ReminderRecord> all_reminders() override { return {}; }
    std::string answer_query(const std::string&, memory::UnixTime) override { return ""; }
    Status log_event(const memory::EventRecord&) override { return Status::Ok; }
    std::vector<memory::EventRecord> recent_events(int) override { return {}; }

private:
    bool open_ = false;
};

}  // namespace echo::test
