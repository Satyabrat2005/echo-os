## Phase 20 — longevity & resource-leak soak testing

Every test in this repo before now exercised a **single** turn, a single fault, or a
bounded scenario. None of them proved the one thing a device meant to be *worn all day,
every day* needs most: that it runs **continuously, for a long time, without degrading** —
no memory creep, no scheduler drift after enough ticks, no watchdog recovery that leaks a
worker each time, no retention pass that only ever ran once in a unit test. This PR builds
the harness that actually looks for that class of bug and puts **real measured numbers** on
each — and it found and fixed **two genuine longevity bugs** in the process.

### How it's done (honestly)

`tests/soak_test.cpp` drives the **real** runtime (`boot/runtime.cpp`) with the **real**
memory engine (SQLite + encryption at rest) and the shared Phase-17/18 deterministic fakes
for the other five engines — through a large number of ticks, but in **simulated time**,
never `sleep()`. The runtime gained a one-line clock seam (`Runtime::set_clock`) so the
reminder scheduler reads an injected **virtual clock**; the soak advances it 60 simulated
seconds per tick. That compresses **10,080 ticks into exactly 7 simulated days** and runs in
seconds — the only honest way to reach a multi-day duration inside a CI budget
(`ECHO_SOAK_TICKS`-overridable for a longer local run).

Reuses the Phase-17/18 fake-engine + fault-injection infrastructure — no third parallel
harness (`fake_engines.hpp` extended additively: a transient-hang budget + bounded-recording
helpers). Cross-platform resource sampling (`tests/resource_probe.hpp`): `/proc` on Linux
(so the RSS/fd assertions run for real in CI), psapi on Windows, self-skip elsewhere.

### Measured numbers (default 7-simulated-day run, this commit)

| Metric | Bar | Measured | Verdict |
|--------|-----|----------|---------|
| Reminder-scheduler drift (600 one-shots across the week) | fires once, within one tick, no cumulative drift | 600/600, **0 early**, **worst drift 59 s** (< 60 s tick) | ✅ |
| Recurring reminder re-arm across days | fires on **every** acknowledged occurrence | 4/4 daily occurrences | ✅ (bug fixed) |
| Event-log retention at scale (Phase 16) | ≤ cap despite thousands of firings | **200 rows** at cap 200 after 600 firings | ✅ |
| Process RSS plateau | growth < 15 % and < 16 MiB | **+~1–2 % (~0.1 MiB)** | ✅ |
| Open fd / handle stability | no growth with save-to-disk churn | span **≤ 5** | ✅ |
| Memory re-init (watchdog close+open) fd | no leak per cycle | **300 cycles, fd unchanged** | ✅ |
| Watchdog recovery (transient hang every 900 ticks) | recovers every time, no reboot, no worker leak | **11 episodes, 0 reboots, peak 1 abandoned worker, drains to 0** | ✅ |

### Two real longevity bugs — found and root-cause fixed (not papered over)

1. **Recurring reminders went silent after day one.** The runtime de-duplicated in-session
   reminder delivery by reminder *id*, permanently. A recurring reminder that the wearer
   acknowledges *re-arms* to a new due time, but the id-only record suppressed every
   occurrence after the first — a **daily medication reminder would fire once and never
   again until reboot**. Fixed by keying the record by *occurrence* (id → delivered `due`):
   a re-armed occurrence delivers; the original failed-write backstop (same occurrence,
   still "pending") is preserved. Regression test asserts 4/4 daily occurrences fire.
   (`boot/.../runtime.hpp` `delivered_occurrence_`.)

2. **The store rewrote its entire encrypted image to flash on every tick.** Retention runs
   on the core tick; `prune_events` marked the store dirty every time the DELETE *executed* —
   even when it evicted **nothing** (the common case at cap) — forcing a full
   `sqlite3_serialize` + AES-256 + file rewrite in `enforce_retention` **every tick,
   forever**: constant flash wear + CPU proportional to store size, for no state change.
   (It also made the soak infeasibly slow — ~170 ms/tick — until fixed.) Fixed by gating
   the dirty flag on `sqlite3_changes(db_) > 0`; an idle retention pass is now a couple of
   cheap SELECTs and **no disk write**, a real eviction still persists exactly as before.
   (`memory/src/memory_engine.cpp` `prune_events`; `echo-memory` still green.)

### CI

New `soak` job — dependency-free stub build, its own **time-bounded** job (scoped like the
real-LLM one) so the longer run never slows the always-green stub baseline. Excluded from
the fast `stub-build` and `coverage` ctest runs; runs in parallel with everything else.

### What this does NOT prove (new gap #14, permanent-until-hardware)

Proves the **orchestration layer** — runtime loop, scheduler, retention/eviction, the
fault-tolerance watchdog, and the memory store's own serialize/encrypt/persist cycle —
doesn't leak or drift over a compressed multi-day run. It does **not** instrument the real
third-party engine libraries (whisper.cpp, llama.cpp, OpenCV, ONNX Runtime): the soak drives
*fakes* in their place, so a slow leak *inside* one of those over real multi-day wall-clock
uptime would not be caught here. That stays gap #14, provable only by real engines on real
hardware for real days. Simulated-time soak with fakes is a real longevity proof of
everything ECHO actually wrote — and honest about the boundary where third-party code and
real time take over.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
