## Phase 17 — Fault tolerance & graceful degradation

Sixteen phases built the perception → cognitive → voice pipeline; **none had ever
asked what happens when a piece of it breaks mid-turn.** That gap matters more here
than in most software: the wearer is, by design, someone who may not notice or be
able to reset a malfunctioning device on their own face. This phase makes every
engine boundary containable, gives each failure a **deliberate** degraded behaviour,
adds a watchdog for genuinely hung engines, and — per this repo's discipline —
documents precisely which failure classes are now handled and which still bring the
process down.

### What's in it

- **Containment at every engine boundary.** A new
  [`EngineGuard`](boot/include/echo/boot/engine_guard.hpp) wraps each call into
  wake-word/ASR/vision, the LLM, TTS, and memory. It (1) turns a thrown exception into
  a `fail(HardwareError)` `Result` (built on the existing `common/result.hpp`
  `Status`, not a second error channel), (2) bounds a **hung** call by running it on a
  worker and abandoning it past a per-engine budget (`fail(Timeout)`), and (3) records
  health for the watchdog. A `try/catch` backstop wraps the whole tick.

- **A defined degraded mode per engine** (in `boot/src/runtime.cpp`), decided not
  accidental:
  - **Vision** fails → **voice-only**, and **never a fabricated face match**
    (constraint #2 — the same "don't guess" rule as the safe-mode gate).
  - **ASR/wake** fails → a calm spoken retry, not silence, not a crash.
  - **LLM** doesn't respond (throw/**timeout**) → a known-good engine-fault line +
    an `EngineDegraded` caregiver alert. This is **kept distinct from the low-confidence
    safe-mode gate** (constraint #1): the gate is an `Ok` `SafeMode` response decided
    inside `respond()`; a non-responding engine is a failed *call* handled by the runtime.
  - **Memory** write fails (disk full/corruption) → the reminder is **still delivered
    this session** and not dropped; logged; an in-session set stops it repeating.

- **A watchdog on the existing core tick** (no fourth timer — constraint #4). It
  recovers transient throws by construction, attempts in-process re-init of a hung
  engine, and escalates to a `reboot_required()` request when in-process recovery is
  exhausted (the honest fallback for a wedged thread C++ cannot kill).

- **Fault-injection tests** (`tests/fault_injection_test.cpp`, suite
  `echo-fault-injection`) drive the **real runtime** with fault-injecting fake engines
  and assert, at every boundary for throw/error/hang: no crash, the defined degraded
  behaviour, recovery on the next turn, and reboot escalation on a persistent hang.
  Runs entirely in the dependency-free stub build (constraint #3).

### Honest scope (the safety claim, stated per class)

This contains **misbehaviour**, not **undefined behaviour**. Covered: exceptions,
non-Ok statuses, and hangs at the guarded boundaries — none crash the process, and a
persistent unrecoverable hang escalates to a supervised restart. **Not** covered: a
segfault, memory corruption/UB, a `noexcept`-violation `std::terminate`, or OOM still
take the process down (a supervising init system or hardware watchdog is the only
recovery). A truly wedged thread leaks until restart and can block process exit — which
is exactly why the reboot path exists rather than a pretence of always-recover. The
full per-class ledger is in [`docs/STATE.md`](docs/STATE.md); the design rationale and
trade-offs are in [ADR-16](docs/DECISIONS.md); the recovery-scope narrative is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

### Verification

- **11 stub-build CTest suites green** (was 10), including the new `echo-fault-injection`.
- **clang-tidy and cppcheck clean** on the new/changed first-party sources (findings are
  hard errors in CI).
- The reference `echo-os` binary boots, ticks, and shuts down cleanly on the new
  guarded path.

### Constraints honored

1. Safe-mode confidence gate **not** weakened or bypassed — engine-not-responding is a
   separate path from responded-but-unsure, asserted to speak different lines.
2. Degraded fallbacks **never fabricate** (vision failure reports no face).
3. Stub build stays dependency-free and fast; fault injection needs no real engines.
4. The watchdog reuses the existing boot/scheduler tick — no fourth timer.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
