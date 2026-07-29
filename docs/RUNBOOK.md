# ECHO OS — Runbook: how to actually run this for real

The full real-run setup is scattered across the README's Phase 3, 4, 5, and 6
sections because it accreted over nine phases. This is the **consolidated, linear
checklist** — dependencies, accounts, and scripts in the order you actually run
them — so whoever does the bring-up doesn't have to reconstruct it from history.

Three levels of "running it", smallest to largest. Do them in order; each builds
on the last.

- **Level 0 — Stub build.** Zero dependencies, proves the tree compiles and the
  logic is sound. Do this first, always.
- **Level 1 — Real local AI.** Real wake-word / ASR / LLM / TTS / vision on a
  laptop with a webcam and mic. No network, no accounts except Porcupine.
- **Level 2 — Real third-party APIs.** Live Spotify / Gmail / Search / YouTube
  behind the apps layer. Needs developer credentials and the network build.

Platform note: commands are shown for both PowerShell (`.ps1`, this dev laptop)
and bash (`.sh`). Windows/MinGW users must heed the **runtime-DLL note** at the
bottom.

---

## Level 0 — Stub build (5 minutes, no dependencies)

**Requires:** CMake ≥ 3.16 and any C++17 compiler. Nothing else.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the reference binary (boots the runtime, runs one tick of the core loop
through the stub pipeline, shuts down cleanly):

```bash
./build/boot/echo-os          # build/boot/echo-os.exe on Windows/MinGW
```

Run the apps layer standalone (8 mock apps as real supervised processes):

```bash
./build/apps-bin/echo-apps                 # supervised processes
./build/apps-bin/echo-apps --in-process    # apps in the host process
./build/apps-bin/echo-apps --window        # draw the simulated HUD band
```

✅ **Gate:** all four CTest suites green (`echo-smoke`, `echo-appkit-tests`,
`echo-apps-smoke`, `echo-nlu-routing`). If this fails, stop — nothing above will
work until it passes.

---

## Level 1 — Real local AI on a laptop

Goal: `webcam + mic in → "Hey ECHO" → speech-to-text → {face recognition | app
routing | LLM reasoning behind the safe-mode gate} → spoken reply + HUD subtitle`.
Everything runs on-device; nothing touches the network.

### 1a. Install the engine libraries and download models

```powershell
# Windows (PowerShell):
.\scripts\setup_deps.ps1
```
```bash
# Linux / macOS:
./scripts/setup_deps.sh
```

`setup_deps` builds **whisper.cpp** + **llama.cpp**, downloads the free models,
and writes `models/INSTALLED_VERSIONS.md`. It then **prints manual steps** for the
pieces it can't automate — do all of them:

| Piece | Where to get it | Put it in `models/` as |
|-------|-----------------|------------------------|
| Whisper model | `whisper.cpp` repo: `download-ggml-model.sh base.en-q5_1` | `ggml-base.en-q5_1.bin` |
| LLM (instruct GGUF) | model card, e.g. Llama-3.2-3B-Instruct **Q4_K_M** | `llm.gguf` |
| Piper voice | Piper voices release: `en_US-amy-medium.onnx` **and** `.onnx.json` | both files |
| OpenCV face models | OpenCV Zoo: YuNet + SFace | `face_detection_yunet.onnx`, `face_recognition_sface.onnx` |
| Enrolled faces (demo) | one photo per person, named after them | `models/faces/grace.jpg` → "That's Grace." |
| OpenCV / SDL2 runtime libs | vcpkg (as `setup_deps` prints) | on the library search path |

### 1b. Register the ONE account this level needs — Porcupine

The wake word is the **only** account-gated piece here. In the
[Picovoice console](https://console.picovoice.ai/): get a **free AccessKey** and
train a custom **"Hey ECHO"** keyword.

- Save the AccessKey to `models/porcupine_access_key.txt` (or `$PV_ACCESS_KEY`).
- Download `hey-echo.ppn` + `porcupine_params.pv` into `models/`.

The key is validated **offline** — no audio or request ever leaves the device at
runtime. **Never commit the key** (it's gitignored; the secret scan blocks it).

### 1c. Build with the real engines on

```powershell
.\scripts\build_real.ps1 -PorcupineRoot "<unpacked porcupine sdk>"
```

Or drive CMake directly — all engines, or à la carte:

```bash
cmake -S . -B build -DECHO_REAL_AI=ON                              # everything
cmake -S . -B build -DECHO_WITH_LLAMA=ON -DECHO_WITH_PIPER=ON -DECHO_WITH_SDL=ON   # subset
cmake --build build
```

Point CMake at any off-path dependency with `-DOpenCV_DIR=…`, `-DSDL2_DIR=…`,
`-Dwhisper_DIR=…`, `-Dllama_DIR=…`, `-DPORCUPINE_ROOT=…`.

> **llama.cpp API drift.** If the build fails on the one KV-cache-clear line in
> [`llm.cpp`](../cognitive-core/src/llm.cpp), your llama.cpp is a different API
> revision. Reconfigure with `-DECHO_LLAMA_KV_CLEAR=self` (early–mid 2025) or
> `=cache` (pre-2025). This is the single call most likely to move between
> versions.

### 1d. Run the demo

```powershell
.\scripts\run_demo.ps1                 # real: webcam + mic + wake word + HUD
.\scripts\run_demo.ps1 -Mode stub      # no models: type commands, headless HUD
```
```bash
./scripts/run_demo.sh                   # real mode
./scripts/run_demo.sh --mode stub       # stub mode
```

Then confirm each item in the README's **Manual test flow**: wake + reason (and
watch it fall to safe-mode rather than guess on something unknowable); face
recognition on an enrolled person; app routing ("play some music" → media app).

### 1e. Measure real latency (the honest number)

Each turn's timings are written to `latency_log.csv`. Run **≥20 real end-to-end
turns**, then:

```powershell
.\scripts\analyze_latency.ps1          # min/avg/max table + gap-analysis line
```
```bash
./scripts/analyze_latency.sh
```

Paste the output into the README latency table. It **warns if you logged fewer
than 20 turns** rather than pretend. Expect the real number to **exceed 120 ms**
on a laptop CPU — record it as measured; do not tune the budget to fit.

✅ **Gate for the project's Phase 4:** ≥20 measured turns recorded, the demo runs
end-to-end on real hardware, and one **non-founder** has used it with no
instructions beyond *"say 'Hey ECHO' and talk to it"* — with every failure mode
logged in the README's Known Issues table.

---

## Level 2 — Real third-party APIs

Replaces the mock Spotify / Gmail / Search / YouTube backends with live ones,
behind the exact same interfaces. Needs **both** a `--real` runtime flag **and** a
network-compiled build.

### 2a. Register the four developer apps and fill `.env`

```bash
cp .env.example .env      # .env is gitignored — NEVER commit it
```

| Service | Register at | Key variables | Scopes / notes |
|---------|-------------|---------------|----------------|
| **Spotify** | [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) | `ECHO_SPOTIFY_CLIENT_ID/SECRET/REDIRECT_URI` | add the redirect URI to app settings verbatim |
| **Gmail** | Google Cloud Console → enable Gmail API → OAuth client (Desktop) | `ECHO_GMAIL_CLIENT_ID/SECRET` | request **only** `gmail.readonly` + `gmail.send` |
| **Google Search** | Google Cloud → Custom Search API + a Programmable Search Engine | `ECHO_GOOGLE_SEARCH_KEY/CX` | `CX` is the search-engine id |
| **YouTube** | same Cloud project → YouTube Data API v3 | `ECHO_YOUTUBE_API_KEY` | falls back to the search key if unset |

Record in `.env.example`'s "Registered under" lines **which account owns each
app** — this matters later for company-vs-personal credential ownership.

### 2b. One-time OAuth authorize (Spotify + Gmail)

Both use the **Authorization Code flow with a loopback redirect**
(`http://127.0.0.1:8888/callback`). Once per service: open the authorize URL the
backend builds, approve in the browser, copy the `code` from the loopback
redirect, exchange it once for tokens. The token store caches them under
`ECHO_TOKEN_DIR` (`.echo-tokens/`, gitignored); the cached refresh token drives
silent renewals thereafter.

> This one-time consent is currently a **manual browser paste** — a scripted
> `scripts/authorize.*` helper is still a TODO (see [STATE.md](STATE.md) gap #6).

### 2c. Build with the network transport and run `--real`

```bash
# Network build (needs libcurl installed — setup_deps.ps1 provisions it):
cmake -S apps -B build-apps -G "MinGW Makefiles" -DECHO_WITH_NETWORK=ON
cmake --build build-apps

./build-apps/apps-bin/echo-apps --real
```

Without `-DECHO_WITH_NETWORK=ON`, `--real` degrades cleanly: the HTTP client is
null and every real backend reports `Unavailable` (it degrades, it does not lie).

### 2d. Confirm the live flow + the confirm-before-send gate

Run the README's **real credentialed test flow**: real playback by voice; a real
unread email read aloud; a real search summarized (not dumped); and — critically —
**confirm-before-send**: *"reply saying …"* stages and speaks the message back,
and only an explicit **"send"/"confirm"** actually sends. Verify "cancel" (and any
unrelated word) sends nothing. Paste evidence into the README table.

✅ **Gate for the project's Phase 5/6 Part B:** each of the four live tests run
once with evidence, and the Known Issues (real APIs) table annotated with what you
actually observed.

---

## Secret hygiene (every level, every push)

`.env` and `.echo-tokens/` are gitignored; only `.env.example` (no real values)
is tracked. Run the scan before every push — CI runs it too, but catch it locally
first:

```bash
bash scripts/check_secrets.sh
```
```powershell
powershell -File scripts/check_secrets.ps1
```

Wire it as a pre-commit hook:

```bash
ln -sf ../../scripts/check_secrets.sh .git/hooks/pre-commit
```

---

## Runtime-DLL note (Windows / MinGW) — read this if the binary crashes at startup

A MinGW-built exe crashes with SIGSEGV inside `libstdc++-6.dll` if a **mismatched
runtime** (e.g. Git's bundled MinGW at `C:\Program Files\Git\mingw64\bin`) is
ahead of your toolchain on `PATH`. This is an ABI mismatch, **not a bug**.

**Fix:** put your build toolchain's `bin` **first** on `PATH` before running any
built binary, and put the dependency DLLs (`SDL2.dll`, OpenCV, `whisper.dll`,
`llama.dll`, Piper, `libcurl-x64.dll`) beside the exe or on `PATH`.

```powershell
$env:PATH = "C:\path\to\your\mingw64\bin;" + $env:PATH
```
