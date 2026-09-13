#!/usr/bin/env python3
"""
call_schedule.py - Turn the lead list into a calling plan for someone sitting
in India, so you never dial a closed business.

For each lead it works out the local timezone, then converts a sensible local
calling window into IST. Restaurants and bars get a window that avoids the
lunch and dinner rush, when nobody will talk to you.

    python call_schedule.py leads_ready.csv
    python call_schedule.py leads_ready.csv --now      (who can I call rig ht now)
"""

import argparse
import csv
import sys
from collections import defaultdict
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

IST = ZoneInfo("Asia/Kolkata")

US_TZ = {
    "NY": "America/New_York", "OH": "America/New_York",
    "PA": "America/New_York", "MI": "America/New_York",
    "NC": "America/New_York", "VA": "America/New_York",
    "SC": "America/New_York", "GA": "America/New_York",
    "MA": "America/New_York", "CT": "America/New_York",
    "NJ": "America/New_York", "IN": "America/New_York",
    "IL": "America/Chicago", "TX": "America/Chicago",
    "OK": "America/Chicago", "KS": "America/Chicago",
    "IA": "America/Chicago", "MO": "America/Chicago",
    "LA": "America/Chicago", "AR": "America/Chicago",
    "WI": "America/Chicago", "MN": "America/Chicago",
    "AL": "America/Chicago", "MS": "America/Chicago",
    "TN": "America/Chicago", "NE": "America/Chicago",
    "CO": "America/Denver", "NM": "America/Denver",
    "UT": "America/Denver", "AZ": "America/Phoenix",
    "ID": "America/Boise", "NV": "America/Los_Angeles",
    "CA": "America/Los_Angeles", "WA": "America/Los_Angeles",
    "OR": "America/Los_Angeles",
}

EU_TZ = {
    "Italy": "Europe/Rome", "Spain": "Europe/Madrid",
    "Portugal": "Europe/Lisbon", "Poland": "Europe/Warsaw",
    "Czechia": "Europe/Prague", "Slovakia": "Europe/Bratislava",
    "Hungary": "Europe/Budapest", "Romania": "Europe/Bucharest",
    "Bulgaria": "Europe/Sofia", "Greece": "Europe/Athens",
    "Croatia": "Europe/Zagreb", "Slovenia": "Europe/Ljubljana",
    "Serbia": "Europe/Belgrade", "Germany": "Europe/Berlin",
    "France": "Europe/Paris", "Belgium": "Europe/Brussels",
    "Netherlands": "Europe/Amsterdam", "UK": "Europe/London",
    "Ireland": "Europe/Dublin", "Denmark": "Europe/Copenhagen",
    "Sweden": "Europe/Stockholm", "Norway": "Europe/Oslo",
    "Finland": "Europe/Helsinki", "Austria": "Europe/Vienna",
    "Switzerland": "Europe/Zurich", "Lithuania": "Europe/Vilnius",
    "Latvia": "Europe/Riga", "Estonia": "Europe/Tallinn",
}

# Hospitality is slammed at meal times. Call mid-afternoon instead.
FOOD = {"restaurant", "cafe", "bar", "pub", "takeaway", "bakery",
        "ice cream shop", "pizzeria"}


def tz_for(row):
    if row["region"] == "USA":
        return US_TZ.get(row["country_or_state"])
    return EU_TZ.get(row["country_or_state"])


def window_for(row):
    """(start_hour, end_hour) in the lead's local time."""
    if row["category"] in FOOD:
        return 14, 17          # after lunch, before dinner service
    return 10, 17


def to_ist(tzname, hour, when):
    local = datetime.now(ZoneInfo(tzname)).replace(
        hour=hour, minute=0, second=0, microsecond=0)
    if when:
        local = local.replace(year=when.year, month=when.month, day=when.day)
    return local.astimezone(IST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--out", default="call_plan.csv")
    ap.add_argument("--now", action="store_true",
                    help="only show leads callable right now")
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))

    now_ist = datetime.now(IST)
    print("Right now it is %s IST\n" % now_ist.strftime("%a %d %b, %H:%M"))

    plan, skipped = [], 0
    for r in rows:
        tzname = tz_for(r)
        if not tzname:
            skipped += 1
            continue
        s, e = window_for(r)
        start = to_ist(tzname, s, None)
        end = to_ist(tzname, e, None)
        local_now = now_ist.astimezone(ZoneInfo(tzname))
        callable_now = s <= local_now.hour < e and local_now.weekday() < 6

        r["timezone"] = tzname
        r["their_local_time"] = local_now.strftime("%H:%M")
        r["call_window_local"] = "%02d:00-%02d:00" % (s, e)
        r["call_window_IST"] = "%s-%s" % (start.strftime("%H:%M"),
                                          end.strftime("%H:%M"))
        r["callable_now"] = "YES" if callable_now else "no"
        plan.append(r)

    if args.now:
        plan = [r for r in plan if r["callable_now"] == "YES"]

    plan.sort(key=lambda r: (r["call_window_IST"], -int(r["lead_score"])))

    cols = list(plan[0].keys()) if plan else list(rows[0].keys())
    with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(plan)

    buckets = defaultdict(list)
    for r in plan:
        buckets[r["call_window_IST"]].append(r)

    print("Calling blocks in your time (IST):\n")
    for win in sorted(buckets):
        rows_ = buckets[win]
        places = sorted({r["country_or_state"] for r in rows_})
        live = sum(1 for r in rows_ if r["callable_now"] == "YES")
        flag = "  <-- OPEN NOW" if live else ""
        print("  %s  %3d leads   %s%s"
              % (win, len(rows_), ", ".join(places)[:44], flag))

    total_live = sum(1 for r in plan if r["callable_now"] == "YES")
    print("\n  callable right now: %d" % total_live)
    if skipped:
        print("  skipped (no timezone mapping): %d" % skipped)
    print("\nWrote %s" % args.out)


if __name__ == "__main__":
    main()
