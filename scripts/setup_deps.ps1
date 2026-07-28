<#
.SYNOPSIS
  Phase 4 dependency bring-up for the real ECHO local-AI stack (Windows).

.DESCRIPTION
  Automates the parts of "Step 1 - Install real dependencies" that CAN be
  automated on a laptop:
    * builds and installs whisper.cpp and llama.cpp from source into a local
      prefix (so CMake's find_package(whisper|llama CONFIG) resolves them), and
    * downloads the freely-available models (whisper ASR, Piper voice, OpenCV
      YuNet/SFace face models).

  It deliberately does NOT try to fake the pieces that require a human:
    * OpenCV + SDL2 runtime libs   -> recommended via vcpkg (printed below)
    * the Piper *binary*           -> download from the Piper releases page
    * the LLM .gguf                -> your model choice / license acceptance
    * Porcupine AccessKey + custom "Hey ECHO" .ppn -> Picovoice console (account)

  Every version/commit it installs is recorded in models/INSTALLED_VERSIONS.md so
  future-you has the exact provenance when moving to embedded hardware.

.PARAMETER Prefix
  Install prefix for whisper/llama (passed to CMake as CMAKE_PREFIX_PATH later).
  Default: <repo>/third_party/install

.PARAMETER LlamaGgufUrl
  Optional direct URL to an instruct .gguf; if given, downloaded to models/llm.gguf.

.PARAMETER SkipBuild   Skip building whisper.cpp / llama.cpp.
.PARAMETER SkipModels  Skip model downloads.

.EXAMPLE
  .\scripts\setup_deps.ps1
  .\scripts\setup_deps.ps1 -LlamaGgufUrl "https://.../Llama-3.2-3B-Instruct-Q4_K_M.gguf"
#>
[CmdletBinding()]
param(
    [string] $Prefix,
    [string] $LlamaGgufUrl = "",
    [switch] $SkipBuild,
    [switch] $SkipModels
)

$ErrorActionPreference = "Stop"
$repo   = Split-Path -Parent $PSScriptRoot
$tp     = Join-Path $repo "third_party"
$models = Join-Path $repo "models"
if (-not $Prefix) { $Prefix = Join-Path $tp "install" }
New-Item -ItemType Directory -Force -Path $tp, $models | Out-Null

function Need($name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Required tool '$name' not found on PATH. Install it (or your MinGW/CMake bin dir) and re-run."
    }
}
function Info($m) { Write-Host "[setup] $m" -ForegroundColor Cyan }
function Warn($m) { Write-Host "[setup] $m" -ForegroundColor Yellow }

Need git
Need cmake

# --- Build whisper.cpp + llama.cpp from source, install into $Prefix ----------
function Build-Cmake($name, $url) {
    $src = Join-Path $tp $name
    if (-not (Test-Path $src)) {
        Info "cloning $name ..."
        git clone --depth 1 $url $src | Out-Null
    } else {
        Info "$name already cloned (leaving as-is; delete third_party/$name to refresh)"
    }
    Info "configuring + building $name (Release) ..."
    cmake -S $src -B "$src/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
    cmake --build "$src/build" --config Release -j
    cmake --install "$src/build" --prefix $Prefix
    $commit = (git -C $src rev-parse --short HEAD).Trim()
    return $commit
}

$whisperCommit = "(skipped)"
$llamaCommit   = "(skipped)"
if (-not $SkipBuild) {
    $whisperCommit = Build-Cmake "whisper.cpp" "https://github.com/ggml-org/whisper.cpp.git"
    $llamaCommit   = Build-Cmake "llama.cpp"   "https://github.com/ggml-org/llama.cpp.git"
    Info "whisper.cpp @ $whisperCommit and llama.cpp @ $llamaCommit installed into $Prefix"
} else {
    Warn "-SkipBuild set: not building whisper.cpp / llama.cpp"
}

# --- libcurl (the ECHO_WITH_NETWORK transport for the live third-party APIs) ---
# Phase 6 requires the network branch to build. We install the official curl
# win64-mingw dev package (headers + import lib + DLL) into $Prefix so
# find_package(CURL) resolves it via CMAKE_PREFIX_PATH, exactly like whisper/llama.
# It's a native UCRT build (SChannel TLS, no extra runtime DLLs), so it links and
# runs against the MinGW-w64 ucrt toolchain with no ABI mismatch.
function Provision-Libcurl {
    if (Test-Path (Join-Path $Prefix "include/curl/curl.h")) {
        Info "libcurl already installed in prefix"
        try { return (Get-Content (Join-Path $Prefix "include/curl/curlver.h") |
                      Select-String 'LIBCURL_VERSION "' | ForEach-Object {
                        ($_ -replace '.*LIBCURL_VERSION "([^"]+)".*','$1') } | Select-Object -First 1) }
        catch { return "(installed)" }
    }
    $zip = Join-Path $tp "curl-win64-mingw.zip"
    Info "downloading official curl (win64-mingw) ..."
    Invoke-WebRequest -Uri "https://curl.se/windows/latest.cgi?p=win64-mingw.zip" `
                      -OutFile $zip -UseBasicParsing
    $ex = Join-Path $tp "curl-extract"
    if (Test-Path $ex) { Remove-Item -Recurse -Force $ex }
    Expand-Archive -Path $zip -DestinationPath $ex -Force
    $root = Get-ChildItem -Directory $ex | Where-Object { $_.Name -like "curl-*win64-mingw" } |
            Select-Object -First 1
    if (-not $root) { throw "curl package layout unexpected under $ex" }
    New-Item -ItemType Directory -Force -Path `
        (Join-Path $Prefix "include"), (Join-Path $Prefix "lib"), (Join-Path $Prefix "bin") | Out-Null
    Copy-Item -Recurse -Force (Join-Path $root.FullName "include/curl") (Join-Path $Prefix "include")
    Copy-Item -Force (Join-Path $root.FullName "lib/libcurl.dll.a") (Join-Path $Prefix "lib")
    Copy-Item -Force (Join-Path $root.FullName "lib/libcurl.a")     (Join-Path $Prefix "lib") -ErrorAction SilentlyContinue
    Copy-Item -Force (Join-Path $root.FullName "bin/libcurl-x64.dll") (Join-Path $Prefix "bin")
    # version is the trailing part of the folder name, e.g. curl-8.21.0_5-win64-mingw
    return ($root.Name -replace '^curl-','' -replace '-win64-mingw$','')
}
$curlVer = "(skipped)"
if (-not $SkipBuild) {
    try { $curlVer = Provision-Libcurl; Info "libcurl $curlVer installed into $Prefix" }
    catch { Warn "libcurl provisioning failed ($_); ECHO_WITH_NETWORK will not build until it's installed" }
} else {
    Warn "-SkipBuild set: not provisioning libcurl"
}

# --- Download the freely-available models -------------------------------------
# URLs can drift upstream; a failed download warns rather than aborting the run.
function Fetch($url, $dest) {
    if (Test-Path $dest) { Info "have $(Split-Path -Leaf $dest) already"; return }
    try {
        Info "downloading $(Split-Path -Leaf $dest) ..."
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    } catch {
        Warn "could not fetch $url  ->  get it manually into $dest"
    }
}

if (-not $SkipModels) {
    Fetch "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin" `
          (Join-Path $models "ggml-base.en-q5_1.bin")
    Fetch "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx" `
          (Join-Path $models "en_US-amy-medium.onnx")
    Fetch "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx.json" `
          (Join-Path $models "en_US-amy-medium.onnx.json")
    Fetch "https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx" `
          (Join-Path $models "face_detection_yunet.onnx")
    Fetch "https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx" `
          (Join-Path $models "face_recognition_sface.onnx")
    if ($LlamaGgufUrl) { Fetch $LlamaGgufUrl (Join-Path $models "llm.gguf") }
} else {
    Warn "-SkipModels set: not downloading models"
}

# --- Record provenance (feeds README Step 1 "document exact versions") --------
$cmakeVer = (cmake --version | Select-Object -First 1)
$gccVer   = try { (gcc --version | Select-Object -First 1) } catch { "(gcc not found)" }
$verFile  = Join-Path $models "INSTALLED_VERSIONS.md"
@"
# ECHO OS - installed dependency versions (Phase 4 bring-up)

Generated by ``scripts/setup_deps.ps1`` on $(Get-Date -Format u).
Fill in the rows this script can't detect (OpenCV, SDL2, Piper, Porcupine) by hand.

| Dependency  | Version / commit            | How installed                    |
|-------------|-----------------------------|----------------------------------|
| Toolchain   | $gccVer | MinGW-w64 on PATH |
| CMake       | $cmakeVer | on PATH |
| whisper.cpp | $whisperCommit | built from source -> $Prefix |
| llama.cpp   | $llamaCommit | built from source -> $Prefix |
| libcurl     | $curlVer | official curl win64-mingw -> $Prefix |
| OpenCV      | _fill in_ (e.g. vcpkg 4.x)  | _vcpkg / prebuilt_ |
| SDL2        | _fill in_                   | _vcpkg / prebuilt_ |
| SDL2_ttf    | _fill in_                   | _vcpkg / prebuilt_ |
| Piper       | _fill in_ (release tag)     | _binary from releases_ |
| Porcupine   | _fill in_ (SDK version)     | _Picovoice console + SDK_ |
| LLM gguf    | _fill in_ (model + quant)   | _model card_ |
"@ | Set-Content -Encoding utf8 $verFile
Info "wrote $verFile"

# --- What's left for the human ------------------------------------------------
Write-Host ""
Write-Host "==================== MANUAL STEPS STILL REQUIRED ====================" -ForegroundColor Green
Write-Host @"
1. OpenCV + SDL2 + SDL2_ttf runtime libs (recommended: vcpkg):
     vcpkg install opencv4 sdl2 sdl2-ttf
   then configure ECHO with  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

2. Piper binary (self-contained, no link dep):
     download the Windows build from https://github.com/rhasspy/piper/releases
     put piper.exe on PATH (or set ECHO_PIPER_BIN to its full path)

3. LLM .gguf (your choice; README suggests Llama-3.2-3B-Instruct Q4_K_M):
     download the .gguf from its model card -> models/llm.gguf
     (or re-run this script with -LlamaGgufUrl "<direct url>")

4. Porcupine (account-gated; only YOU can do this):
     - create a free AccessKey at https://console.picovoice.ai
     - train a custom "Hey ECHO" wake word, download hey-echo.ppn + porcupine_params.pv
     - unpack the Porcupine SDK, note its root for -DPORCUPINE_ROOT=
     - put: models/hey-echo.ppn, models/porcupine_params.pv,
            models/porcupine_access_key.txt (the key on one line)

5. Enrolled faces: drop a clear photo per person in models/faces/<name>.jpg

Then build everything ON:
     .\scripts\build_real.ps1 -Prefix "$Prefix" -PorcupineRoot "<sdk>" [-VcpkgToolchain "<file>"]

And record OpenCV/SDL2/Piper/Porcupine/LLM versions in:
     models/INSTALLED_VERSIONS.md
"@ -ForegroundColor Green
