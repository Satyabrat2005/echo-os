#!/usr/bin/env python3
"""
reverify.py - Re-run the website check over an existing leads CSV using the
current classifier, drop anything that turns out to be healthy or merely
bot-blocked, and rewrite the file.

Run this after any change to the detection logic, so a stale CSV never sends
you into a call with a claim that is no longer true.

    python reverify.py leads.csv --out leads_clean.csv
"""

import argparse
import csv
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

from find_leads import classify_website, COLUMNS
from osm_leads import rank


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--out", default="")
    ap.add_argument("--workers", type=int, default=24)
    ap.add_argument("--timeout", type=int, default=15)
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))

    with_site = [r for r in rows if r.get("website")]
    no_site = [r for r in rows if not r.get("website")]
    print("Re-verifying %d websites (%d rows have none)..."
          % (len(with_site), len(no_site)))

    before = Counter(r["web_status"] for r in with_site)

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(
            lambda r: classify_website(r["website"], timeout=args.timeout),
            with_site))

    changed = 0
    for r, info in zip(with_site, results):
        if info["web_status"] != r["web_status"]:
            changed += 1
        r.update(info)
        r["lead_score"] = rank(r)

    kept = [r for r in with_site
            if r["web_status"] not in ("OK", "BLOCKED")] + no_site
    kept.sort(key=lambda r: -int(r["lead_score"]))

    out = args.out or args.infile
    cols = COLUMNS if set(COLUMNS) >= set(rows[0].keys()) else list(rows[0])
    with open(out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(kept)

    dropped = len(rows) - len(kept)
    print("  %d statuses changed, %d rows dropped as healthy/blocked"
          % (changed, dropped))
    print("Wrote %d rows -> %s\n" % (len(kept), out))
    after = Counter(r["web_status"] for r in kept)
    for k in sorted(set(before) | set(after)):
        print("  %-13s %3d -> %3d" % (k, before.get(k, 0), after.get(k, 0)))


if __name__ == "__main__":
    main()
