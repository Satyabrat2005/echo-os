#!/usr/bin/env bash
# ECHO OS — clang-tidy over the first-party sources (Phase 8).
#
# Runs clang-tidy (using the repo-root .clang-tidy ruleset) over every first-party
# translation unit in the compile database, in parallel, and FAILS if any enabled
# check fires — WarningsAsErrors in .clang-tidy makes each finding a non-zero exit.
# This is the automated backstop for the bug CLASS that manual review missed once
# already (the Gmail header-injection): bugprone-/cert-/clang-analyzer-security.
#
# It needs a compile_commands.json, so configure first:
#   cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
#   scripts/run_clang_tidy.sh build
#
# Args:  $1 = build dir (default: build).  Env: CLANG_TIDY overrides the binary.
set -uo pipefail

BUILD_DIR="${1:-build}"
DB="$BUILD_DIR/compile_commands.json"
if [ ! -f "$DB" ]; then
    echo "error: no compile_commands.json at '$DB'" >&2
    echo "       configure with: cmake -S . -B '$BUILD_DIR' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 2
fi

CT="${CLANG_TIDY:-clang-tidy}"
if ! command -v "$CT" >/dev/null 2>&1; then
    echo "error: '$CT' not found on PATH" >&2
    exit 2
fi

# First-party sources only. Skip tests and the fuzz harnesses (they are exercised
# by their own suites/jobs), and anything outside the module tree.
mapfile -t FILES < <(python3 - "$DB" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
first = ("apps", "common", "perception", "cognitive-core", "sensor-pipeline",
         "voice-ui", "companion-sync", "power-mgmt", "boot")
seen = set()
for x in d:
    f = x["file"].replace("\\", "/")
    if f in seen:
        continue
    seen.add(f)
    if any(("/" + s + "/") in f for s in first) and "/tests/" not in f and "/fuzz/" not in f:
        print(x["file"])
PY
)

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "error: no first-party sources found in $DB" >&2
    exit 2
fi

echo "clang-tidy: analyzing ${#FILES[@]} first-party translation units with $($CT --version | head -1)"

# -std forced to c++17: the build compiler's default standard may exceed the one
# clang-tidy's driver assumes, which would otherwise hide C++17 stdlib symbols
# (std::string_view, std::optional) and spew false clang-diagnostic errors.
JOBS="$(nproc 2>/dev/null || echo 2)"
printf '%s\0' "${FILES[@]}" \
    | xargs -0 -P"$JOBS" -I{} "$CT" -p "$BUILD_DIR" --quiet --extra-arg=-std=c++17 "{}"
status=$?

if [ "$status" -eq 0 ]; then
    echo "clang-tidy: clean"
else
    echo "clang-tidy: findings above (exit $status)" >&2
fi
exit "$status"
