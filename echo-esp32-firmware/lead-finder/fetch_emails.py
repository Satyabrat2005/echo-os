#!/usr/bin/env python3
"""
fetch_emails.py - Look up publicly published email addresses for leads already
collected, using the OSM ids stored in the `place_id` column.

Only returns addresses the business itself published on its public listing.
Rows with no published address stay blank - they are phone-only leads.

    python fetch_emails.py leads_ready.csv --out leads_email.csv
"""

import argparse
import csv
import re
import sys
import time
from collections import Counter, defaultdict

import requests

ENDPOINTS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]
HDRS = {"User-Agent": "smb-web-audit/1.0 (lead research)"}

EMAIL_KEYS = ("email", "contact:email", "operator:email")
EMAIL_RE = re.compile(r"^[^@\s,;]+@[^@\s,;]+\.[A-Za-z]{2,}$")


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


def query(ids_by_type, tries=2):
    parts = []
    for typ, ids in ids_by_type.items():
        if ids:
            parts.append("%s(id:%s);" % (typ, ",".join(ids)))
    if not parts:
        return []
    q = "[out:json][timeout:180];(%s);out tags;" % "".join(parts)
    for i in range(tries):
        try:
            r = requests.post(ENDPOINTS[i % len(ENDPOINTS)],
                              data={"data": q}, headers=HDRS, timeout=200)
            if r.status_code == 200:
                return r.json().get("elements", [])
            time.sleep(15)
        except Exception as exc:
            print("  ! %s" % type(exc).__name__, file=sys.stderr)
            time.sleep(5)
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--out", default="leads_email.csv")
    ap.add_argument("--batch", type=int, default=150)
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))

    # place_id looks like "osm:node/123456"
    wanted = []
    for r in rows:
        pid = r.get("place_id", "")
        if pid.startswith("osm:") and "/" in pid:
            typ, oid = pid[4:].split("/", 1)
            wanted.append((typ, oid, r))

    print("Looking up %d OSM objects for published email addresses...\n"
          % len(wanted))

    found = {}
    for batch in chunks(wanted, args.batch):
        by_type = defaultdict(list)
        for typ, oid, _ in batch:
            by_type[typ].append(oid)
        els = query(by_type)
        for el in els:
            key = "%s/%s" % (el.get("type"), el.get("id"))
            tags = el.get("tags", {})
            for k in EMAIL_KEYS:
                v = (tags.get(k) or "").strip()
                v = v.split(";")[0].strip()
                if v.lower().startswith("mailto:"):
                    v = v[7:]
                if EMAIL_RE.match(v):
                    found[key] = v
                    break
        print("  batch of %d -> %d emails so far" % (len(batch), len(found)))
        time.sleep(2)

    cols = list(rows[0].keys())
    if "email" not in cols:
        cols.insert(cols.index("phone") + 1, "email")

    for r in rows:
        pid = r.get("place_id", "")
        r["email"] = found.get(pid[4:], "") if pid.startswith("osm:") else ""

    with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    have = sum(1 for r in rows if r["email"])
    print("\nWrote %d rows -> %s" % (len(rows), args.out))
    print("  with published email: %d" % have)
    print("  phone-only:           %d" % (len(rows) - have))
    if have:
        print("\n  by status:",
              dict(Counter(r["web_status"] for r in rows if r["email"])))


if __name__ == "__main__":
    main()
