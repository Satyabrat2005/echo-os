// ECHO OS — EngineGuard: containment around a single engine call site (Phase 17).
//
// The wearer is, by design, someone who may not notice or be able to correct a
// misbehaving device on their own face. So no single engine — wake-word, ASR,
// vision, LLM, TTS, memory — is allowed to take the whole runtime down with it.
// EngineGuard is the containment primitive that makes that true at every call
// site. It does three things and nothing else:
//
//   1. contains exceptions — a throw out of an engine becomes fail(HardwareError),
//      never an unwind through the core loop (see result.hpp: the loop must not
//      throw across a stage boundary);
//   2. bounds hung calls    — the call runs on a worker and is ABANDONED if it
//      outruns `budget`, returning fail(Timeout) so the loop keeps ticking even
//      when an engine wedges and never returns (a different failure from "threw");
//   3. records health        — consecutive/total failures and whether the last
//      failure was a hang — so the watchdog and the runtime's degraded-mode policy
//      can react.
//
// It deliberately does NOT decide what to speak on failure. That is the runtime's
// degraded-mode policy (a vision failure and an ASR failure want different
// fallbacks). The guard only answers "did this call fail, and how?".
//
// Honest cost, stated once here because it is the crux of the watchdog design:
// C++ cannot safely kill a wedged thread, and a std::async future's destructor
// BLOCKS until its task finishes. So a call that truly never returns leaks one
// worker thread (parked in graveyard_) until the process restarts. This is why
// the watchdog's recovery scope (documented in the runtime) is honest about what
// it can and cannot bring back without a physical reboot.
#pragma once

#include "echo/result.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace echo::boot {

// Health of one supervised engine boundary. Read by the watchdog (to decide when
// to attempt recovery or escalate to a reboot) and by the runtime (to drive
// degraded-mode behaviour).
struct EngineHealth {
    int    consecutive_failures = 0;         // reset to 0 on any successful call
    int    total_failures       = 0;         // cumulative, for status/telemetry
    bool   hung                 = false;      // last failure was a timeout (non-returning call)
    Status last_status          = Status::Ok; // status of the most recent call
};

class EngineGuard {
public:
    EngineGuard(std::string name, std::chrono::milliseconds budget);
    ~EngineGuard();

    EngineGuard(const EngineGuard&)            = delete;
    EngineGuard& operator=(const EngineGuard&) = delete;
    EngineGuard(EngineGuard&&)                 = delete;
    EngineGuard& operator=(EngineGuard&&)      = delete;

    // Guard a Result<T>-returning engine call (perception.process, cognitive.respond).
    // Returns the engine's own Result on normal completion; fail(HardwareError) on a
    // throw; fail(Timeout) on a hang. A Result that comes back non-Ok is counted as a
    // (non-hung) failure too — an engine reporting its own fault still degrades the
    // turn.
    template <typename Fn>
    auto call(Fn&& fn) -> decltype(fn());

    // Guard a Status-returning engine call (voice.speak, memory.mark_fired). Propagates
    // the engine's own Status on completion; Timeout/HardwareError on hang/throw.
    template <typename Fn>
    Status call_status(Fn&& fn);

    // Guard a void engine call (memory.enforce_retention). Ok on completion.
    template <typename Fn>
    Status call_void(Fn&& fn);

    [[nodiscard]] const EngineHealth& health() const noexcept { return health_; }
    [[nodiscard]] const std::string&  name()   const noexcept { return name_; }
    [[nodiscard]] std::chrono::milliseconds budget() const noexcept { return budget_; }

    // Number of abandoned (still-running) hung workers currently parked. Exposed so
    // tests and telemetry can see the honest leak the watchdog cannot avoid.
    [[nodiscard]] std::size_t leaked_workers() const noexcept { return graveyard_.size(); }

    // Watchdog: clear the failure/hung bookkeeping after a recovery attempt so the
    // engine gets a fresh chance on the next turn.
    void reset_health() noexcept;

private:
    // Transport-level outcome of running `task`: Ok = completed normally (the task's
    // own payload may still be a non-Ok Result/Status), Timeout = hung and abandoned,
    // HardwareError = threw. Health accounting is done by the templated callers, which
    // also fold in the engine's returned status.
    Status run_guarded_raw(const std::function<void()>& task);

    // Non-blocking sweep of finished abandoned workers (their threads have returned),
    // so a burst of hangs doesn't pile up unbounded while the process keeps running.
    void reap_graveyard() noexcept;

    void record_success() noexcept;
    void record_failure(Status s) noexcept;

    std::string               name_;
    std::chrono::milliseconds budget_;
    EngineHealth              health_;
    // Futures of calls that timed out and are still running. We can't kill a wedged
    // thread, and a std::async future's destructor joins — so abandoned futures live
    // here and are reaped when they finish (or joined at shutdown). See the header
    // comment: this is the deliberate, documented cost of hang detection.
    std::vector<std::future<void>> graveyard_;
};

// --- templated call wrappers -------------------------------------------------

template <typename Fn>
auto EngineGuard::call(Fn&& fn) -> decltype(fn()) {
    using R   = decltype(fn());   // Result<T>
    auto slot = std::make_shared<std::optional<R>>();
    const Status transport = run_guarded_raw(
        [slot, f = std::forward<Fn>(fn)]() mutable { *slot = f(); });

    if (transport != Status::Ok) {          // threw or hung: engine never produced a value
        record_failure(transport);
        return R::fail(transport);
    }
    if (!slot->has_value()) {                // shouldn't happen, but never read an empty slot
        record_failure(Status::HardwareError);
        return R::fail(Status::HardwareError);
    }
    // Access is guarded by the has_value() check directly above; clang-tidy can't prove
    // engagement through a shared_ptr<optional>, so this is the same reviewed, intentional
    // NOLINT the project uses in result.hpp for exactly this pattern.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    R result = std::move(**slot);
    if (result.status() == Status::Ok) record_success();
    else                               record_failure(result.status());
    return result;
}

template <typename Fn>
Status EngineGuard::call_status(Fn&& fn) {
    auto slot = std::make_shared<std::optional<Status>>();
    const Status transport = run_guarded_raw(
        [slot, f = std::forward<Fn>(fn)]() mutable { *slot = f(); });

    if (transport != Status::Ok) { record_failure(transport); return transport; }
    if (!slot->has_value()) { record_failure(Status::HardwareError); return Status::HardwareError; }
    // Guarded by has_value() above; see the note in call() on the shared_ptr<optional> NOLINT.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const Status engine_status = **slot;
    if (engine_status == Status::Ok) record_success();
    else                             record_failure(engine_status);
    return engine_status;
}

template <typename Fn>
Status EngineGuard::call_void(Fn&& fn) {
    const Status transport = run_guarded_raw(
        [f = std::forward<Fn>(fn)]() mutable { f(); });
    if (transport != Status::Ok) record_failure(transport);
    else                         record_success();
    return transport;
}

}  // namespace echo::boot
