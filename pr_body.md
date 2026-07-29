## Phase 9 — Architecture & project documentation

A **documentation-only** pass (no source changes). It adds the reference docs a
YC partner or a new engineer should read first, consolidating knowledge that was
scattered across nine phases of README history into four focused files.

### What's added

- **`docs/ARCHITECTURE.md`** — a single system diagram (Mermaid, validated to
  render) giving a 10-second mental model: the safety-critical core loop
  (sensor-pipeline → perception → cognitive-core's safe-mode gate → voice-ui), the
  apps layer with its hard isolation boundary, the HUD compositor, companion-sync,
  and the mock/real toggle for each external dependency. Plus a short walkthrough
  of the core loop and the isolation boundary.
- **`docs/STATE.md`** — honest project status: what's built vs. still pending
  real-world validation (the open Phase 4/5/6 gaps), **test coverage by module**
  measured with gcov/gcovr, and the CI/quality gates in place. Headline finding:
  the *newest* code (apps framework/appkit) is the best-tested while the **core
  safety-critical loop — perception (7%), sensor-pipeline/voice-ui/companion-sync
  (0%) — is the least-tested**; recorded plainly rather than glossed.
- **`docs/RUNBOOK.md`** — the consolidated, linear "run it for real" checklist:
  Level 0 (stub build) → Level 1 (real local AI) → Level 2 (real APIs), with the
  exact scripts, accounts, and env vars in order, plus the Windows runtime-DLL fix.
- **`docs/DECISIONS.md`** — a dated architectural decision log (embedded Linux
  base not a custom kernel; voice-first/no-touchscreen; local-first privacy
  enforced by interface shape; phone-as-Bluetooth-bridge; confirm-before-send;
  and more), each with its reasoning and honest trade-off.
- **README** — links all four docs from a new "Documentation" section at the top.

### Constraints honored

- **No source code changes** — diff is documentation only. Nothing surfaced that
  needed a code fix; STATE.md's known-issues section is where any such finding
  would go.
- **Every claim matches the repo as it is now** — coverage numbers are measured,
  not estimated; the "compiles but not run live" caveats from earlier phases are
  carried through verbatim; nothing aspirational is stated as done.
- **The diagram stays legible at a glance** — core modules only, not a class
  diagram; validated to render cleanly via mermaid-cli.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
