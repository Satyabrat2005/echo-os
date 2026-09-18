# ECHO OS — Phase 3 demo launcher (Windows / PowerShell).
#
# Builds and starts the whole local pipeline on the laptop: opens the webcam,
# starts listening for the "Hey ECHO" wake word, and pops up the HUD overlay.
#
#   .\scripts\run_demo.ps1                 # real mode: webcam + mic + models + HUD
#   .\scripts\run_demo.ps1 -Mode stub      # no models needed: type commands, headless HUD
#   .\scripts\run_demo.ps1 -Reconfigure    # wipe the build dir and reconfigure first
#   .\scripts\run_demo.ps1 -Kiosk          # real mode, fullscreen "ECHO OS" shell, no console
#
# REAL MODE prerequisites (see README "Phase 3"):
#   * the libraries installed (OpenCV, SDL2[/_ttf], whisper.cpp, llama.cpp,
#     Porcupine SDK) and, for llama.cpp+piper, on PATH so their DLLs load;
#   * the model files placed under models\  (see models\README.md);
#   * point CMake at each dep via an environment variable before running, e.g.
#       $env:OpenCV_DIR   = 'C:\opencv\build'
#       $env:SDL2_DIR     = 'C:\SDL2\cmake'
#       $env:whisper_DIR  = 'C:\whisper.cpp\build'
#       $env:llama_DIR    = 'C:\llama.cpp\build'
#       $env:PORCUPINE_ROOT = 'C:\porcupine'
param(
    [ValidateSet('real','stub')] [string]$Mode = 'real',
    [string]$BuildDir = 'build-demo',
    [switch]$Reconfigure,
    [switch]$Kiosk
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot   # the echo-os/ directory
Push-Location $root
try {
    if ($Kiosk -and $Mode -eq 'stub') {
        throw "kiosk mode requires -Mode real (stub mode is --text, which is incompatible with a hidden console)"
    }

    $cfg = @('-DCMAKE_BUILD_TYPE=Release')
    if ($Mode -eq 'real') {
        $cfg += '-DECHO_REAL_AI=ON'
        foreach ($v in 'OpenCV_DIR','SDL2_DIR','SDL2_ttf_DIR','whisper_DIR','llama_DIR','PORCUPINE_ROOT') {
            if (Test-Path "env:$v") { $cfg += "-D$v=$((Get-Item "env:$v").Value)" }
        }
    }

    if ($Reconfigure -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }

    Write-Host "==> Configuring ($Mode mode)..." -ForegroundColor Cyan
    cmake -S . -B $BuildDir @cfg
    Write-Host "==> Building..." -ForegroundColor Cyan
    cmake --build $BuildDir -j

    $exe = Join-Path $BuildDir 'apps-bin\echo-demo.exe'
    if (-not (Test-Path $exe)) { throw "echo-demo not found at $exe" }

    if ($Kiosk) {
        $outLog = Join-Path $root 'echo-kiosk.out.log'
        $errLog = Join-Path $root 'echo-kiosk.err.log'
        Write-Host "==> Launching ECHO OS kiosk shell (detached, hidden console)." -ForegroundColor Green
        Start-Process -FilePath $exe -ArgumentList '--kiosk' -WindowStyle Hidden `
            -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        Write-Host "Fullscreen shell launching. Press Esc inside it to quit." -ForegroundColor Green
        Write-Host "Logs: $outLog / $errLog" -ForegroundColor DarkGray
    } elseif ($Mode -eq 'real') {
        Write-Host "==> Launching ECHO demo ($Mode mode). Ctrl+C to stop." -ForegroundColor Green
        & $exe --window
    } else {
        Write-Host "==> Launching ECHO demo ($Mode mode). Ctrl+C to stop." -ForegroundColor Green
        & $exe --text --headless-hud
    }
}
finally { Pop-Location }
