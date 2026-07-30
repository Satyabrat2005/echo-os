# ECHO OS — real-engine asset manifest

Every third-party model / binary / image the **real-engine CI jobs** download is
pinned here: exact URL, exact SHA-256, size, license, and *why* it was chosen.
None of these files is committed to the repo (they are large and carry their own
licenses); they are fetched and **checksum-verified at run time** by
[`scripts/fetch_real_engine_assets.sh`](scripts/fetch_real_engine_assets.sh),
which is the single source of truth for the URLs + checksums — this file is the
human-readable companion to it. A checksum mismatch is a hard error, never a
warning: running an unverified third-party binary model is a real supply-chain
risk, so the fetch fails closed.

The fetch-script and manifest checksums must always agree; the CI asset caches are
keyed on `hashFiles('scripts/fetch_real_engine_assets.sh')`, so the cache is
invalidated exactly when a pinned asset changes ("cache by checksum").

## Discipline (applies to every entry)

1. **Checksum-verified before use** — pinned SHA-256, hard-fail on mismatch.
2. **No account, key, or authentication** to download — every URL is anonymously
   fetchable.
3. **Redistributable / freely fetchable license**, noted per entry.
4. **Fetched, not committed** — kept out of the tree; live only in the ephemeral
   CI/checkout dir.

---

## Phase 12 — reasoning LLM (llama.cpp)

| | |
|---|---|
| **File** | `llm.gguf` |
| **Model** | Qwen2.5-0.5B-Instruct, `Q5_K_M` quantization |
| **URL** | https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q5_k_m.gguf |
| **SHA-256** | `041474553fcabfc2a2d67903f9d2c2e50bd92528e670da4f33b5d0ce6e59fd55` |
| **Size** | 522,186,592 bytes (~498 MiB) |
| **License** | Apache-2.0 (redistributable; no account/key to download) |
| **Used by** | `tests/real_llm_test.cpp` (the `real-llm` CI job) via `ECHO_LLAMA_MODEL` |

**Why this model.** Phase 11 explicitly ruled real-LLM CI *out of scope* as
impractical — but that call was written for a full-size 3B–4B instruct model.
Phase 12 narrows the ambition instead of abandoning it: pick a genuinely **tiny**
instruction-following model that can run one short CPU inference in CI in seconds.

- **Size / runtime.** 0.5B parameters. Q5_K_M is ~498 MiB — a couple hundred MB of
  the weight is Qwen's large (151k-token) vocab embedding, which no quantization
  shrinks; the transformer blocks themselves are tiny, so a ~96-token greedy
  generation runs at ~70 tok/s on a CI CPU (one prompt ≈ 1–2 s). Same spirit as
  whisper `tiny.en` in Phase 11.
- **Instruction-following, verified.** A 0.5B model is only useful here if it
  *actually* follows ECHO's `[route:<app>]` convention. It does: with ECHO's real
  system prompt and greedy (deterministic) sampling, `"play some jazz music"` →
  `[route:media] [track:jazz]` and `"What is two plus two?"` → `Two plus two is
  four.` (no tag). Q5_K_M was chosen over the smaller Q-levels because it holds
  that formatting reliably at close to the smallest footprint.
- **License / access.** Apache-2.0, downloadable anonymously from Qwen's own HF
  GGUF repo — matches every other asset here (no key, redistributable).

**What it does NOT establish (kept honest, see `docs/STATE.md`).** This is *not*
production-quality reasoning. It exists to prove the real llama.cpp *integration*
(load, generate, KV-clear), the route-tag parser against a real model's real
output format, and that the safe-mode gate behaves correctly with a real LLM
wired in. Production reasoning quality depends on the larger model used in the
real deployment, which this test does not validate.

---

## Phase 11 — perception + voice engines

### whisper.cpp ASR model

| | |
|---|---|
| **File** | `ggml-tiny.en.bin` |
| **URL** | https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin |
| **SHA-256** | `921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f` |
| **License** | MIT (whisper.cpp model card) |
| **Used by** | `tests/real_asr_test.cpp` via `ECHO_WHISPER_MODEL` |
| **Why** | Smallest whisper model; transcribes the clean fixture/synthesized clips in CI without a GPU. |

### OpenCV YuNet face detector

| | |
|---|---|
| **File** | `face_detection_yunet.onnx` (`face_detection_yunet_2022mar.onnx`) |
| **URL** | https://github.com/opencv/opencv_zoo/raw/7e062e54cf5410c09b795ff71b4a255e58498c79/models/face_detection_yunet/face_detection_yunet_2022mar.onnx |
| **SHA-256** | `50ef07f702a31741ca46a4c0d947773b64143b9362780237bf0d427d6c79bab7` |
| **License** | MIT (opencv_zoo) |
| **Used by** | `tests/real_vision_test.cpp` via `ECHO_FACE_DETECT_MODEL` |
| **Why** | The **2022mar** revision is pinned on purpose: 2023mar needs OpenCV ≥4.8, but Ubuntu's OpenCV is 4.6, which trips an eltwise-layer shape assertion on the newer model. |

### OpenCV SFace face recognizer

| | |
|---|---|
| **File** | `face_recognition_sface.onnx` (`face_recognition_sface_2021dec.onnx`) |
| **URL** | https://github.com/opencv/opencv_zoo/raw/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_recognition_sface/face_recognition_sface_2021dec.onnx |
| **SHA-256** | `0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79` |
| **License** | MIT (opencv_zoo) |
| **Used by** | `tests/real_vision_test.cpp` via `ECHO_FACE_RECOG_MODEL` |
| **Why** | 2021dec is OpenCV-4.6-compatible; produces the face embeddings the enrolled/unknown match test compares. |

### Piper TTS voice (+ config)

| | |
|---|---|
| **Files** | `voices/en_US-amy-medium.onnx`, `voices/en_US-amy-medium.onnx.json` |
| **URLs** | https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx (+ `.json`) |
| **SHA-256** | `b3a6e47b57b8c7fbe6a0ce2518161a50f59a9cdd8a50835c02cb02bdd6206c18` (onnx) / `95a23eb4d42909d38df73bb9ac7f45f597dbfcde2d1bf9526fdeaf5466977d77` (json) |
| **License** | MIT (piper-voices) |
| **Used by** | `tests/real_tts_test.cpp` via `ECHO_PIPER_VOICE`; also synthesizes the ASR speech clip |
| **Why** | A small, license-clean US-English voice; lets CI *generate* the clean speech clip rather than commit audio. |

### Piper binary

| | |
|---|---|
| **File** | `piper_linux_x86_64.tar.gz` → `piper/piper` |
| **URL** | https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz |
| **SHA-256** | `a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992` |
| **License** | MIT (piper) |
| **Used by** | ASR-clip synthesis step in the `real-engines` CI job |
| **Why** | Pinned release tarball so the synthesized clip is reproducible run-to-run. |

### Face fixtures (public-domain photographs)

| | |
|---|---|
| **Files** | `faces/enrolled/grace_hopper.jpg`, `faces/astronaut.png` |
| **URLs** | https://raw.githubusercontent.com/matplotlib/matplotlib/v3.8.4/lib/matplotlib/mpl-data/sample_data/grace_hopper.jpg · https://raw.githubusercontent.com/scikit-image/scikit-image/v0.19.3/skimage/data/astronaut.png |
| **SHA-256** | `a8ca6d734765703b09728ab47fe59f473d93ae3967fc24c7c0288c3c7adb7130` · `88431cd9653ccd539741b555fb0a46b61558b301d4110412b5bc28b5e3ea6cb5` |
| **License** | Public domain (US Navy / NASA); classic CV test images |
| **Used by** | `tests/real_vision_test.cpp` (enrolled vs. unknown identity) |
| **Why** | Public figures, public-domain, no private individual — real faces kept out of the tree on purpose. |
