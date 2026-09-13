#!/usr/bin/env python3
"""
phone_audit.py - Work out which lead phone numbers could plausibly be on
WhatsApp, and which are landlines that never will be.

WhatsApp requires a mobile number. A large share of local-business listings
carry a landline, so messaging that list means most attempts simply fail.

    python phone_audit.py leads_ready.csv
"""

import argparse
import csv
import re
import sys
from collections import Counter

# Mobile prefixes that follow the country code, per country.
# ("US"/"CA" deliberately absent: NANP area codes do not separate mobile from
# landline, so it cannot be determined from the number alone.)
MOBILE_RULES = {
    "44": ("7",),                       # UK
    "48": ("45", "50", "51", "53", "57", "60", "66", "69", "72", "73",
           "78", "79", "88"),           # Poland
    "49": ("15", "16", "17"),           # Germany
    "33": ("6", "7"),                   # France
    "39": ("3",),                       # Italy
    "34": ("6", "7"),                   # Spain
    "31": ("6",),                       # Netherlands
    "30": ("69",),                      # Greece
    "40": ("7",),                       # Romania
    "351": ("9",),                      # Portugal
    "353": ("8",),                      # Ireland
    "32": ("4",),                       # Belgium
    "420": ("6", "7"),                  # Czechia
    "421": ("9",),                      # Slovakia
    "385": ("9",),                      # Croatia
    "386": ("3", "4", "5", "6", "7"),   # Slovenia
    "359": ("87", "88", "89", "98", "99"),   # Bulgaria
    "36": ("20", "30", "31", "50", "70"),    # Hungary
    "46": ("7",),                       # Sweden
    "43": ("65", "66", "67", "68", "69"),    # Austria
    "370": ("6",),                      # Lithuania
    "371": ("2",),                      # Latvia
    "372": ("5",),                      # Estonia
    "358": ("4", "5"),                  # Finland
    "47": ("4", "9"),                   # Norway
    "381": ("6",),                      # Serbia
    "41": ("7",),                       # Switzerland
    "45": (),                           # Denmark: no mobile/landline split
}

NANP = {"1"}


def classify(raw):
    """-> (verdict, note)"""
    if not raw:
        return "no number", ""
    digits = re.sub(r"[^\d+]", "", raw)
    if not digits.startswith("+"):
        return "unknown", "no country code"
    digits = digits[1:]

    for cc_len in (3, 2, 1):
        cc = digits[:cc_len]
        rest = digits[cc_len:]
        if cc in NANP:
            return "undetermined", "US/Canada number, mobile cannot be " \
                                   "distinguished from landline"
        if cc in MOBILE_RULES:
            prefixes = MOBILE_RULES[cc]
            if not prefixes:
                return "undetermined", "country has no mobile/landline split"
            if any(rest.startswith(p) for p in prefixes):
                return "mobile", "+%s %s" % (cc, rest[:3])
            return "landline", "+%s %s is not a mobile range" % (cc, rest[:3])
    return "unknown", "unrecognised country code"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--out", default="phone_audit.csv")
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))

    for r in rows:
        verdict, note = classify(r.get("phone", ""))
        r["phone_type"] = verdict
        r["phone_note"] = note

    cols = list(rows[0].keys())
    with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    counts = Counter(r["phone_type"] for r in rows)
    total = len(rows)
    print("Phone audit of %d leads -> %s\n" % (total, args.out))
    for k, v in counts.most_common():
        print("  %-14s %4d   (%.0f%%)" % (k, v, 100.0 * v / total))

    eu = [r for r in rows if r["region"] == "Europe"]
    eu_c = Counter(r["phone_type"] for r in eu)
    print("\n  Europe only (where mobile can be detected):")
    for k, v in eu_c.most_common():
        print("    %-12s %4d   (%.0f%%)" % (k, v, 100.0 * v / len(eu)))
    print("\n  Reachable on WhatsApp at best: %d of %d"
          % (counts.get("mobile", 0) + counts.get("undetermined", 0), total))


if __name__ == "__main__":
    main()
