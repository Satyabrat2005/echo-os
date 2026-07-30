#!/usr/bin/env bash
# ECHO OS — fetch + verify the free models/assets for the REAL-engine tests.
#
# Downloads exactly the third-party model files and test images the real-engine
# verification needs (whisper.cpp tiny.en, OpenCV YuNet+SFace, a Piper voice + the
# piper binary, two public-domain face photos, and — Phase 12 — a tiny quantized
# instruct LLM), and VERIFIES each against a pinned SHA-256 before it is used.
# Running unverified third-party binary model files is a real supply-chain risk,
# not a hypothetical one — so a checksum mismatch is a hard error here, never a
# warning.
#
# It is idempotent: a file already present with the right checksum is left alone,
# so it is safe to point at a CI cache directory (only missing/changed files are
# re-downloaded).
#
# Usage:   scripts/fetch_real_engine_assets.sh [DEST_DIR] [SUBSTRING_FILTER]
#   DEST_DIR          defaults to ./.real-engine-assets
#   SUBSTRING_FILTER  optional: fetch only assets whose relative path contains this
#                     substring (e.g. "llm.gguf" for the LLM-only CI job). Empty =
#                     every asset. The pinned URL + checksum list is unchanged; the
#                     filter only narrows what a given job downloads.
#
# Layout produced under DEST_DIR:
#   ggml-tiny.en.bin                      whisper ASR model
#   llm.gguf                              tiny instruct LLM (llama.cpp, Phase 12)
#   face_detection_yunet.onnx             OpenCV YuNet detector
#   face_recognition_sface.onnx           OpenCV SFace recognizer
#   voices/en_US-amy-medium.onnx[.json]   Piper voice (+ its config)
#   piper/piper                           Piper binary (extracted from the release)
#   faces/enrolled/grace_hopper.jpg       the one enrolled identity (US Navy, PD)
#   faces/astronaut.png                   an UNKNOWN identity (NASA, PD)
#
# The two face images are public-domain photographs of public figures, and classic
# computer-vision test images — no private individual, and (per tests/fixtures/
# README.md) they are fetched here rather than committed to the tree.
set -euo pipefail

DEST="${1:-.real-engine-assets}"
FILTER="${2:-}"
mkdir -p "$DEST"

# SFace (2021dec) lives at the recent pinned commit; YuNet is pinned to an OLDER
# commit on purpose — the 2023mar YuNet needs OpenCV >=4.8, but Ubuntu's OpenCV is
# 4.6, so we use the 4.6-compatible 2022mar model (2023mar trips an eltwise-layer
# shape assertion in 4.6's DNN backend).
OPENCV_ZOO_SFACE_COMMIT="47534e27c9851bb1128ccc0102f1145e27f23f98"
OPENCV_ZOO_YUNET_COMMIT="7e062e54cf5410c09b795ff71b4a255e58498c79"

# The Phase 12 reasoning LLM: Qwen2.5-0.5B-Instruct, Q5_K_M quantization, from
# Qwen's own GGUF repo. Chosen the same way Phase 11 chose whisper tiny.en — the
# smallest genuinely instruction-following model that runs one short CPU inference
# in CI in seconds, needs no account/key to download, and carries a redistributable
# license (Apache-2.0). At 0.5B params it is NOT production-quality reasoning; it
# exists only to exercise the REAL llama.cpp path, the route-tag parser, and the
# safe-mode gate against a real model's real output. See MANIFEST.md.
read -r -d '' ASSETS <<EOF || true
ggml-tiny.en.bin|921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f|https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin
llm.gguf|041474553fcabfc2a2d67903f9d2c2e50bd92528e670da4f33b5d0ce6e59fd55|https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q5_k_m.gguf
face_detection_yunet.onnx|50ef07f702a31741ca46a4c0d947773b64143b9362780237bf0d427d6c79bab7|https://github.com/opencv/opencv_zoo/raw/${OPENCV_ZOO_YUNET_COMMIT}/models/face_detection_yunet/face_detection_yunet_2022mar.onnx
face_recognition_sface.onnx|0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79|https://github.com/opencv/opencv_zoo/raw/${OPENCV_ZOO_SFACE_COMMIT}/models/face_recognition_sface/face_recognition_sface_2021dec.onnx
voices/en_US-amy-medium.onnx|b3a6e47b57b8c7fbe6a0ce2518161a50f59a9cdd8a50835c02cb02bdd6206c18|https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx
voices/en_US-amy-medium.onnx.json|95a23eb4d42909d38df73bb9ac7f45f597dbfcde2d1bf9526fdeaf5466977d77|https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx.json
piper_linux_x86_64.tar.gz|a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992|https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz
faces/enrolled/grace_hopper.jpg|a8ca6d734765703b09728ab47fe59f473d93ae3967fc24c7c0288c3c7adb7130|https://raw.githubusercontent.com/matplotlib/matplotlib/v3.8.4/lib/matplotlib/mpl-data/sample_data/grace_hopper.jpg
faces/astronaut.png|88431cd9653ccd539741b555fb0a46b61558b301d4110412b5bc28b5e3ea6cb5|https://raw.githubusercontent.com/scikit-image/scikit-image/v0.19.3/skimage/data/astronaut.png
EOF

sha_of() { sha256sum "$1" | awk '{print $1}'; }

fetch_one() {
    local rel="$1" want="$2" url="$3"
    local out="$DEST/$rel"
    mkdir -p "$(dirname "$out")"
    if [[ -f "$out" ]] && [[ "$(sha_of "$out")" == "$want" ]]; then
        echo "  ok (cached): $rel"
        return 0
    fi
    echo "  fetching: $rel"
    curl -fSL --retry 3 --retry-delay 2 -o "$out" "$url"
    local got
    got="$(sha_of "$out")"
    if [[ "$got" != "$want" ]]; then
        echo "CHECKSUM MISMATCH for $rel" >&2
        echo "  expected: $want" >&2
        echo "  actual:   $got"  >&2
        rm -f "$out"
        exit 1
    fi
    echo "  verified: $rel"
}

echo "Fetching real-engine assets into: $DEST${FILTER:+ (filter: *$FILTER*)}"
while IFS='|' read -r rel want url; do
    [[ -z "$rel" ]] && continue
    if [[ -n "$FILTER" && "$rel" != *"$FILTER"* ]]; then continue; fi
    fetch_one "$rel" "$want" "$url"
done <<< "$ASSETS"

# Extract the Piper binary from its verified tarball (idempotent) — but only when
# the tarball is actually in scope. A filtered fetch (e.g. the LLM-only CI job,
# which pulls just llm.gguf) legitimately skips Piper, and must not fail here.
if [[ -f "$DEST/piper_linux_x86_64.tar.gz" ]]; then
    if [[ ! -x "$DEST/piper/piper" ]]; then
        echo "  extracting: piper binary"
        tar -xzf "$DEST/piper_linux_x86_64.tar.gz" -C "$DEST"
    fi
    test -x "$DEST/piper/piper" || { echo "piper binary missing after extract" >&2; exit 1; }
fi

echo "All requested real-engine assets present and verified."
