# Vendored SQLite amalgamation

This directory contains the official SQLite **amalgamation** (`sqlite3.c` +
`sqlite3.h`), version **3.46.1** (2024-08-13), downloaded verbatim from
<https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip>.

## Why it is committed here (and not under `third_party/`)

`third_party/` at the repo root is `.gitignore`d — it holds the large,
rebuilt-from-source engine deps (whisper.cpp, llama.cpp, OpenCV, ORT). The memory
engine is different: it must build and pass tests in the **dependency-free stub
build** (see the top-level `README.md` and `docs/STATE.md`), because persistence
across restarts is the whole point of the module and cannot be gated behind a
real-engine flag. The single-file amalgamation is the canonical way to embed
SQLite with zero external dependencies, so it is vendored directly and compiled
as part of the tree.

## License

SQLite is in the **public domain** — there are no restrictions on its use. See
<https://www.sqlite.org/copyright.html>. Nothing here is Anthropic/ECHO code.

## Build

`memory/CMakeLists.txt` compiles `sqlite3.c` into a small static library
(`echo-sqlite3`) with warnings disabled (it is upstream C we do not lint) and a
minimal, honest set of compile-time options:

- `SQLITE_OMIT_LOAD_EXTENSION` — no dynamic extension loading (attack surface we
  never use on-device).
- `SQLITE_THREADSAFE=1` — serialized mode; the store may be touched from the
  perception/cognitive/reminder paths.
- `SQLITE_DQS=0` — no double-quoted string literals (stricter, safer SQL).
- `SQLITE_DEFAULT_MEMSTATUS=0` — drop the per-alloc memory bookkeeping we do not
  read (small footprint win on an embedded target).

It compiles clean under the project's MinGW-w64 ucrt g++ 15.1 toolchain in ~10s
with no warnings — see ADR-13 in `docs/DECISIONS.md`.
