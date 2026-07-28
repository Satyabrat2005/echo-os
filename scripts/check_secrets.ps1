# ECHO OS - pre-commit / pre-push credential scan (constraint #3), PowerShell port.
#
# Refuses the push if a real API key, OAuth secret, token file, or .env is about to
# enter the repo. Run before every push:  powershell -File scripts/check_secrets.ps1
# Exit 0 = clean, 1 = something suspicious is tracked/staged.
$ErrorActionPreference = 'Stop'

$root = (git rev-parse --show-toplevel).Trim()
Set-Location $root

$staged = git diff --cached --name-only --diff-filter=ACM
if (-not $staged) { $files = git ls-files } else { $files = $staged }

$fail = $false
function Note($msg) { Write-Host "  x $msg"; $script:fail = $true }

# 1. No real .env or token cache may be tracked.
foreach ($f in $files) {
    if (-not $f) { continue }
    if (($f -eq '.env') -or ($f -like '.env.*')) {
        if ($f -ne '.env.example') { Note "tracked env file: $f (should be gitignored)" }
    }
    if ($f -like '*.echo-tokens/*') { Note "tracked token cache: $f" }
}

# 2. Content patterns indicating a real secret in a tracked file.
$patterns = @(
    'AIza[0-9A-Za-z_-]{35}',
    'ya29\.[0-9A-Za-z_-]+',
    '-----BEGIN [A-Z ]*PRIVATE KEY-----',
    'ECHO_[A-Z_]*SECRET=\S{10,}',
    'ECHO_[A-Z_]*KEY=[A-Za-z0-9_-]{16,}',
    '"refresh_token"\s*:\s*"[^"]{20,}"'
)
$skip = @('.env.example', 'scripts/check_secrets.sh', 'scripts/check_secrets.ps1')

foreach ($f in $files) {
    if (-not $f) { continue }
    if ($skip -contains $f) { continue }
    if (-not (Test-Path $f)) { continue }
    $content = Get-Content -Raw -ErrorAction SilentlyContinue $f
    if (-not $content) { continue }
    foreach ($p in $patterns) {
        if ($content -match $p) { Note "possible secret in $f (matched /$p/)" }
    }
}

if ($fail) {
    Write-Host ""
    Write-Host "x check_secrets: potential credentials detected - push blocked."
    Write-Host "  Move secrets into .env (gitignored). See .env.example."
    exit 1
}
Write-Host "ok check_secrets: no credentials detected in tracked/staged files."
exit 0
