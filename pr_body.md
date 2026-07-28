# Phase 6: NLU routing fix + network-build verification (live validation pending)

This phase does **not** add features. It fixes the known NLU routing bug, verifies
the third-party network build actually compiles, and hardens two backends — the
code-side of closing the "code complete, live validation pending" gap left open by
Phases 4 and 5. The **live** run (real accounts, real hardware, a human tester,
measured latency) is still pending and is tracked in the TODO blocks below.

> ⚠️ **Honesty banner — read before merging.** Part A (bug fix) and every
> code-verifiable item below are done and green in CI. The parts that require real
> accounts, real hardware, a microphone, and a human tester are marked with
> explicit TODO blocks and are **not** filled by code — they are filled by actually
> doing the run. Do not read the checked items as "the whole demo works end to end"
> until the TODO blocks below carry real evidence and measured numbers.

## Part A — NLU mail/browser routing bug (DONE, verified)

The keyword NLU matched the browser's generic `read` at the front of *"read my
unread email"* before mail's `unread`, misrouting mail commands to the browser.

- **Fix** (`apps/app-framework/…/nlu.{hpp,cpp}`): intent selection is now
  specificity-aware. Generic catch-all verbs (`read`, `open`) only win as a
  fallback; a domain-specific intent anywhere in the utterance takes precedence,
  with position breaking ties within a tier. Added `message`/`messages` to the mail
  app's intents.
- **Regression test** (`echo-nlu-routing`, `apps/tests/nlu_routing_test.cpp`):
  covers the exact broken phrase plus `read my messages`, `check my mail`,
  `open my inbox`, and negative cases that must stay on the browser
  (`read this page`, `open wikipedia`).
- Verified three ways: `ctest` (4/4), and end-to-end through the real
  `echo-apps --mock --in-process` host (`read my unread email`→mail,
  `open my inbox`→mail, `read this page`→browser, `play some jazz`→media).

## Part B — validation progress

### Done and verified in this repo's toolchain
- **`ECHO_WITH_NETWORK=ON` now compiles + links** (it never had, anywhere). Built
  against **libcurl 8.21.0** (official curl win64-mingw, UCRT/SChannel — no extra
  runtime DLLs, no libstdc++ ABI risk). `http.cpp` emits real `curl_easy_*` calls,
  the app binaries import `libcurl-x64.dll` (transport retained, not stripped), and
  all four CTest suites pass in the network build. `scripts/setup_deps.ps1` now
  provisions libcurl into the install prefix, and `scripts/build_real.ps1` enables
  the network transport by default (the full demo is local AI **and** the live
  integrations together — `ECHO_REAL_AI` alone does not imply it).
- **Two real bugs found by review and fixed here** (this phase's mandate is to fix,
  not defer):
  - *JSON parser stack-overflow on deeply-nested input* — the hand-rolled reader
    parses untrusted network responses and had unbounded recursion, violating its
    own "never crash on garbage" contract. Added a depth cap; regression test
    `test_json_deep_nesting_is_null_not_crash`.
  - *Gmail email-header injection* — `gmail_build_raw_message` put `To`/`Subject`
    into RFC 2822 headers unescaped, so an embedded CRLF could forge a header
    (e.g. a hidden `Bcc`). Header fields are now sanitized; regression test
    `test_gmail_no_header_injection`.

### TODO — the real run (fill with real evidence before "done")

**1. Full real dependency install + `ECHO_REAL_AI=ON` build.**
<!-- TODO: whisper/llama/OpenCV/SDL2/Piper/Porcupine installed; paste build_real.ps1 result and models/INSTALLED_VERSIONS.md. libcurl is 8.21.0 (done); fill the rest. -->

**2. Live credentialed smoke run** (real Spotify/Gmail/Search/YouTube accounts):
<!-- TODO: voice playback, a real unread email read aloud, a real search summarized, and the send-email confirm gate end to end. Attach screenshots/logs. -->

**3. ≥20 real end-to-end turns + measured latency.**
<!-- TODO: run analyze_latency.ps1 over latency_log.csv; paste the min/avg/max table and the measured-vs-budget gap line. Do NOT paste the target budget as if measured. -->

**4. Non-founder tester, zero instructions beyond "say 'Hey ECHO' and talk to it".**
<!-- TODO: what they tried, what worked, where they got stuck. This is the one signal beyond the team that has never happened. -->

**5. Known Issues actually observed** (local-AI pipeline + live integrations):
<!-- TODO: real rate-limit / token-expiry / network-flake / ASR / wake-word failures, replacing the "anticipated" rows in the README. -->

## Constraints held
Stub/CI build stays zero-dependency and green; the safe-mode core, apps isolation,
and latency-budget model are unchanged. `ctest`: `echo-smoke`, `echo-appkit-tests`,
`echo-apps-smoke`, `echo-nlu-routing` — all pass in both the stub and network builds.
