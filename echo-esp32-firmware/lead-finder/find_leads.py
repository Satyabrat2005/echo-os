#!/usr/bin/env python3
"""
find_leads.py - Build a CSV of local businesses (EU + USA) with a weak or
missing web presence, using the official Google Places API (New).

Targets exactly the profile you want to sell to:
  * NO_WEBSITE   - Google listing has no website field at all
  * SOCIAL_ONLY  - "website" is just a Facebook / Instagram / Linktree page
  * BUILDER_PAGE - free Wix/Weebly/GoDaddy placeholder subdomain
  * DEAD         - domain does not resolve / 4xx / 5xx / times out
  * OUTDATED     - loads, but no mobile viewport, stale copyright, or no HTTPS
  * OK           - real, modern site (dropped unless --keep-ok)

Usage:
    set GOOGLE_MAPS_API_KEY=xxxxx
    python find_leads.py --target 200 --out leads.csv
    python find_leads.py --target 500 --region eu --categories cafe,bakery,gym
"""

import argparse
import csv
import os
import random
import re
import socket
import sys
import time
from collections import Counter
from datetime import datetime, timezone
from urllib.parse import urlparse

try:
    import requests
except ImportError:
    sys.exit("Missing dependency. Run:  pip install -r requirements.txt")

PLACES_URL = "https://places.googleapis.com/v1/places:searchText"

FIELD_MASK = ",".join([
    "places.id",
    "places.displayName",
    "places.formattedAddress",
    "places.nationalPhoneNumber",
    "places.internationalPhoneNumber",
    "places.websiteUri",
    "places.rating",
    "places.userRatingCount",
    "places.googleMapsUri",
    "places.businessStatus",
    "nextPageToken",
])

# ---------------------------------------------------------------- search grid

CATEGORIES = [
    "cafe", "coffee shop", "bakery", "restaurant", "pizzeria",
    "gym", "fitness studio", "yoga studio", "personal trainer",
    "barber shop", "hair salon", "nail salon", "beauty salon", "spa",
    "construction company", "builder", "plumber", "electrician",
    "roofing contractor", "painter decorator", "landscaping service",
    "car repair garage", "auto detailing", "tyre shop",
    "dentist", "physiotherapist", "veterinarian",
    "florist", "butcher", "dry cleaner", "locksmith", "moving company",
    "catering service", "photographer", "tattoo studio", "pet grooming",
]

USA_CITIES = [
    "Cleveland OH", "Buffalo NY", "Toledo OH", "Bakersfield CA", "Fresno CA",
    "Tulsa OK", "Wichita KS", "Akron OH", "Rochester NY", "Syracuse NY",
    "Dayton OH", "El Paso TX", "Laredo TX", "Shreveport LA", "Mobile AL",
    "Little Rock AR", "Des Moines IA", "Spokane WA", "Boise ID", "Reno NV",
    "Albuquerque NM", "Tucson AZ", "Fort Wayne IN", "Lubbock TX",
    "Chattanooga TN", "Augusta GA", "Jackson MS", "Scranton PA",
    "Erie PA", "Youngstown OH", "Flint MI", "Lansing MI", "Peoria IL",
    "Springfield MO", "Columbia SC", "Winston-Salem NC", "Newport News VA",
]

EU_CITIES = [
    "Naples Italy", "Palermo Italy", "Bari Italy", "Catania Italy",
    "Seville Spain", "Malaga Spain", "Zaragoza Spain", "Murcia Spain",
    "Porto Portugal", "Braga Portugal", "Lisbon Portugal",
    "Lodz Poland", "Katowice Poland", "Poznan Poland", "Gdansk Poland",
    "Brno Czechia", "Ostrava Czechia", "Kosice Slovakia",
    "Debrecen Hungary", "Szeged Hungary", "Cluj-Napoca Romania",
    "Timisoara Romania", "Plovdiv Bulgaria", "Varna Bulgaria",
    "Thessaloniki Greece", "Patras Greece", "Zagreb Croatia", "Split Croatia",
    "Duisburg Germany", "Dortmund Germany", "Essen Germany", "Bremen Germany",
    "Lille France", "Marseille France", "Saint-Etienne France", "Rouen France",
    "Liege Belgium", "Charleroi Belgium", "Rotterdam Netherlands",
    "Sheffield UK", "Bradford UK", "Stoke-on-Trent UK", "Sunderland UK",
    "Limerick Ireland", "Cork Ireland", "Aarhus Denmark", "Malmo Sweden",
]

# ------------------------------------------------------- website classification

SOCIAL_HOSTS = (
    "facebook.com", "fb.me", "fb.com", "instagram.com", "linktr.ee",
    "linkedin.com", "twitter.com", "x.com", "tiktok.com", "youtube.com",
    "pinterest.com", "yelp.com", "tripadvisor.", "google.com", "g.page",
    "business.site", "wa.me", "whatsapp.com", "nextdoor.com",
)

BUILDER_HOSTS = (
    "wixsite.com", "weebly.com", "godaddysites.com", "webnode.",
    "jimdosite.com", "site123.me", "blogspot.", "wordpress.com",
    "strikingly.com", "carrd.co", "webs.com", "tripod.com", "angelfire.com",
)

BOOKING_HOSTS = {
    "opentable.": "OpenTable", "thefork.": "TheFork", "resy.com": "Resy",
    "quandoo.": "Quandoo", "sevenrooms.com": "SevenRooms",
    "bookatable.": "Bookatable", "exploretock.com": "Tock",
    "calendly.com": "Calendly", "fresha.com": "Fresha",
    "treatwell.": "Treatwell", "booksy.": "Booksy",
    "mindbodyonline.com": "Mindbody", "squareup.com": "Square Appointments",
    "setmore.com": "Setmore", "acuityscheduling.com": "Acuity",
    "doctolib.": "Doctolib", "planity.com": "Planity",
    "ubereats.com": "UberEats", "deliveroo.": "Deliveroo",
    "justeat.": "JustEat", "lieferando.": "Lieferando",
    "doordash.com": "DoorDash", "grubhub.com": "Grubhub",
}

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/125.0 Safari/537.36")

CURRENT_YEAR = datetime.now().year


def host_of(url):
    try:
        h = (urlparse(url).hostname or "").lower()
        return h[4:] if h.startswith("www.") else h
    except Exception:
        return ""


def classify_website(url, timeout=8):
    """Return a dict describing the state of the business's web presence."""
    out = {
        "web_status": "NO_WEBSITE", "web_issue": "", "https": "",
        "mobile_friendly": "", "last_copyright_year": "",
        "has_online_booking": "no", "booking_platform": "",
        "social_only_link": "",
    }
    if not url:
        out["web_issue"] = "no website listed on Google Maps"
        return out

    url = url.strip()
    # Listings often store "www.foo.com" with no scheme. Without this, the
    # request raises MissingSchema and a perfectly healthy site gets branded
    # dead - the worst possible error to put in front of a salesperson.
    if not re.match(r"(?i)^[a-z][a-z0-9+.-]*://", url):
        if url.lower().startswith("mailto:") or "@" in url.split("/")[0]:
            out["web_issue"] = "listing holds an email address, not a website"
            return out
        url = "https://" + url.lstrip("/")

    host = host_of(url)
    if not host:
        out["web_issue"] = "listing has an unusable website value"
        return out

    if any(s in host for s in SOCIAL_HOSTS):
        out["web_status"] = "SOCIAL_ONLY"
        out["social_only_link"] = url
        out["web_issue"] = "only a social page (" + host + "), no real site"
        return out

    if any(b in host for b in BUILDER_HOSTS):
        out["web_status"] = "BUILDER_PAGE"
        out["web_issue"] = "free builder subdomain (" + host + "), no own domain"

    prior = out["web_issue"]

    def fail(status, msg):
        out["web_status"] = status
        out["web_issue"] = "; ".join(x for x in [prior, msg] if x)
        return out

    out["https"] = "yes" if url.lower().startswith("https") else "no"

    # Try the listed URL, then the other scheme. Plenty of small-business
    # sites are http-only or have a half-broken https redirect; giving up
    # after one attempt manufactures false "dead site" claims.
    attempts = [url]
    if url.lower().startswith("https://"):
        attempts.append("http://" + url[8:])
    elif url.lower().startswith("http://"):
        attempts.append("https://" + url[7:])

    r = None
    ssl_broken = False
    timed_out = False
    for attempt in attempts:
        try:
            r = requests.get(attempt, timeout=timeout,
                             headers={"User-Agent": UA}, allow_redirects=True)
            break
        except requests.exceptions.SSLError:
            ssl_broken = True
        except requests.exceptions.Timeout:
            timed_out = True
        except (requests.exceptions.ConnectionError, socket.gaierror):
            pass
        except Exception:
            pass

    if r is None:
        # Separate "domain is gone" from "domain exists but isn't serving" -
        # they are different sales conversations.
        resolves = True
        try:
            socket.gethostbyname(host)
        except Exception:
            resolves = False

        if not resolves:
            return fail("DEAD", "domain does not resolve - expired or never "
                                "set up (NXDOMAIN)")
        if ssl_broken:
            out["https"] = "broken"
            return fail("DEAD", "domain resolves but HTTPS is broken and "
                                "HTTP does not answer")
        if timed_out:
            return fail("DEAD", "domain resolves but server never responded ("
                        + str(timeout) + "s, both http and https)")
        return fail("DEAD", "domain resolves but refuses connections on "
                            "http and https")

    # 401/403/406/429 = bot protection (Cloudflare etc), NOT a broken site.
    # Calling these "dead" would embarrass you on the phone. Park them.
    if r.status_code in (401, 403, 406, 429):
        return fail("BLOCKED", "site blocks automated checks (HTTP "
                    + str(r.status_code) + ") - verify by hand")

    if r.status_code >= 400:
        return fail("DEAD", "HTTP " + str(r.status_code))

    html = r.text or ""
    low = html.lower()

    for frag, name in BOOKING_HOSTS.items():
        if frag in low:
            out["has_online_booking"] = "yes"
            out["booking_platform"] = name
            break

    issues = []

    mobile = 'name="viewport"' in low or "name='viewport'" in low
    out["mobile_friendly"] = "yes" if mobile else "no"
    if not mobile:
        issues.append("not mobile-responsive (no viewport tag)")

    if out["https"] == "no" and not r.url.lower().startswith("https"):
        issues.append("no HTTPS")

    years = [int(y) for y in re.findall(
        r"(?:copyright|&copy;|©)[^0-9]{0,20}(20[0-2][0-9])", low)]
    if years:
        newest = max(years)
        out["last_copyright_year"] = str(newest)
        if CURRENT_YEAR - newest >= 3:
            issues.append("copyright stuck at " + str(newest))

    if "<table" in low and "flex" not in low and "grid-template" not in low:
        issues.append("table-based layout (pre-2010 build)")
    if ".swf" in low or "<frameset" in low or "shockwave-flash" in low:
        issues.append("Flash / frameset remnants")
    if len(html) < 800:
        issues.append("near-empty placeholder page")

    if issues:
        if out["web_status"] == "NO_WEBSITE":
            out["web_status"] = "OUTDATED"
        parts = [x for x in [out["web_issue"]] + issues if x]
        out["web_issue"] = "; ".join(parts)
    elif out["web_status"] == "NO_WEBSITE":
        out["web_status"] = "OK"
        out["web_issue"] = "modern site, low priority"

    return out


def score(row):
    """0-100. Higher = better prospect for a website / booking build."""
    s = {"NO_WEBSITE": 55, "DEAD": 50, "SOCIAL_ONLY": 45,
         "BUILDER_PAGE": 35, "OUTDATED": 30, "OK": 0}.get(row["web_status"], 25)
    try:
        reviews = int(row["review_count"] or 0)
    except (TypeError, ValueError):
        reviews = 0
    try:
        rating = float(row["rating"] or 0)
    except (TypeError, ValueError):
        rating = 0.0

    if reviews >= 200:
        s += 20
    elif reviews >= 75:
        s += 15
    elif reviews >= 25:
        s += 10
    elif reviews >= 5:
        s += 5

    if rating >= 4.5:
        s += 10
    elif rating >= 4.0:
        s += 6

    if row["has_online_booking"] == "no":
        s += 8
    if row["phone"]:
        s += 7
    return min(s, 100)


# ------------------------------------------------------------------- API layer

def search_places(api_key, query, page_token=None):
    headers = {
        "Content-Type": "application/json",
        "X-Goog-Api-Key": api_key,
        "X-Goog-FieldMask": FIELD_MASK,
    }
    body = {"textQuery": query, "maxResultCount": 20}
    if page_token:
        body["pageToken"] = page_token
    try:
        r = requests.post(PLACES_URL, json=body, headers=headers, timeout=25)
    except Exception as e:
        print("  ! network error: " + str(e), file=sys.stderr)
        return {}
    if r.status_code == 429:
        time.sleep(5)
        return {}
    if r.status_code != 200:
        print("  ! API " + str(r.status_code) + ": " + r.text[:180],
              file=sys.stderr)
        return {}
    return r.json()


COLUMNS = [
    "business_name", "category", "city", "country_or_state", "region",
    "address", "phone", "rating", "review_count", "google_maps_url",
    "website", "web_status", "web_issue", "https", "mobile_friendly",
    "last_copyright_year", "has_online_booking", "booking_platform",
    "social_only_link", "lead_score", "place_id", "collected_at",
]


def build_queries(region, categories):
    pairs = []
    if region in ("usa", "both"):
        pairs += [(c, city, "USA") for city in USA_CITIES for c in categories]
    if region in ("eu", "both"):
        pairs += [(c, city, "Europe") for city in EU_CITIES for c in categories]
    random.shuffle(pairs)
    return pairs


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", type=int, default=200, help="rows to collect")
    ap.add_argument("--out", default="leads.csv")
    ap.add_argument("--region", choices=["eu", "usa", "both"], default="both")
    ap.add_argument("--categories", default="",
                    help="comma-separated override, e.g. cafe,gym,bakery")
    ap.add_argument("--min-reviews", type=int, default=5,
                    help="skip listings with fewer reviews")
    ap.add_argument("--keep-ok", action="store_true",
                    help="also keep businesses that already have a good site")
    ap.add_argument("--no-check", action="store_true",
                    help="skip live website checks (faster, less signal)")
    ap.add_argument("--delay", type=float, default=0.4,
                    help="seconds between API calls")
    args = ap.parse_args()

    api_key = os.environ.get("GOOGLE_MAPS_API_KEY", "").strip()
    if not api_key:
        sys.exit("Set GOOGLE_MAPS_API_KEY first - see README.md")

    cats = [c.strip() for c in args.categories.split(",") if c.strip()]
    if not cats:
        cats = CATEGORIES
    queries = build_queries(args.region, cats)

    rows = []
    seen = set()
    print("Collecting " + str(args.target) + " leads (" + args.region +
          ") across " + str(len(cats)) + " categories...\n")

    for cat, city, region in queries:
        if len(rows) >= args.target:
            break
        token = None
        page = 0
        while page < 3 and len(rows) < args.target:
            data = search_places(api_key, cat + " in " + city, token)
            places = data.get("places") or []
            if not places:
                break
            for p in places:
                if len(rows) >= args.target:
                    break
                pid = p.get("id")
                if not pid or pid in seen:
                    continue
                if p.get("businessStatus") not in (None, "OPERATIONAL"):
                    continue
                reviews = p.get("userRatingCount") or 0
                if reviews < args.min_reviews:
                    continue
                seen.add(pid)

                site = p.get("websiteUri") or ""
                if args.no_check:
                    info = {
                        "web_status": "NO_WEBSITE" if not site else "UNCHECKED",
                        "web_issue": "", "https": "", "mobile_friendly": "",
                        "last_copyright_year": "",
                        "has_online_booking": "unknown",
                        "booking_platform": "", "social_only_link": "",
                    }
                else:
                    info = classify_website(site)

                if info["web_status"] == "OK" and not args.keep_ok:
                    continue

                row = {
                    "business_name": (p.get("displayName") or {}).get("text", ""),
                    "category": cat,
                    "city": city.rsplit(" ", 1)[0] if region == "Europe" else city,
                    "country_or_state": city.rsplit(" ", 1)[-1],
                    "region": region,
                    "address": p.get("formattedAddress") or "",
                    "phone": (p.get("internationalPhoneNumber")
                              or p.get("nationalPhoneNumber") or ""),
                    "rating": p.get("rating", ""),
                    "review_count": reviews,
                    "google_maps_url": p.get("googleMapsUri") or "",
                    "website": site,
                    "place_id": pid,
                    "collected_at": datetime.now(timezone.utc)
                                            .strftime("%Y-%m-%d"),
                }
                row.update(info)
                row["lead_score"] = score(row)
                rows.append(row)
                print("  [" + str(len(rows)).rjust(4) + "/" +
                      str(args.target) + "] " +
                      str(row["lead_score"]).rjust(3) + " | " +
                      row["web_status"].ljust(12) + " | " +
                      row["business_name"][:38].ljust(38) + " | " + city)

            token = data.get("nextPageToken")
            page += 1
            if not token:
                break
            time.sleep(args.delay)
        time.sleep(args.delay)

    rows.sort(key=lambda r: -r["lead_score"])
    with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    print("\nWrote " + str(len(rows)) + " rows -> " + args.out)
    for k, v in Counter(r["web_status"] for r in rows).most_common():
        print("  " + k.ljust(13) + str(v))


if __name__ == "__main__":
    main()
