# Phase 5: real third-party integrations (code complete; live validation pending)

Replaces the mock Spotify / Gmail / Google-Search / YouTube backends with real
network-facing ones **behind the exact `IApp` interfaces the apps already exposed**
— swapping the backend changes not one caller. The `--mock` default is untouched,
so CI and the stub build stay deterministic and credential-free; real backends are
opt-in via a new `-DECHO_WITH_NETWORK` CMake option.

> ⚠️ **Read before merging — this PR is honest about what has and hasn't run.**
> Like the Phase 4 PR (which merged as *"prep … hardware validation pending"*), the
> code here is written and compile/test-verified, but the **live, credentialed run
> has not happened**, and it is gated on Phase 4's still-unfilled hardware
> validation. The three TODO blocks at the bottom are the proof-of-real steps and
> are intentionally blank. Do not treat this as "the integrations work end to end"
> until they are filled.

## What's in this PR (verified in the default stub build)

- **`apps/appkit/`** — shared toolkit every real backend links: the single network
  boundary (`net::IHttpClient`, libcurl behind `ECHO_WITH_NETWORK`), a minimal JSON
  reader, URL/base64 encoding, `.env`/credential loading, an OAuth token cache
  (gitignored `.echo-tokens/`), the **confirm-before-action gate**, and headless
  readability extraction. Everything except the libcurl transport is
  dependency-free and **unit-tested** (`echo-appkit-tests`).
- **Real backends** for media (Spotify Web API), mail (Gmail API), search (Google
  Custom Search), browser (live fetch + on-device readability, reusing the search
  result set), and video (YouTube Data API). Each app selects a mock or real backend
  from the same `--mock`/`--real` flag; a missing credential, an absent network
  transport, a rate limit, a token-refresh failure, or a timeout all **degrade to a
  calm spoken line, never a crash** (constraint #2).
- **Confirm-before-send** (constraint #1): `"reply saying …"` stages and speaks the
  message; only an explicit `"send"`/`"confirm"` sends. Cancel, an unrelated command,
  or an unrecognized word all **fail safe** — nothing sent, pending cleared (one-shot).
- **Secret hygiene** (constraint #3): `.env`/`.echo-tokens/` gitignored, only
  `.env.example` tracked; `scripts/check_secrets.{sh,ps1}` blocks a commit that
  would leak a key/secret/token.
- **README Phase 5 section**: how to obtain each credential (and under which
  account), the OAuth-flow choice + rationale, the manual test flow, and an honest
  Known Issues table.

## Constraints held

- Safe-mode core, apps isolation, latency budget untouched. The stub build stays
  zero-dependency and green; `echo-appkit-tests` and `echo-apps-smoke` (incl. the
  new `test_send_email_confirmation_gate`) both pass under `ctest`.

## Honesty caveats (do not paper over)

- **Live APIs not exercised.** No Spotify/Gmail/Search/YouTube call has run against
  real credentials. Request-building and response-parsing are unit-tested with a
  fake HTTP client; the sockets are not.
- **libcurl branch not compiled here.** The dev environment had no libcurl, so the
  `-DECHO_WITH_NETWORK=ON` path is written to the documented API but uncompiled —
  the first real build may need a small fix.
- **One-time OAuth authorize is a manual paste** for now; `scripts/authorize.*` is
  a documented TODO.

## TODO — real credentialed validation (fill before this is "done")

**1. Network build compiles.**
<!-- TODO: paste the `-DECHO_WITH_NETWORK=ON` configure+build result; note libcurl version and any fix needed. -->

**2. Live smoke run** (real creds on the laptop, per README manual test flow):
<!-- TODO: Spotify playback by voice, a real unread email read aloud, a real search summarized, and the send-email confirm gate end to end. Attach screenshots/logs. -->

**3. Known Issues actually observed:**
<!-- TODO: the real rate-limit / token-expiry / network-flake failures you hit, replacing the "anticipated" rows in the README. -->
