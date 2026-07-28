#!/usr/bin/env bash
# ECHO OS — Phase 3 demo launcher (Linux / macOS).
#
# Builds and starts the whole local pipeline: opens the webcam, listens for the
# "Hey ECHO" wake word, and pops up the HUD overlay.
#
#   ./scripts/run_demo.sh                 # real mode: webcam + mic + models + HUD
#   ./scripts/run_demo.sh --mode stub     # no models needed: type commands, headless HUD
#   ./scripts/run_demo.sh --reconfigure   # wipe the build dir and reconfigure first
#
# REAL MODE prerequisites (see README "Phase 3"): OpenCV, SDL2[/_ttf],
# whisper.cpp, llama.cpp and the Porcupine SDK installed; the model files placed
# under models/ (see models/README.md). Point CMake at any dep that isn't on the
# default search path via env vars, e.g. OpenCV_DIR, SDL2_DIR, whisper_DIR,
# llama_DIR, PORCUPINE_ROOT.
set -euo pipefail

MODE="real"
BUILD_DIR="build-demo"
RECONFIGURE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) MODE="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --reconfigure) RECONFIGURE=1; shift ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CFG=(-DCMAKE_BUILD_TYPE=Release)
if [[ "$MODE" == "real" ]]; then
    CFG+=(-DECHO_REAL_AI=ON)
    for v in OpenCV_DIR SDL2_DIR SDL2_ttf_DIR whisper_DIR llama_DIR PORCUPINE_ROOT; do
        if [[ -n "${!v:-}" ]]; then CFG+=("-D${v}=${!v}"); fi
    done
fi

[[ "$RECONFIGURE" == "1" && -d "$BUILD_DIR" ]] && rm -rf "$BUILD_DIR"

echo "==> Configuring ($MODE mode)..."
cmake -S . -B "$BUILD_DIR" "${CFG[@]}"
echo "==> Building..."
cmake --build "$BUILD_DIR" -j

EXE="$BUILD_DIR/apps-bin/echo-demo"
[[ -x "$EXE" ]] || { echo "echo-demo not found at $EXE" >&2; exit 1; }

echo "==> Launching ECHO demo ($MODE mode). Ctrl+C to stop."
if [[ "$MODE" == "real" ]]; then exec "$EXE" --window
else                              exec "$EXE" --text --headless-hud
fi
