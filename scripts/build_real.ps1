<#
.SYNOPSIS
  Configure + build ECHO OS with the full real local-AI stack (all ECHO_WITH_* ON).

.DESCRIPTION
  Step 2 of Phase 4. Wraps the CMake configure line with the paths the real
  engines need, then builds. Run scripts/setup_deps.ps1 first.

  If the build fails on the single llama.cpp KV-clear line (llama_memory_clear /
  llama_kv_self_clear / llama_kv_cache_clear), your installed llama.cpp uses a
  different API revision - re-run with -KvClear self  (or  -KvClear cache). See
  cognitive-core/src/llm.cpp and README "Phase 4".

.PARAMETER Prefix           CMAKE_PREFIX_PATH (where setup_deps.ps1 installed whisper/llama).
.PARAMETER PorcupineRoot    Root of the unpacked Porcupine SDK (-DPORCUPINE_ROOT).
.PARAMETER VcpkgToolchain   Path to vcpkg.cmake, if OpenCV/SDL2 came from vcpkg.
.PARAMETER KvClear          llama.cpp KV-clear API: current (default) | self | cache.
.PARAMETER BuildDir         Build directory (default: build-real).
#>
[CmdletBinding()]
param(
    [string] $Prefix,
    [string] $PorcupineRoot,
    [string] $VcpkgToolchain,
    [ValidateSet("current","self","cache")] [string] $KvClear = "current",
    [string] $BuildDir = "build-real"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Prefix) { $Prefix = Join-Path $repo "third_party/install" }

$cmakeArgs = @(
    "-S", $repo, "-B", (Join-Path $repo $BuildDir),
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DECHO_REAL_AI=ON",
    "-DECHO_LLAMA_KV_CLEAR=$KvClear",
    "-DCMAKE_PREFIX_PATH=$Prefix"
)
if ($PorcupineRoot)  { $cmakeArgs += "-DPORCUPINE_ROOT=$PorcupineRoot" }
if ($VcpkgToolchain) { $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain" }

Write-Host "[build] cmake $($cmakeArgs -join ' ')" -ForegroundColor Cyan
cmake @cmakeArgs
Write-Host "[build] building ..." -ForegroundColor Cyan
cmake --build (Join-Path $repo $BuildDir) --config Release -j

Write-Host ""
Write-Host "[build] done. Runtime DLL note (Windows):" -ForegroundColor Green
Write-Host @"
  Put your toolchain's bin FIRST on PATH, and keep the dependency DLLs
  (SDL2.dll, opencv_*.dll, whisper.dll, llama.dll, ggml*.dll) beside echo-demo.exe
  or on PATH. A stray libstdc++ from an unrelated MinGW (e.g. Git's) ahead of
  yours will crash the exe at startup - that's an ABI/PATH issue, not a bug.

  Run it:   .\scripts\run_demo.ps1
"@ -ForegroundColor Green
