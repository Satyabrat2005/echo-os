#include "echo/boot/engine_guard.hpp"
#include "echo/log.hpp"

#include <exception>
#include <system_error>

namespace echo::boot {

EngineGuard::EngineGuard(std::string name, std::chrono::milliseconds budget)
    : name_(std::move(name)), budget_(budget) {}

EngineGuard::~EngineGuard() {
    // Reaping first is cheap; whatever is still running will be joined by the
    // future destructors below. If an engine is genuinely wedged this blocks
    // process exit — acceptable, because a wedged engine means we are rebooting
    // anyway (see the watchdog's honest recovery-scope note in the runtime).
    reap_graveyard();
}

void EngineGuard::reset_health() noexcept {
    health_.consecutive_failures = 0;
    health_.hung                 = false;
    // total_failures and last_status are intentionally preserved: recovery clears
    // the "act now" signals but not the cumulative fault history.
}

void EngineGuard::record_success() noexcept {
    health_.consecutive_failures = 0;
    health_.hung                 = false;
    health_.last_status          = Status::Ok;
}

void EngineGuard::record_failure(Status s) noexcept {
    ++health_.consecutive_failures;
    ++health_.total_failures;
    health_.hung        = (s == Status::Timeout);
    health_.last_status = s;
}

void EngineGuard::reap_graveyard() noexcept {
    for (auto it = graveyard_.begin(); it != graveyard_.end();) {
        if (it->valid() &&
            it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                it->get();  // observe (and swallow) any exception the abandoned task threw
            } catch (const std::exception& e) {
                log_debug("watchdog", e.what());
            } catch (...) {
                log_debug("watchdog", "abandoned engine worker threw a non-standard exception");
            }
            it = graveyard_.erase(it);
        } else {
            ++it;
        }
    }
}

Status EngineGuard::run_guarded_raw(const std::function<void()>& task) {
    reap_graveyard();

    std::future<void> fut;
    bool launched = false;
    try {
        // Copy (not move) task: if the launch throws we still need it for the
        // synchronous fallback below.
        fut     = std::async(std::launch::async, task);
        launched = true;
    } catch (const std::system_error&) {
        launched = false;  // could not spawn a worker (thread/resource exhaustion)
    }

    if (!launched) {
        // Degraded watchdog: with no worker thread available we cannot bound a hang,
        // but we still contain exceptions so a throw can't escape the loop. This is a
        // real, documented limitation of the fallback path, not the normal one.
        log_warn("watchdog", "no worker thread; running engine call inline (no hang bound)");
        try {
            task();
        } catch (...) {
            return Status::HardwareError;
        }
        return Status::Ok;
    }

    if (fut.wait_for(budget_) == std::future_status::timeout) {
        // Hung: the call outran its budget and has NOT returned. We cannot kill it,
        // so we abandon the worker (park the future so its blocking destructor does
        // not stall the loop) and report Timeout. The pipeline keeps running.
        graveyard_.push_back(std::move(fut));
        return Status::Timeout;
    }

    try {
        fut.get();  // rethrows anything the task threw
    } catch (const std::exception& e) {
        log_warn("watchdog", e.what());
        return Status::HardwareError;
    } catch (...) {
        log_warn("watchdog", "engine call threw a non-standard exception");
        return Status::HardwareError;
    }
    return Status::Ok;
}

}  // namespace echo::boot
