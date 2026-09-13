#!/usr/bin/env python3
"""
add_pitch.py - Post-process a leads CSV: add a ready-to-say opening line and a
priority tier, so you can work the file top-down without thinking.

    python add_pitch.py leads.csv
    python add_pitch.py leads.csv --out leads_ready.csv
"""

import argparse
import csv
import sys


def tier(score):
    try:
        s = int(score)
    except (TypeError, ValueError):
        return "C"
    if s >= 75:
        return "A"
    if s >= 50:
        return "B"
    return "C"


def pitch(row):
    name = row.get("business_name", "").strip() or "there"
    cat = (row.get("category") or "business").strip()
    city = row.get("city", "").strip()
    status = row.get("web_status", "")
    issue = (row.get("web_issue") or "").lower()
    site = row.get("website", "")

    if status == "DEAD":
        if "nxdomain" in issue or "does not resolve" in issue:
            return ("Hi, is this %s? I was trying to look you up online and "
                    "your web address %s isn't working at all any more - the "
                    "domain looks like it's lapsed. Anyone searching for you "
                    "right now just hits an error. Did you know?"
                    % (name, site))
        if "404" in issue:
            return ("Hi, is this %s? Your website link comes up as a "
                    "'page not found' error - I checked it twice. Customers "
                    "clicking through from maps are hitting a dead end."
                    % name)
        return ("Hi, is this %s? Your website isn't loading - I tried it on "
                "both http and https and got nothing back. How long has it "
                "been down?" % name)

    if status == "SOCIAL_ONLY":
        return ("Hi, is this %s? I noticed the only web link you have is a "
                "Facebook page. That means Facebook controls whether people "
                "can find you, and you can't take bookings directly. Have you "
                "thought about a proper site you own?" % name)

    if status == "BUILDER_PAGE":
        return ("Hi, is this %s? Your site is on a free builder subdomain "
                "rather than your own domain name - it looks temporary to "
                "customers and it won't rank well against other %ss in %s."
                % (name, cat, city))

    if status == "OUTDATED":
        bits = []
        if "not mobile-responsive" in issue:
            bits.append("it doesn't work properly on a phone")
        if "no https" in issue:
            bits.append("browsers flag it as 'not secure'")
        if "copyright stuck" in issue:
            bits.append("the footer still says "
                        + row.get("last_copyright_year", "years ago"))
        if "placeholder" in issue:
            bits.append("the page is basically empty")
        if "table-based" in issue or "flash" in issue:
            bits.append("it's built on tech that's long obsolete")
        detail = ", and ".join(bits[:2]) if bits else "it's very dated"
        return ("Hi, is this %s? I had a look at your website and %s. Most of "
                "your customers are searching on their phones - what are they "
                "seeing?" % (name, detail))

    # NO_WEBSITE
    booking = row.get("booking_platform", "")
    if booking:
        return ("Hi, is this %s? You've got no website of your own, so every "
                "booking goes through %s and they take a cut of each one. "
                "Would you rather those came to you direct?" % (name, booking))
    return ("Hi, is this %s? I was looking for %ss in %s and you're one of "
            "the few with no website at all - people can only find you if "
            "they already know your name. Is that deliberate?"
            % (name, cat, city))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit("No rows in " + args.infile)

    cols = list(rows[0].keys())
    for extra in ("priority", "opening_line"):
        if extra not in cols:
            cols.append(extra)

    for r in rows:
        r["priority"] = tier(r.get("lead_score"))
        r["opening_line"] = pitch(r)

    out = args.out or args.infile.replace(".csv", "_ready.csv")
    with open(out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    from collections import Counter
    print("Wrote %d rows -> %s" % (len(rows), out))
    for k, v in sorted(Counter(r["priority"] for r in rows).items()):
        print("  tier %s: %d" % (k, v))


if __name__ == "__main__":
    main()
