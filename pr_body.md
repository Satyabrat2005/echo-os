## Phase 15 — The memory & recall engine (the actual dementia-care core)

Every phase before this built real, working **generic** voice-assistant plumbing:
wake word, transcribe, route to an app, speak a reply. None of it was actually about
dementia. The pitch — *"glasses that remember for you"* — needs a module that
**remembers**, and there wasn't one: nothing on-device persisted who a face belongs
to, what was said about them, or what the wearer needs reminding of.

This phase adds that core: a new **`memory/`** module — a local, private, on-device
store of **people**, **reminders**, and a narrow **event log** — and wires it *into*
the existing perception → cognitive → voice loop. It is the first phase since the
scaffold that adds a genuinely **new capability** rather than making an existing one
real. "Who is this?" now answers *"That's Priya, your daughter. You last saw her two
days ago,"* recalled from the real record — not a bare "face detected," and not a
guess.

### What's added

- **`memory/` module** mirroring the existing layout:
  [`memory_engine.hpp`](memory/include/echo/memory/memory_engine.hpp)/`.cpp`, its own
  `CMakeLists.txt`, and its own unit tests. It stores:
  - **Person records** — a stable identity key (vision's **SFace embedding**, matched
    by cosine similarity — never a raw image), a display name + relationship the
    wearer stated, accumulating notes, and last-seen.
  - **Reminder records** — text + due time + recurrence (once/daily/weekly) +
    acknowledged state.
  - **Event log** — an append-only log of *notable* moments only (a known person
    seen, a reminder fired/acknowledged). **Not** a transcript store.
- **Real persistence: SQLite** (vendored amalgamation v3.46.1, public domain, in
  [`memory/vendor/sqlite3/`](memory/vendor/sqlite3/)), compiled in-tree as
  `echo-sqlite3`. Records survive restarts — proven by a close/reopen test. It is the
  honest embedded choice over a hand-rolled file (ADR-13), and stays dependency-free
  so the stub build is unaffected.
- **Wired into the pipeline, not around it:**
  - `perception` now surfaces each face's SFace **embedding** on `FaceObservation`;
    `cognitive-core` matches it against the store and returns an **enriched recall**
    *before* the safe-mode gate (retrieval is a fact, not an LLM guess).
  - Reminders are checked on the **existing `boot/` core tick** (no second timer
    loop — constraint #3) and spoken through the existing `voice-ui` path.
  - A new **`[route:memory]`** tag — parsed by Phase 6's **unchanged** route-tag
    parser (constraint #3) — lets the LLM defer an on-device memory question ("did I
    take my medication today?") to the engine, which answers from the **real event
    log** (or returns nothing, so the system falls back rather than fabricate).
- **Tests** (green in the dependency-free stub build):
  [`tests/memory_engine_test.cpp`](tests/memory_engine_test.cpp) — CRUD, recurrence,
  the RAG medication query, persistence-across-reopen, utterance parsing, and **the
  no-auto-create-from-an-unnamed-face rule**; plus a new turn in
  [`tests/e2e_pipeline_test.cpp`](tests/e2e_pipeline_test.cpp) driving a fixture
  embedding + a prior "this is Priya" utterance to an enriched recall, end to end,
  with no hardware.

### Safety & privacy, made concrete (not aspirational)

- **A person is created ONLY when the wearer names one.** An unknown face with no
  naming utterance creates **zero** records — recognizing a stranger never invents an
  identity. Asserted directly (`test_unknown_face_creates_no_person`) and end to end.
- **The store's raw content cannot reach `companion-sync`.** Phase 10 proved (via
  `static_assert`) that the sync transport can't accept a `SensorFrame`; this PR
  **extends that same proof** to embeddings, person records, and events. It was
  confirmed to *bite* — a deliberately-wrong "a leak path exists" assertion fails to
  compile.
- **No new "phone home" path** (constraint #2): the module makes zero network calls;
  a future caregiver view stays a `companion-sync` boundary discussion, not something
  opened here.
- **Narrow persistence scope** (documented in `docs/ARCHITECTURE.md`): only
  caregiver-legible summaries are logged ("Saw Priya", "Reminded: take medication") —
  never verbatim conversation.

### Honest limits (constraint #4 + docs/STATE.md gaps 9–11)

- **File-permission restriction, NOT encryption at rest.** Best-effort owner-only
  perms (chmod 0600 on POSIX; `std::filesystem` maps loosely on Windows). True
  encryption is **SQLCipher**, a deliberate follow-up (ADR-13). Stated plainly, not
  overclaimed.
- **SQLite built cleanly under MinGW** — ~10 s, zero warnings, links and runs first
  try on the project's MinGW-w64 ucrt g++ 15.1 toolchain. Unlike the ORT/OpenCV
  native-dep friction in Phase 14b, there was **no** toolchain gap to work around, so
  none was invented. (One change: C enabled as a project language for that one TU.)
- **Still unproven, and labelled as such:** multi-day real-world recall accuracy
  (tests use fixture embeddings); note summarization/pruning so the store stays
  bounded (today it only accumulates); and — as ever — any use by a real wearer.

### Impact on existing behavior

None when no store is attached. Every memory branch in `cognitive-core` is guarded on
an attached, open engine, so the pre-Phase-15 behavior is byte-for-byte unchanged and
**all previously-green tests still pass** (10/10 CTest suites green locally on MinGW,
clean-from-scratch). clang-tidy and cppcheck now cover the new first-party `memory/`
code (findings fixed); the vendored `sqlite3.c` is excluded from both as upstream C.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
