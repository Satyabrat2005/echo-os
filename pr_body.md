## Phase 18 — Power & thermal management

`power-mgmt` has been a top-level module directory since the very first scaffold — named
alongside boot, perception, cognitive-core, voice-ui, and the rest — but across seventeen
phases it was **never given real logic**: a placeholder DVFS actor and a hardcoded
`evaluate()` call were all it had. Every other module on that original list has since been
made real, tested, and hardened. Power and thermal behaviour was the last dormant item —
and it matters more here than in almost any other device: this is worn all day by someone
who may not notice a dead battery, an uncomfortably hot temple, or a device silently
throttling itself into uselessness by mid-afternoon. Phase 17 made ECHO OS resilient to
*software* failures; Phase 18 makes it resilient to the *physical* reality of limited power
and real heat — and, per this repo's discipline, is precise about what is policy-verified
versus still hardware-unvalidated.

### What's in it

- **A real sensing boundary + a deterministic fake.**
  [`IPowerSource`](power-mgmt/include/echo/power/power_source.hpp) (battery percent +
  charging) and `IThermalSource` (a coarse `ThermalState` nominal/warm/hot, plus an optional
  numeric SoC temp as telemetry) mirror the `IHttpClient`/engine-interface discipline: a real
  backend hook and a fully deterministic fake (`fake_power_source.hpp`). The thermal signal is
  a discrete STATE by choice — a temple-worn device's realistic input is a few thermal-zone
  trip points, not a calibrated skin temperature, and the policy needs a throttle *level*
  ([ADR-17](docs/DECISIONS.md)).

- **Battery-driven vision duty-cycling** ([`power_policy.hpp`](power-mgmt/include/echo/power/power_policy.hpp)).
  Continuous camera face-detection is the expensive, always-on cost, so the sampling cadence
  drops as charge falls: **full-rate above 40 %, reduced (1-in-4) 15–40 %, vision-off /
  voice-only below 15 %**. A dropped frame is DROPPED — recognition just happens less often;
  **nothing is fabricated** to hide the lower rate (the same "never guess" rule as Phase 15's
  naming-gate and Phase 17's degraded modes). Microphone frames are never duty-cycled, so a
  vision-off device stays fully responsive to speech.

- **A thermal-aware inference throttle, wired INTO the Phase-17 watchdog.** A warm/hot temple
  relaxes the cognitive hang budget (×1.5 / ×2.0) so a legitimately-slower throttled turn is
  not abandoned as if it were hung — the throttle scales the *existing* guard budget up rather
  than adding a parallel timer. Thermal pressure also forces at-least-reduced vision even on a
  full battery.

- **Low-battery / high-thermal ride the SAME caregiver channel.** Both surface through the
  existing `AlertKind::EngineDegraded` alert path (edge-triggered, so a standing condition
  alerts once) — **not** a second, parallel device-health mechanism (the brief's constraint,
  honoured). The specific condition is in the alert note.

- **A low-battery critical-reminder pass.** When the battery crosses a critical floor, a
  best-effort final delivery of due reminders fires before a possible shutdown, and reminder
  delivery is never duty-cycled or throttled away — a low battery must not be what silences a
  medication reminder.

- **Tests** (`tests/power_mgmt_test.cpp`, suite `echo-power-mgmt`) assert all of the above in
  two layers: the pure policy against scripted readings, and the same behaviour through the
  **real runtime** driven by fake sources over the defined threshold transitions (including
  that a dropped frame is never fabricated and a source read-fault neither crashes the loop nor
  invents a charge). The Phase-17 fault-injection fakes were extracted into a shared
  `tests/fake_engines.hpp` and reused, not duplicated.

### Honest scope (a NEW, permanent-until-hardware gap)

This closes a **design and policy** gap, *not* a hardware-validation one — conflating the two
would overstate what's proven, the mistake avoided since Phase 9.

- **No real sensor exists to read.** There is no fuel gauge or skin-adjacent thermal sensor on
  a dev laptop or the not-yet-existing glasses, so the real backend is a **documented stub**:
  the *shape* of the sysfs / fuel-gauge read is captured, the *values* are simulated.
- **The critical-reminder pass is doubly speculative** — it fires on a low-charge THRESHOLD,
  not a real "about to die" signal, and there's no guaranteed post-alert power budget.
- **Real thermal behaviour is unproven** — whether these throttle levels keep a temple-worn
  device comfortable needs the real hardware.

Only the policy LOGIC is verified. This is logged as gap #12 in
[`docs/STATE.md`](docs/STATE.md), with the same honesty as the live-mic gap; the design
rationale and trade-offs are in [ADR-17](docs/DECISIONS.md); the integration narrative is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

### Verification

- **12 stub-build CTest suites green** (was 11), including the new `echo-power-mgmt`;
  `echo-fault-injection` stays green on the shared fakes.
- **clang-tidy and cppcheck clean** on the new/changed first-party sources (findings are hard
  errors in CI).
- The reference `echo-os` binary boots, initializes the power/thermal sources, ticks, and
  shuts down cleanly on the new path.

### Constraints honored

1. Reduced sampling **never fabricates** a recognition result — a dropped frame is dropped,
   not guessed (same non-negotiable as Phase 15's naming-gate and Phase 17's degraded modes).
2. Low-battery/thermal reuse the Phase-17 `EngineDegraded` alert vocabulary — no parallel
   device-health channel.
3. The stub build stays **dependency-free** — the whole module is testable with fake
   power/thermal sources, no real sensor required.
4. `docs/STATE.md` states plainly this closes a **policy** gap, not a hardware one.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
