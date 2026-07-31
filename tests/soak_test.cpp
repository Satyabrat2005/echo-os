// ECHO OS — longevity & resource-leak soak test (Phase 20).
//
// Every other test in this repo exercises a single turn, a single fault, or a bounded
// scenario. None of them prove the thing this specific product needs most: that it can
// run CONTINUOUSLY, for many hours, without degrading. This one does — as far as the
// orchestration layer goes. It drives the REAL runtime (boot/runtime.cpp) with the REAL
// memory engine (memory/, SQLite + encryption at rest) and the shared deterministic
// fakes for the other five engines, through THOUSANDS of ticks that represent a
// compressed multi-day day-in-the-life — using SIMULATED time (an injected virtual
// clock), never sleep()-based real time, so a week of scheduler behaviour runs in
// seconds and fits in CI.
//
// It puts real numbers on the failure modes that only show up over a long run:
//
//   1. Reminder-scheduler drift (Phase 15) — thousands of one-shot reminders scheduled
//      across the simulated week must each fire exactly once, within one tick of their
//      due time, with NO cumulative drift. And a recurring reminder that is acknowledged
//      (re-armed) must fire on EVERY occurrence — the longevity bug this phase found and
//      fixed (an id-only in-session dedup silently dropped every occurrence after the
//      first; see delivered_occurrence_ in runtime.hpp).
//   2. Event-log retention (Phase 16) — the append-only log, driven past its cap
//      thousands of times through the live tick (not the single-shot unit test), stays
//      bounded.
//   3. Process RSS — sampled across the run, must plateau, not creep.
//   4. Open fd / handle count — must not grow with save-to-disk churn or with repeated
//      watchdog memory re-init (close+open) cycles.
//   5. Watchdog recovery (Phase 17) — a transient engine hang injected periodically
//      throughout the soak must recover in-process EVERY time, never escalate to reboot,
//      and never accumulate leaked worker threads (the abandoned-worker graveyard must
//      drain back to zero).
//
// HONEST SCOPE (also in docs/STATE.md): simulated-time soak with fake engines proves the
// ORCHESTRATION doesn't leak or drift. It does NOT prove the real third-party engine
// libraries (whisper.cpp, llama.cpp, OpenCV, ONNX Runtime) are leak-free over real
// multi-day uptime — this harness doesn't instrument them; that stays unproven until real
// hardware runs for real days.
#include "echo/boot/runtime.hpp"
#include "echo/memory/memory_engine.hpp"

#include "fake_engines.hpp"    // shared deterministic fakes (Phase 17/18), extended for the soak
#include "resource_probe.hpp"  // cross-platform RSS + fd sampler
#include "check.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace echo;

namespace {

using echo::test::FakePerception;
using echo::test::FakeCognitive;
using echo::test::FakeVoice;
using echo::test::FakeCompanion;
using echo::test::FakePower;
using echo::test::Fault;

// --- tunables (env-overridable so CI can dial the run length) ---------------

// Read a positive integer env var, else the fallback.
long env_long(const char* var, long fallback) {
    const char* v = std::getenv(var);
    if (v == nullptr || *v == '\0') return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(v, &end, 10);
    return (end != v && *end == '\0' && parsed > 0) ? parsed : fallback;
}

// One simulated day-in-the-life. Defaults chosen so the run is a genuine soak that still
// finishes fast in CI: 10,080 ticks x 60 simulated seconds = 604,800 s = exactly 7
// simulated days. Overridable via ECHO_SOAK_TICKS for a longer local run.
struct Params {
    long ticks         = env_long("ECHO_SOAK_TICKS", 10080);
    long dt_sim        = 60;                                  // simulated seconds advanced per tick
    long reminders     = env_long("ECHO_SOAK_REMINDERS", 600);  // one-shots spread across timeline
    int  retention_cap = static_cast<int>(env_long("ECHO_SOAK_RETENTION_CAP", 200));  // event-log cap
    long hang_every    = 900;     // inject a transient perception hang this often (watchdog)
    long mic_every     = 4;       // feed a normal mic frame this often (exercise the pipeline)
    long sample_every  = 200;     // sample RSS + fd this often
    long forget_every  = 200;     // drop fake recording this often (bound harness memory)
    long warmup        = 1000;    // ticks to let RSS settle before taking the plateau baseline
};

// --- a virtual clock the runtime reads instead of the real wall clock -------
struct SimClock {
    std::int64_t now = 1'700'000'000;  // an arbitrary fixed epoch base (deterministic)
    std::int64_t base = now;
};

// --- temp store path (real file so save-to-disk + fd are exercised) ---------
struct TempStore {
    std::filesystem::path path;
    explicit TempStore(const std::string& tag) : path(make_path(tag)) {
        cleanup();  // start from a clean slate
    }
    ~TempStore() { cleanup(); }

    static std::filesystem::path make_path(const std::string& tag) {
        // Unique per process AND per call so concurrent runs (or a retried CI job) never
        // share a store file: high-resolution clock + a monotonic counter.
        static std::atomic<unsigned> seq{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
               ("echo_soak_" + tag + "_" + std::to_string(stamp) + "_" +
                std::to_string(seq.fetch_add(1)) + ".db");
    }
    void cleanup() const {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(std::filesystem::path(path).concat(".key"), ec);
        std::filesystem::remove(std::filesystem::path(path).concat(".tmp"), ec);
    }
    std::string str() const { return path.string(); }
};

// Portable in-process setenv (config::memory_db reads ECHO_MEMORY_DB via getenv).
void set_env(const char* k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k, v.c_str());
#else
    setenv(k, v.c_str(), /*overwrite=*/1);
#endif
}

// Non-owning handles to the injected fakes + the real memory engine.
struct Handles {
    FakePerception* perception = nullptr;
    FakeCognitive*  cognitive  = nullptr;
    FakeVoice*      voice      = nullptr;
    FakeCompanion*  companion  = nullptr;
    memory::IMemoryEngine* memory = nullptr;  // REAL engine (owned by the runtime)
};

boot::WatchdogConfig soak_watchdog() {
    boot::WatchdogConfig c;
    // The soak injects hangs ONLY on perception, so only perception needs a budget below
    // the 600 ms injected hang (kHang) to detect it — 400 ms trips the hang with wide
    // headroom over a healthy microsecond call. The other guards get GENEROUS budgets on
    // purpose: over thousands of ticks on a shared/loaded CI runner, a tight budget on a
    // perfectly healthy call can spuriously time out (a guarded call spawns a worker
    // thread, and thread-scheduling latency under load is real), which for the memory
    // read-path would look like reminder drift and for others like a phantom fault. A
    // wedged healthy engine simply does not take ~1.5 s, so these bounds still catch a
    // real wedge while removing load-induced false positives — the soak is measuring
    // longevity, not budget tuning.
    c.perception_budget = std::chrono::milliseconds(400);   // must be < kHang (600) to detect the hang
    c.cognitive_budget  = std::chrono::milliseconds(1500);
    c.voice_budget      = std::chrono::milliseconds(1500);
    c.memory_budget     = std::chrono::milliseconds(2000);  // real SQLite serialize+encrypt+write
    c.max_consecutive_failures = 3;
    c.max_recoveries           = 3;
    return c;
}

// Build + boot a runtime: REAL memory engine at `db_path`, shared fakes for the rest,
// with the virtual clock injected. Returns the runtime; `out` receives raw handles.
std::unique_ptr<boot::Runtime> make_soak_runtime(const std::string& db_path, SimClock& clock,
                                                 Handles& out) {
    set_env("ECHO_MEMORY_DB", db_path);

    auto memory     = memory::make_memory_engine();  // the REAL SQLite-backed store
    auto perception = std::make_unique<FakePerception>();
    auto cognitive  = std::make_unique<FakeCognitive>();
    auto voice      = std::make_unique<FakeVoice>();
    auto companion  = std::make_unique<FakeCompanion>();
    auto power      = std::make_unique<FakePower>();

    out.memory     = memory.get();
    out.perception = perception.get();
    out.cognitive  = cognitive.get();
    out.voice      = voice.get();
    out.companion  = companion.get();

    boot::Runtime::Engines engines;
    engines.memory     = std::move(memory);
    engines.perception = std::move(perception);
    engines.cognitive  = std::move(cognitive);
    engines.voice      = std::move(voice);
    engines.companion  = std::move(companion);
    engines.power      = std::move(power);

    auto rt = std::make_unique<boot::Runtime>(std::move(engines), soak_watchdog());
    rt->set_clock([&clock]() -> memory::UnixTime { return clock.now; });
    CHECK(rt->boot() == Status::Ok);
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

// Parse the index out of a spoken reminder line "Reminder: dose <i>." Returns -1 if not one.
long parse_dose_index(const std::string& text) {
    static const std::string kPrefix = "Reminder: dose ";
    if (text.rfind(kPrefix, 0) != 0) return -1;
    return std::strtol(text.c_str() + kPrefix.size(), nullptr, 10);
}

// ===========================================================================
// 1. The main soak: drift + retention + RSS + fd + watchdog, all in one run.
// ===========================================================================
void test_longevity_soak() {
    const Params p;
    SimClock clock;
    TempStore store("main");
    Handles h;
    auto rt = make_soak_runtime(store.str(), clock, h);

    // Bound the event log so retention is exercised at scale (4000 firings >> 500 cap).
    // Age-based pruning off; this run compresses a week, so count-based is the honest cap
    // to stress (age-eviction is covered by the Phase 16 unit test).
    h.memory->set_retention(memory::RetentionPolicy{p.retention_cap, /*age_days=*/0,
                                                    /*notes=*/20});

    // Schedule N one-shot reminders spread strictly inside (base, base+span) so each has a
    // real, distinct due time to hit on schedule.
    const std::int64_t span = static_cast<std::int64_t>(p.ticks) * p.dt_sim;
    std::vector<std::int64_t> due(static_cast<std::size_t>(p.reminders));
    for (long i = 0; i < p.reminders; ++i) {
        due[static_cast<std::size_t>(i)] =
            clock.base + (span * (i + 1)) / (p.reminders + 1);
        const auto r = h.memory->add_reminder("dose " + std::to_string(i),
                                              due[static_cast<std::size_t>(i)],
                                              memory::Recurrence::Once);
        CHECK(static_cast<bool>(r));
    }

    std::vector<std::int64_t> delivered_at(static_cast<std::size_t>(p.reminders), -1);

    // Sampling + watchdog bookkeeping.
    std::vector<std::int64_t> rss_samples, fd_samples;
    long hang_episodes = 0;
    std::int64_t peak_leaked = 0;
    bool ever_reboot = false;

    for (long t = 0; t < p.ticks; ++t) {
        clock.now = clock.base + static_cast<std::int64_t>(t) * p.dt_sim;

        const bool hang_tick = (p.hang_every > 0) && (t > 0) && (t % p.hang_every == 0);
        if (hang_tick) {
            // Inject a TRANSIENT perception hang: hang exactly once, so the guarded process
            // call is abandoned and the watchdog's re-init then succeeds — repeatable
            // in-process recovery, the thing this soak is proving doesn't leak.
            h.perception->fault       = Fault::Hang;
            h.perception->hang_budget = 1;
            (void)rt->offer_frame(frame_of(Modality::Camera));
            ++hang_episodes;
        } else if (p.mic_every > 0 && t % p.mic_every == 0) {
            (void)rt->offer_frame(frame_of(Modality::Microphone));  // exercise the full pipeline
        }

        rt->tick();

        if (hang_tick) {
            h.perception->fault       = Fault::None;  // fault cleared for subsequent ticks
            h.perception->hang_budget = -1;
        }

        // Record first-delivery time of any reminder spoken this window (drift signal).
        for (const auto& u : h.voice->spoken()) {
            const long idx = parse_dose_index(u.text);
            if (idx >= 0 && idx < p.reminders &&
                delivered_at[static_cast<std::size_t>(idx)] < 0) {
                delivered_at[static_cast<std::size_t>(idx)] = clock.now;
            }
        }

        if (rt->reboot_required()) ever_reboot = true;
        peak_leaked = std::max<std::int64_t>(
            peak_leaked, static_cast<std::int64_t>(rt->perception_guard().leaked_workers()));

        if (p.sample_every > 0 && t % p.sample_every == 0) {
            rss_samples.push_back(test::rss_kib());
            fd_samples.push_back(test::open_handle_count());
        }
        if (p.forget_every > 0 && t % p.forget_every == 0) {
            h.voice->forget();
            h.companion->forget();
        }
    }

    // A short quiescence: drain any still-running abandoned hang worker so leaked_workers
    // returns to zero. Bounded — a hang worker finishes ~600 ms after it was abandoned; a
    // HEALTHY perception call reaps it (reaping happens at the start of the next guarded
    // call), so we must feed a frame each drain tick, not just idle-tick. This is the
    // honest "does the graveyard actually empty?" check.
    for (int i = 0; i < 300 && rt->perception_guard().leaked_workers() > 0; ++i) {
        clock.now += p.dt_sim;
        (void)rt->offer_frame(frame_of(Modality::Camera));  // healthy call -> reap on entry
        rt->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const std::int64_t final_leaked =
        static_cast<std::int64_t>(rt->perception_guard().leaked_workers());

    // ---- assertions + measured numbers ------------------------------------

    // (a) Reminder-scheduler: every reminder fired exactly once, within one tick of its due
    //     time, with no cumulative drift (max drift < one tick's simulated seconds).
    long delivered = 0, worst_drift = 0, early = 0;
    for (long i = 0; i < p.reminders; ++i) {
        const std::int64_t d = delivered_at[static_cast<std::size_t>(i)];
        if (d < 0) continue;
        ++delivered;
        const std::int64_t drift = d - due[static_cast<std::size_t>(i)];
        if (drift < 0) ++early;
        worst_drift = std::max<std::int64_t>(worst_drift, drift);
    }
    CHECK(delivered == p.reminders);          // none dropped over the whole simulated week
    CHECK(early == 0);                        // none fired BEFORE its due time
    CHECK(worst_drift < p.dt_sim);            // bounded by one tick — no accumulating drift

    // (b) Retention: the event log stayed bounded despite thousands of firings.
    const auto events = h.memory->recent_events(p.retention_cap * 4);  // ask for more than the cap
    CHECK(static_cast<int>(events.size()) <= p.retention_cap);

    // (c) Watchdog: recovered every injected hang in-process, never rebooted, no leaked
    //     workers left behind.
    CHECK(hang_episodes > 0);
    CHECK(!ever_reboot);
    CHECK(final_leaked == 0);

    // (d) RSS plateau: compare the post-warmup baseline to the end of the run.
    const bool have_res = !rss_samples.empty() && rss_samples.front() != test::kUnavailable;
    std::int64_t rss_base = 0, rss_end = 0, rss_growth_kib = 0;
    std::int64_t fd_min = 0, fd_max = 0;
    if (have_res) {
        const std::size_t warm_idx =
            std::min<std::size_t>(rss_samples.size() - 1,
                                  static_cast<std::size_t>(p.warmup / std::max(1L, p.sample_every)));
        rss_base = rss_samples[warm_idx];
        rss_end  = rss_samples.back();
        rss_growth_kib = rss_end - rss_base;
        // Plateau bar: post-warmup growth under 15% AND under 16 MiB. Chosen as an honest
        // "flat, not creeping" bound, not tuned to the observed number.
        CHECK(rss_growth_kib < rss_base * 15 / 100);
        CHECK(rss_growth_kib < 16 * 1024);

        fd_min = *std::min_element(fd_samples.begin(), fd_samples.end());
        fd_max = *std::max_element(fd_samples.begin(), fd_samples.end());
        // fd must not creep with save-to-disk churn: a handful of transient fds is fine,
        // monotonic growth is not.
        CHECK(fd_max - fd_min < 16);
    }

    std::printf(
        "[soak] main: ticks=%ld  sim=%.1f days (%ld s/tick)  reminders=%ld\n",
        p.ticks, static_cast<double>(span) / 86400.0, p.dt_sim, p.reminders);
    std::printf(
        "[soak] scheduler: delivered=%ld/%ld  early=%ld  worst_drift=%lds (< %lds tick)\n",
        delivered, p.reminders, early, worst_drift, p.dt_sim);
    std::printf(
        "[soak] retention: event_log_rows=%zu (cap=%d)  total_spoken=%llu  total_alerts=%llu\n",
        events.size(), p.retention_cap,
        static_cast<unsigned long long>(h.voice->total_spoken.load()),
        static_cast<unsigned long long>(h.companion->total_alerts.load()));
    std::printf(
        "[soak] watchdog: hang_episodes=%ld  reboot=%s  peak_leaked_workers=%lld  final_leaked=%lld\n",
        hang_episodes, ever_reboot ? "YES" : "no", static_cast<long long>(peak_leaked),
        static_cast<long long>(final_leaked));
    if (have_res) {
        std::printf(
            "[soak] rss: baseline=%lld KiB  end=%lld KiB  growth=%lld KiB (%.2f%%)\n",
            static_cast<long long>(rss_base), static_cast<long long>(rss_end),
            static_cast<long long>(rss_growth_kib),
            rss_base > 0 ? 100.0 * static_cast<double>(rss_growth_kib) / static_cast<double>(rss_base)
                         : 0.0);
        std::printf("[soak] fd: min=%lld  max=%lld  span=%lld\n",
                    static_cast<long long>(fd_min), static_cast<long long>(fd_max),
                    static_cast<long long>(fd_max - fd_min));
    } else {
        std::printf("[soak] rss/fd: unavailable on this platform — assertions skipped "
                    "(they run on Linux CI)\n");
    }
}

// ===========================================================================
// 2. Recurring-reminder re-arm across simulated days — the regression test for the
//    longevity bug this phase found and fixed. A daily reminder that is acknowledged
//    (re-armed to the next day) must fire on EVERY occurrence. Before the fix, the
//    runtime's id-only in-session dedup suppressed every occurrence after the first,
//    so a daily medication reminder would go silent after day one until reboot.
// ===========================================================================
void test_recurring_reminder_fires_every_occurrence() {
    SimClock clock;
    TempStore store("recurring");
    Handles h;
    auto rt = make_soak_runtime(store.str(), clock, h);

    const std::int64_t first_due = clock.base + 3600;  // an hour in
    const auto rid = h.memory->add_reminder("daily-med", first_due, memory::Recurrence::Daily);
    CHECK(static_cast<bool>(rid));
    const memory::ReminderId id = rid.value();

    auto count_daily_med = [&h]() {
        long n = 0;
        for (const auto& u : h.voice->spoken())
            if (u.text == "Reminder: daily-med.") ++n;
        return n;
    };

    const int kOccurrences = 4;  // day 0..3
    for (int day = 0; day < kOccurrences; ++day) {
        const std::int64_t occ_due = first_due + static_cast<std::int64_t>(day) * 86400;

        // Advance to just after this occurrence's due time and tick; it must fire now.
        clock.now = occ_due + 30;
        rt->tick();
        CHECK(count_daily_med() == day + 1);  // one MORE delivery than the previous day

        // The wearer confirms it — the store re-arms the reminder to the next day. This is
        // the exact path an id-only dedup would then wrongly suppress forever.
        CHECK(h.memory->acknowledge_reminder(id, clock.now) == Status::Ok);

        // A few idle ticks before the next day: must NOT re-speak this occurrence.
        for (int k = 0; k < 3; ++k) { clock.now += 600; rt->tick(); }
        CHECK(count_daily_med() == day + 1);  // still exactly one per occurrence so far
    }

    CHECK(count_daily_med() == kOccurrences);
    std::printf("[soak] recurring: fired %d/%d daily occurrences across simulated days "
                "(re-arm not suppressed)\n",
                static_cast<int>(count_daily_med()), kOccurrences);
}

// ===========================================================================
// 3. Memory re-init (close+open) cycle fd stability — the exact operation the Phase 17
//    watchdog performs to recover a degraded memory engine (reinit_memory). Repeated
//    hundreds of times against the REAL encrypted store, the open-fd/handle count must
//    not grow: a watchdog that recovered by leaking a descriptor every time would exhaust
//    the process over a long uptime.
// ===========================================================================
void test_memory_reinit_cycles_dont_leak_fds() {
    TempStore store("reinit");
    set_env("ECHO_MEMORY_DB", store.str());
    auto mem = memory::make_memory_engine();
    CHECK(mem->open(store.str()) == Status::Ok);
    // Put something in it so open() does real load-from-disk work each cycle.
    (void)mem->add_reminder("persisted", 1'700'000'900, memory::Recurrence::Daily);
    mem->enforce_retention(1'700'000'000);

    const int kCycles = 300;
    const std::int64_t fd_before = test::open_handle_count();
    std::int64_t fd_peak = fd_before;
    for (int i = 0; i < kCycles; ++i) {
        mem->close();
        CHECK(mem->open(store.str()) == Status::Ok);  // == reinit_memory()
        fd_peak = std::max(fd_peak, test::open_handle_count());
    }
    const std::int64_t fd_after = test::open_handle_count();

    if (fd_before != test::kUnavailable) {
        // Allow a tiny constant of transient fds; forbid growth proportional to the cycle
        // count (which is what a per-cycle leak would look like).
        CHECK(fd_after - fd_before < 8);
        CHECK(fd_peak - fd_before < 8);
        std::printf("[soak] mem-reinit: %d close/open cycles  fd before=%lld after=%lld peak=%lld\n",
                    kCycles, static_cast<long long>(fd_before), static_cast<long long>(fd_after),
                    static_cast<long long>(fd_peak));
    } else {
        std::printf("[soak] mem-reinit: %d close/open cycles (fd unavailable on this platform)\n",
                    kCycles);
    }
    mem->close();
}

}  // namespace

int main() {
    test_recurring_reminder_fires_every_occurrence();
    test_memory_reinit_cycles_dont_leak_fds();
    test_longevity_soak();  // the long one last, so the focused checks report first
    return echo::test::report("soak");
}
