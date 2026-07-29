# Phase 7: continuous integration

Six phases were reviewed and tested by hand before each merge. That worked — it
caught the Phase 6 JSON stack-overflow and Gmail header-injection bugs — but only
because someone remembered to run the full suite every time. This phase makes that
automatic and mandatory via GitHub Actions, so a red build blocks merge instead of a
forgotten manual step letting a regression through.

No product code changes here: this is purely the CI harness plus documentation.

## What's in this PR

- **`.github/workflows/ci.yml`** — runs on every push and every PR targeting
  `master`, with three jobs:
  1. **Stub build** — configures with every `ECHO_WITH_*` / `ECHO_REAL_AI` flag OFF
     (the dependency-free default since Phase 1), builds, and runs the **full**
     `ctest` suite. Zero external deps, zero secrets — the always-green safety net
     every future phase depends on.
  2. **Network build** — installs libcurl, configures `-DECHO_WITH_NETWORK=ON`,
     builds, and runs the appkit + apps suites: JSON parsing (incl. the deep-nesting
     overflow guard), the Gmail header-injection regression test, the
     confirm-before-action gate, and the NLU mail/browser routing fix. Proves the
     Phase 5/6 network branch compiles and passes on a clean machine, not just a
     developer's laptop. **No live API calls** — the tests are credential-free;
     libcurl only needs to link.
  3. **Secret scan** — runs the existing `scripts/check_secrets.sh` rules over the
     tree so an API key, OAuth secret, private key, token cache, or committed `.env`
     fails the run and blocks merge.
- **README** — a CI status badge at the top, a new **Continuous Integration** section
  (what's covered, what's deliberately excluded and why, how to extend it), and the
  branch-protection setup documented as a manual repo-settings step.

## Constraints held

- **No secrets in the workflow.** No credentials are present or needed; the network
  job compiles and runs credential-free tests only.
- **Fast feedback.** ccache caches compiled objects across runs; a newer push cancels
  the in-flight run for the same branch.
- **Fail loud.** Any failing job fails the run. Making that *block merge* is the
  branch-protection step below — the actual point of the phase, not just a badge.

## Deliberately excluded — the real local-AI matrix

The `ECHO_REAL_AI=ON` build (whisper.cpp / llama.cpp / Porcupine / Piper) is **not**
run in CI, by design. It needs multi-gigabyte model downloads that blow the CI time
budget, and Porcupine needs an account-gated Picovoice access key — a secret we will
not put in a workflow. The stub build is the deliberate dependency-free proxy that
exercises the same code paths with stub adapters. The README's CI section documents
this and sketches how to add real coverage later (cache-hosted models + a Porcupine
key supplied as a repository secret).

## Manual step for whoever has repo admin

Branch protection is a GitHub settings action, not something committed in code. Under
**Settings → Branches** for `master`: **require status checks to pass before merging**
and select all three CI checks (*Stub build*, *Network build*, *Secret scan*), plus
**require branches to be up to date before merging**. Until that's enabled the badge
is informational only.

## Verification done here

- Workflow YAML parses and has the expected structure (`on: push/pull_request`, three
  jobs). No `actionlint`/`yamllint` was available locally, so the schema is validated
  by the parse plus review; the run itself is confirmed only once this branch is
  pushed and Actions fires it.
- `scripts/check_secrets.sh` passes clean against the current tree (the secret-scan
  job will be green on merge).
- The network job's `ctest -R "appkit|apps|nlu"` filter was verified against the
  existing build to select exactly `echo-appkit-tests`, `echo-apps-smoke`, and
  `echo-nlu-routing`.
