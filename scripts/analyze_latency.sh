#!/usr/bin/env bash
# Summarize latency_log.csv into the README's measured-latency table (Phase 4,
# Step 4). Mirror of scripts/analyze_latency.ps1. Invents nothing; warns if the
# sample is smaller than the required number of turns.
#
# Usage: scripts/analyze_latency.sh [csv] [min_turns]
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
csv="${1:-$repo/latency_log.csv}"
min_turns="${2:-20}"
[ -f "$csv" ] || { echo "No CSV at $csv - run the real demo first."; exit 1; }

awk -F, -v mt="$min_turns" '
NR==1 { next }                                   # header
{ n++;
  for (i=3;i<=6;i++){ v=$i+0; s[i]+=v;
    if(n==1||v<mn[i])mn[i]=v; if(n==1||v>mx[i])mx[i]=v } }
END {
  if(n==0){ print "no data rows"; exit 1 }
  split("_ _ Perception(wake+ASR) Cognitive(LLM+gate) Voice(TTS-start) End-to-end",name," ")
  split("_ _ 45 50 18 120",tgt," ")
  printf "\nLatency over %d turn(s) from %s\n", n, "'"$csv"'"
  if(n<mt) printf "  WARNING: only %d turn(s) - task asks for >= %d for an honest number.\n", n, mt
  print  "\n| Stage | Target | Min | Avg | Max |"
  print  "|-------|-------:|----:|----:|----:|"
  for(i=3;i<=6;i++) printf "| %s | %s ms | %.1f | %.1f | %.1f |\n", name[i], tgt[i], mn[i], s[i]/n, mx[i]
  # dominant of perception/cognitive/voice by avg
  di=3; for(i=4;i<=5;i++) if(s[i]/n > s[di]/n) di=i
  e2e=s[6]/n; verdict=(e2e<=120)?"within":"OVER"
  printf "\nGap analysis: avg end-to-end %.1f ms - %s the 120 ms target; dominant stage = %s.\n", e2e, verdict, name[di]
  if(e2e>120) print "  Record the real number in README - do NOT adjust the budget."
}' "$csv"
