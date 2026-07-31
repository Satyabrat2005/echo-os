## Phase 16 — Hardening the memory store (encryption at rest + retention)

Phase 15 shipped the memory engine and, in its own `STATE.md`, flagged two gaps it
left open on purpose rather than glossing over: the on-device store was protected by
**file permissions, not encryption at rest**, and there was **no retention policy**, so
notes and the event log grew unbounded forever. Both sit on a module that stores a
wearer's face-linked identity data. This phase closes them — and, in the same
honest-framing style, says exactly what the new protection does and does **not** buy.

### Part 1 — Encryption at rest

**SQLCipher was tested for real first, then ruled out — with a concrete reason.**
SQLCipher is the textbook answer, so it was smoke-tested before committing hours to it
(the same discipline Phase 15 used on the SQLite amalgamation and Phase 14b on ORT). The
finding: SQLCipher is **not** a self-contained amalgamation like SQLite — it must be
generated from a source tree **and** linked against a crypto backend (OpenSSL
`libcrypto`), and this MinGW toolchain has **no OpenSSL** (verified: no `openssl/*`
headers, no `libcrypto`; a bare `-lcrypto` link fails). Pulling OpenSSL in would break
the dependency-free, vendorable stub build (constraint #3) — the exact native-dep
friction Phase 14b documented. So, per constraint #4, this ships the **documented
fallback**, not a forced heavy dependency and not a silently-dropped requirement.

**What shipped:** the working database is held in an **in-memory** SQLite connection
(loaded via `sqlite3_deserialize`), and its serialized image is written to disk as
**AES-256-CTR ciphertext** (`ECHOAES1` magic · random IV · ciphertext). The AES-256 is a
small, dependency-free, in-tree implementation
([`memory/src/aes256.cpp`](memory/src/aes256.cpp)) **proven against the published
FIPS-197 and NIST SP 800-38A known-answer vectors** in the tests — nothing trusted on
faith. All the Phase-15 SQL logic is unchanged; only the open/close paths differ.
Plaintext **never touches disk** — it exists only in process RAM.

**Key management, stated accurately.** A random **per-device key**, generated once at
first boot into a local owner-only key file — the same "least-bad local option" already
used for OAuth secrets in appkit's `.echo-tokens/`. Honestly a protected local key file,
**not** a secure-enclave key.

**Migration.** An existing **unencrypted** Phase-15 `memory.db` (detected by the
`SQLite format 3` magic) is migrated in place to the encrypted format on open, logged
loudly — a wearer's person/reminder records are never silently discarded. Proven with a
real plaintext fixture DB checked into [`tests/fixtures/`](tests/fixtures/).

**Honest scope (see ADR-14).** This provides **confidentiality** at rest — a copied /
backed-up / offline-imaged `.db` is unreadable without the key, a plain `sqlite3_open` of
the raw file reads garbage, and stored names do not appear in it (all asserted). It is
**not** authenticated (no MAC; wrong key / corruption is caught by a post-decrypt
SQLite-magic sanity check and the engine fails **closed**), **not** hardware-backed (an
attacker with live read access to both files can decrypt), and durable at **checkpoint**,
not per-transaction. All acceptable for a wearable's small store; all said plainly.

### Part 2 — Retention and pruning

- **Event log — bounded by count + age** (defaults 2000 rows / 90 days,
  `ECHO_MEMORY_MAX_*`-overridable), enforced on the **same scheduler tick** that delivers
  reminders (no third timer — constraint held). Pruning touches only the events table, so
  a retained acknowledgment's reminder state is intact (asserted: oldest evicted, newest
  kept, the acknowledged reminder still closed).
- **Notes — cap-and-evict, not aged out.** Notes are the long-term value, so they are
  **not** pruned by age; instead a per-person segment cap evicts oldest-first (default
  20), applied both on the tick and immediately on each `add_note`.
- **LLM summarization considered and deferred (ADR-15).** Given Phase 15's documented
  tiny-model prompt fragility, making note retention depend on the local LLM would trade
  a guaranteed bound for a probabilistic one on a data-loss-sensitive path. Cap-and-evict
  is the honest, robust first pass — exactly the "don't force a fragile feature" call the
  brief asked for.

### Tests (green in the dependency-free stub build)

Extends [`tests/memory_engine_test.cpp`](tests/memory_engine_test.cpp):
- **AES known-answer vectors** (FIPS-197 + NIST SP 800-38A) pin the cipher.
- **Encryption at rest** — the raw file is not a plaintext SQLite db, the stored name is
  absent from it, the right key recovers everything, and a **wrong key fails closed**.
- **Migration** from the checked-in plaintext fixture — records survive, file becomes
  ciphertext.
- **Bounded event log** and **bounded notes** — eviction proven, ack state preserved.

### Constraints held

- The Phase-15 **compile-time privacy proof still bites** (re-verified: inverting a
  `static_assert` to claim a leak *exists* fails to compile).
- The **person-record-only-on-naming** rule and the **reminder-delivery** path are
  unchanged.
- The **stub build stays dependency-free** — AES + key management are plain in-tree C++,
  no new external package (SQLCipher/OpenSSL avoided by design). 10/10 CTest suites green
  locally on MinGW; clang-tidy/cppcheck clean on the new first-party code (the vendored
  `sqlite3.c` stays excluded as upstream C).

`docs/STATE.md`, `docs/ARCHITECTURE.md`, and `docs/DECISIONS.md` (new **ADR-14**,
**ADR-15**) are updated with the real outcome of both parts.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
