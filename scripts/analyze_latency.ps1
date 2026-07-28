<#
.SYNOPSIS
  Summarize latency_log.csv into the README's measured-latency table (Phase 4, Step 4).

.DESCRIPTION
  Reads the CSV the demo writes (one row per real turn) and prints, per stage,
  min / avg / max in Markdown ready to paste into README's "Measured latency"
  table, plus the gap-analysis line (measured end-to-end vs. the 120 ms target
  and the dominant stage). It does no rounding-for-flattery and invents nothing:
  if you ran fewer than the required turns, it says so rather than pretending.

.PARAMETER Csv       Path to the CSV (default: <repo>/latency_log.csv).
.PARAMETER MinTurns  Turns required for an honest sample (default: 20, per the task).

.EXAMPLE
  .\scripts\analyze_latency.ps1
#>
[CmdletBinding()]
param(
    [string] $Csv,
    [int]    $MinTurns = 20
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Csv) { $Csv = Join-Path $repo "latency_log.csv" }
if (-not (Test-Path $Csv)) { throw "No CSV at $Csv - run the real demo first (scripts/run_demo.ps1)." }

$rows = @(Import-Csv $Csv)
if ($rows.Count -eq 0) { throw "$Csv has no data rows." }

# Stage -> (csv column, target ms) in end-to-end order.
$stages = [ordered]@{
    "Perception (wake + ASR)"  = @("perception_ms", 45)
    "Cognitive (LLM + gate)"   = @("cognitive_ms",  50)
    "Voice output (TTS start)" = @("voice_ms",      18)
    "End to end"               = @("total_ms",     120)
}

function Stat($col) {
    $vals = $rows | ForEach-Object { [double]$_.$col }
    $m = $vals | Measure-Object -Minimum -Maximum -Average
    [pscustomobject]@{ Min=$m.Minimum; Avg=$m.Average; Max=$m.Maximum }
}

Write-Host ""
Write-Host "Latency over $($rows.Count) turn(s) from $Csv" -ForegroundColor Cyan
if ($rows.Count -lt $MinTurns) {
    Write-Host "  WARNING: only $($rows.Count) turn(s) - the task asks for >= $MinTurns for an honest number." -ForegroundColor Yellow
}
Write-Host ""
Write-Host "| Stage | Target | Min | Avg | Max |"
Write-Host "|-------|-------:|----:|----:|----:|"
$stageAvgs = @{}
foreach ($name in $stages.Keys) {
    $col, $target = $stages[$name]
    $s = Stat $col
    if ($name -ne "End to end") { $stageAvgs[$name] = $s.Avg }
    $label = if ($name -eq "End to end") { "**End to end**" } else { $name }
    $tgt   = if ($name -eq "End to end") { "**$target ms**" } else { "$target ms" }
    "| {0} | {1} | {2:N1} | {3:N1} | {4:N1} |" -f $label, $tgt, $s.Min, $s.Avg, $s.Max | Write-Host
}

$e2e      = Stat "total_ms"
$dominant = ($stageAvgs.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key
$verdict  = if ($e2e.Avg -le 120) { "within" } else { "OVER" }
Write-Host ""
Write-Host ("Gap analysis: avg end-to-end {0:N1} ms - {1} the 120 ms target; dominant stage = {2}." `
    -f $e2e.Avg, $verdict, $dominant) -ForegroundColor Green
if ($e2e.Avg -gt 120) {
    Write-Host "  Record the real number in README - do NOT adjust the budget. See the forward-looking options there." -ForegroundColor Green
}
