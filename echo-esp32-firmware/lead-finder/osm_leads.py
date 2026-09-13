#!/usr/bin/env python3
"""
osm_leads.py - Pull real local businesses across Europe + USA from OpenStreetMap
(Overpass API, free, no key needed), then LOAD each business's website to verify
whether it is dead, outdated, social-only, or missing entirely.

Unlike a guess-list, every DEAD / OUTDATED / SOCIAL_ONLY row here was verified by
an actual HTTP request at collection time.

    python osm_leads.py --target 250 --out leads.csv

Data (c) OpenStreetMap contributors, ODbL. https://www.openstreetmap.org/copyright
"""

import argparse
import csv
import random
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone

try:
    import requests
except ImportError:
    sys.exit("Run:  pip install -r requirements.txt")

from find_leads import classify_website, COLUMNS

OVERPASS_ENDPOINTS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]

HDRS = {"User-Agent": "smb-web-audit/1.0 (lead research)"}

# ---------------------------------------------------------------- city grid
# (label, country_or_state, region, south, west, north, east)
CITIES = [
    # ---- Europe ----
    ("Napoli", "Italy", "Europe", 40.82, 14.20, 40.88, 14.30),
    ("Palermo", "Italy", "Europe", 38.09, 13.32, 38.15, 13.40),
    ("Bari", "Italy", "Europe", 41.09, 16.83, 41.14, 16.90),
    ("Catania", "Italy", "Europe", 37.48, 15.05, 37.53, 15.11),
    ("Sevilla", "Spain", "Europe", 37.35, -6.02, 37.42, -5.94),
    ("Malaga", "Spain", "Europe", 36.69, -4.46, 36.74, -4.39),
    ("Zaragoza", "Spain", "Europe", 41.62, -0.92, 41.68, -0.84),
    ("Porto", "Portugal", "Europe", 41.13, -8.65, 41.18, -8.57),
    ("Braga", "Portugal", "Europe", 41.53, -8.45, 41.57, -8.39),
    ("Lisboa", "Portugal", "Europe", 38.70, -9.18, 38.75, -9.11),
    ("Lodz", "Poland", "Europe", 51.73, 19.42, 51.79, 19.50),
    ("Katowice", "Poland", "Europe", 50.24, 18.98, 50.29, 19.05),
    ("Poznan", "Poland", "Europe", 52.38, 16.87, 52.43, 16.96),
    ("Gdansk", "Poland", "Europe", 54.33, 18.60, 54.38, 18.69),
    ("Brno", "Czechia", "Europe", 49.17, 16.57, 49.22, 16.64),
    ("Ostrava", "Czechia", "Europe", 49.81, 18.24, 49.86, 18.31),
    ("Debrecen", "Hungary", "Europe", 47.51, 21.60, 47.55, 21.66),
    ("Cluj-Napoca", "Romania", "Europe", 46.75, 23.55, 46.79, 23.63),
    ("Timisoara", "Romania", "Europe", 45.73, 21.20, 45.78, 21.27),
    ("Plovdiv", "Bulgaria", "Europe", 42.12, 24.71, 42.17, 24.78),
    ("Varna", "Bulgaria", "Europe", 43.19, 27.88, 43.23, 27.95),
    ("Thessaloniki", "Greece", "Europe", 40.61, 22.92, 40.65, 22.98),
    ("Zagreb", "Croatia", "Europe", 45.78, 15.94, 45.83, 16.01),
    ("Split", "Croatia", "Europe", 43.49, 16.42, 43.53, 16.48),
    ("Duisburg", "Germany", "Europe", 51.41, 6.73, 51.46, 6.80),
    ("Dortmund", "Germany", "Europe", 51.49, 7.43, 51.54, 7.50),
    ("Essen", "Germany", "Europe", 51.43, 6.98, 51.48, 7.05),
    ("Bremen", "Germany", "Europe", 53.05, 8.77, 53.10, 8.85),
    ("Lille", "France", "Europe", 50.61, 3.02, 50.65, 3.09),
    ("Marseille", "France", "Europe", 43.28, 5.35, 43.32, 5.42),
    ("Rouen", "France", "Europe", 49.42, 1.06, 49.46, 1.12),
    ("Liege", "Belgium", "Europe", 50.61, 5.54, 50.66, 5.61),
    ("Rotterdam", "Netherlands", "Europe", 51.90, 4.44, 51.94, 4.51),
    ("Sheffield", "UK", "Europe", 53.36, -1.50, 53.41, -1.43),
    ("Bradford", "UK", "Europe", 53.77, -1.79, 53.81, -1.73),
    ("Stoke-on-Trent", "UK", "Europe", 52.99, -2.21, 53.04, -2.14),
    ("Sunderland", "UK", "Europe", 54.88, -1.42, 54.92, -1.36),
    ("Cork", "Ireland", "Europe", 51.88, -8.51, 51.91, -8.44),
    ("Limerick", "Ireland", "Europe", 52.65, -8.65, 52.68, -8.60),
    ("Aarhus", "Denmark", "Europe", 56.13, 10.18, 56.18, 10.24),
    ("Malmo", "Sweden", "Europe", 55.58, 12.98, 55.62, 13.04),
    ("Graz", "Austria", "Europe", 47.05, 15.41, 47.09, 15.47),
    ("Linz", "Austria", "Europe", 48.28, 14.27, 48.32, 14.33),
    ("Ljubljana", "Slovenia", "Europe", 46.03, 14.48, 46.07, 14.54),
    ("Kosice", "Slovakia", "Europe", 48.70, 21.23, 48.74, 21.28),
    ("Bratislava", "Slovakia", "Europe", 48.13, 17.09, 48.17, 17.15),
    ("Vilnius", "Lithuania", "Europe", 54.66, 25.24, 54.70, 25.32),
    ("Kaunas", "Lithuania", "Europe", 54.88, 23.88, 54.92, 23.95),
    ("Riga", "Latvia", "Europe", 56.93, 24.08, 56.97, 24.16),
    ("Tartu", "Estonia", "Europe", 58.36, 26.69, 58.40, 26.75),
    ("Tampere", "Finland", "Europe", 61.48, 23.73, 61.51, 23.81),
    ("Turku", "Finland", "Europe", 60.43, 22.24, 60.47, 22.31),
    ("Bergen", "Norway", "Europe", 60.37, 5.30, 60.41, 5.36),
    ("Trondheim", "Norway", "Europe", 63.41, 10.36, 63.45, 10.43),
    ("Novi Sad", "Serbia", "Europe", 45.24, 19.82, 45.28, 19.87),
    ("Nis", "Serbia", "Europe", 43.30, 21.87, 43.34, 21.93),
    ("Valencia", "Spain", "Europe", 39.45, -0.40, 39.49, -0.34),
    ("Bilbao", "Spain", "Europe", 43.24, -2.95, 43.28, -2.90),
    ("Vigo", "Spain", "Europe", 42.22, -8.75, 42.25, -8.69),
    ("Genova", "Italy", "Europe", 44.39, 8.90, 44.43, 8.97),
    ("Verona", "Italy", "Europe", 45.42, 10.97, 45.46, 11.03),
    ("Firenze", "Italy", "Europe", 43.75, 11.23, 43.79, 11.29),
    ("Nantes", "France", "Europe", 47.19, -1.58, 47.24, -1.52),
    ("Toulouse", "France", "Europe", 43.58, 1.41, 43.62, 1.47),
    ("Nancy", "France", "Europe", 48.67, 6.15, 48.71, 6.21),
    ("Leipzig", "Germany", "Europe", 51.32, 12.35, 51.36, 12.41),
    ("Nurnberg", "Germany", "Europe", 49.43, 11.05, 49.47, 11.11),
    ("Wuppertal", "Germany", "Europe", 51.24, 7.11, 51.28, 7.19),
    ("Den Haag", "Netherlands", "Europe", 52.06, 4.27, 52.10, 4.33),
    ("Eindhoven", "Netherlands", "Europe", 51.42, 5.45, 51.46, 5.51),
    ("Gent", "Belgium", "Europe", 51.03, 3.69, 51.07, 3.75),
    ("Wroclaw", "Poland", "Europe", 51.08, 16.98, 51.13, 17.07),
    ("Szczecin", "Poland", "Europe", 53.40, 14.51, 53.45, 14.58),
    ("Bydgoszcz", "Poland", "Europe", 53.10, 17.97, 53.14, 18.03),
    ("Craiova", "Romania", "Europe", 44.30, 23.77, 44.34, 23.83),
    ("Iasi", "Romania", "Europe", 47.14, 27.55, 47.19, 27.62),
    ("Burgas", "Bulgaria", "Europe", 42.48, 27.44, 42.52, 27.49),
    ("Rijeka", "Croatia", "Europe", 45.31, 14.42, 45.35, 14.47),
    ("Larissa", "Greece", "Europe", 39.62, 22.39, 39.65, 22.44),
    ("Heraklion", "Greece", "Europe", 35.32, 25.11, 35.35, 25.16),
    ("Coventry", "UK", "Europe", 52.39, -1.53, 52.43, -1.47),
    ("Hull", "UK", "Europe", 53.73, -0.37, 53.77, -0.31),
    ("Swansea", "UK", "Europe", 51.60, -3.97, 51.64, -3.91),
    ("Dundee", "UK", "Europe", 56.44, -3.01, 56.48, -2.95),
    ("Galway", "Ireland", "Europe", 53.26, -9.08, 53.29, -9.02),
    ("Basel", "Switzerland", "Europe", 47.54, 7.57, 47.58, 7.62),
    # ---- USA ----
    ("Cleveland", "OH", "USA", 41.47, -81.72, 41.52, -81.65),
    ("Buffalo", "NY", "USA", 42.87, -78.90, 42.92, -78.84),
    ("Toledo", "OH", "USA", 41.63, -83.58, 41.68, -83.51),
    ("Bakersfield", "CA", "USA", 35.35, -119.05, 35.40, -118.98),
    ("Fresno", "CA", "USA", 36.72, -119.82, 36.78, -119.75),
    ("Tulsa", "OK", "USA", 36.12, -96.02, 36.18, -95.95),
    ("Wichita", "KS", "USA", 37.66, -97.36, 37.71, -97.29),
    ("Akron", "OH", "USA", 41.06, -81.55, 41.10, -81.49),
    ("Rochester", "NY", "USA", 43.14, -77.64, 43.18, -77.58),
    ("Syracuse", "NY", "USA", 43.02, -76.17, 43.07, -76.11),
    ("Dayton", "OH", "USA", 39.74, -84.22, 39.79, -84.15),
    ("El Paso", "TX", "USA", 31.74, -106.51, 31.80, -106.44),
    ("Shreveport", "LA", "USA", 32.48, -93.78, 32.53, -93.72),
    ("Little Rock", "AR", "USA", 34.72, -92.31, 34.76, -92.25),
    ("Des Moines", "IA", "USA", 41.57, -93.65, 41.62, -93.59),
    ("Spokane", "WA", "USA", 47.64, -117.44, 47.68, -117.38),
    ("Boise", "ID", "USA", 43.59, -116.23, 43.64, -116.17),
    ("Albuquerque", "NM", "USA", 35.06, -106.68, 35.12, -106.59),
    ("Tucson", "AZ", "USA", 32.19, -110.99, 32.25, -110.92),
    ("Scranton", "PA", "USA", 41.39, -75.68, 41.43, -75.63),
    ("Erie", "PA", "USA", 42.10, -80.11, 42.14, -80.05),
    ("Youngstown", "OH", "USA", 41.08, -80.68, 41.12, -80.62),
    ("Flint", "MI", "USA", 43.00, -83.72, 43.04, -83.66),
    ("Lansing", "MI", "USA", 42.71, -84.58, 42.75, -84.52),
    ("Peoria", "IL", "USA", 40.67, -89.63, 40.72, -89.57),
    ("Springfield", "MO", "USA", 37.17, -93.32, 37.23, -93.25),
    ("Winston-Salem", "NC", "USA", 36.07, -80.28, 36.12, -80.21),
    ("Birmingham", "AL", "USA", 33.49, -86.83, 33.54, -86.77),
    ("Mobile", "AL", "USA", 30.66, -88.08, 30.72, -88.01),
    ("Jackson", "MS", "USA", 32.28, -90.21, 32.33, -90.15),
    ("Baton Rouge", "LA", "USA", 30.42, -91.20, 30.47, -91.14),
    ("Knoxville", "TN", "USA", 35.94, -83.95, 35.99, -83.89),
    ("Chattanooga", "TN", "USA", 35.02, -85.33, 35.07, -85.26),
    ("Augusta", "GA", "USA", 33.44, -82.03, 33.49, -81.96),
    ("Columbia", "SC", "USA", 33.97, -81.06, 34.02, -80.99),
    ("Greensboro", "NC", "USA", 36.05, -79.82, 36.10, -79.76),
    ("Richmond", "VA", "USA", 37.51, -77.47, 37.57, -77.41),
    ("Newport News", "VA", "USA", 36.96, -76.44, 37.02, -76.37),
    ("Allentown", "PA", "USA", 40.58, -75.51, 40.63, -75.44),
    ("Reading", "PA", "USA", 40.31, -75.95, 40.36, -75.89),
    ("Harrisburg", "PA", "USA", 40.24, -76.91, 40.29, -76.85),
    ("Trenton", "NJ", "USA", 40.19, -74.79, 40.24, -74.72),
    ("Hartford", "CT", "USA", 41.74, -72.71, 41.79, -72.65),
    ("New Haven", "CT", "USA", 41.28, -72.96, 41.33, -72.89),
    ("Worcester", "MA", "USA", 42.24, -71.83, 42.29, -71.77),
    ("Springfield", "MA", "USA", 42.08, -72.62, 42.13, -72.55),
    ("Albany", "NY", "USA", 42.63, -73.80, 42.68, -73.73),
    ("Utica", "NY", "USA", 43.08, -75.26, 43.12, -75.20),
    ("Grand Rapids", "MI", "USA", 42.94, -85.70, 42.99, -85.63),
    ("Kalamazoo", "MI", "USA", 42.27, -85.62, 42.31, -85.55),
    ("Fort Wayne", "IN", "USA", 41.05, -85.17, 41.10, -85.10),
    ("South Bend", "IN", "USA", 41.65, -86.28, 41.70, -86.21),
    ("Evansville", "IN", "USA", 37.95, -87.60, 38.00, -87.53),
    ("Rockford", "IL", "USA", 42.24, -89.11, 42.29, -89.05),
    ("Madison", "WI", "USA", 43.05, -89.42, 43.10, -89.35),
    ("Green Bay", "WI", "USA", 44.49, -88.05, 44.54, -87.98),
    ("Duluth", "MN", "USA", 46.75, -92.14, 46.80, -92.07),
    ("Cedar Rapids", "IA", "USA", 41.95, -91.69, 42.00, -91.63),
    ("Omaha", "NE", "USA", 41.24, -95.98, 41.29, -95.91),
    ("Topeka", "KS", "USA", 39.03, -95.71, 39.08, -95.64),
    ("Oklahoma City", "OK", "USA", 35.44, -97.55, 35.50, -97.48),
    ("Amarillo", "TX", "USA", 35.18, -101.86, 35.23, -101.79),
    ("Lubbock", "TX", "USA", 33.56, -101.89, 33.61, -101.82),
    ("Corpus Christi", "TX", "USA", 27.76, -97.42, 27.81, -97.36),
    ("Laredo", "TX", "USA", 27.49, -99.53, 27.55, -99.46),
    ("Colorado Springs", "CO", "USA", 38.81, -104.85, 38.86, -104.78),
    ("Pueblo", "CO", "USA", 38.24, -104.64, 38.29, -104.58),
    ("Salt Lake City", "UT", "USA", 40.74, -111.92, 40.79, -111.85),
    ("Reno", "NV", "USA", 39.50, -119.83, 39.55, -119.76),
    ("Stockton", "CA", "USA", 37.93, -121.32, 37.98, -121.25),
    ("Modesto", "CA", "USA", 37.62, -121.03, 37.67, -120.96),
    ("Salem", "OR", "USA", 44.92, -123.06, 44.97, -122.99),
    ("Eugene", "OR", "USA", 44.02, -123.12, 44.07, -123.05),
    ("Tacoma", "WA", "USA", 47.22, -122.47, 47.28, -122.40),
]

# ------------------------------------------------------- OSM tag -> category
AMENITY = ("cafe|restaurant|bar|pub|fast_food|ice_cream|dentist|veterinary|"
           "driving_school|car_wash|nightclub")
SHOP = ("bakery|butcher|hairdresser|beauty|florist|car_repair|dry_cleaning|"
        "massage|tattoo|optician|confectionery|greengrocer|hardware|"
        "car_parts|bicycle|shoe_repair|jewelry|pet_grooming|furniture|"
        "travel_agency|photo|laundry")
LEISURE = "fitness_centre|sports_centre|dance"
CRAFT = ("plumber|electrician|carpenter|builder|roofer|painter|photographer|"
         "caterer|hvac|joiner|stonemason|tiler|scaffolder|locksmith|"
         "metal_construction|window_construction|gardener")

PRETTY = {
    "cafe": "cafe", "restaurant": "restaurant", "fast_food": "takeaway",
    "bar": "bar", "pub": "pub", "ice_cream": "ice cream shop",
    "bakery": "bakery", "butcher": "butcher", "hairdresser": "hair salon",
    "beauty": "beauty salon", "florist": "florist", "car_repair": "auto repair",
    "dry_cleaning": "dry cleaner", "massage": "massage / spa",
    "tattoo": "tattoo studio", "fitness_centre": "gym",
    "sports_centre": "sports centre", "dance": "dance studio",
    "plumber": "plumber", "electrician": "electrician",
    "carpenter": "carpenter", "builder": "construction / builder",
    "roofer": "roofing contractor", "painter": "painter & decorator",
    "photographer": "photographer", "caterer": "catering service",
    "hvac": "HVAC contractor", "dentist": "dentist",
    "veterinary": "veterinarian", "locksmith": "locksmith",
    "gardener": "landscaping", "tiler": "tiling contractor",
    "joiner": "joinery", "car_wash": "car wash",
}


def build_query(s, w, n, e):
    box = "(%s,%s,%s,%s)" % (s, w, n, e)
    parts = [
        'nwr["amenity"~"^(%s)$"]%s;' % (AMENITY, box),
        'nwr["shop"~"^(%s)$"]%s;' % (SHOP, box),
        'nwr["leisure"~"^(%s)$"]%s;' % (LEISURE, box),
        'nwr["craft"~"^(%s)$"]%s;' % (CRAFT, box),
        'nwr["office"="construction_company"]%s;' % box,
    ]
    return "[out:json][timeout:120];(%s);out center tags;" % "".join(parts)


def fetch_city(city, tries=2):
    label, cc, region, s, w, n, e = city
    q = build_query(s, w, n, e)
    for i in range(tries):
        url = OVERPASS_ENDPOINTS[i % len(OVERPASS_ENDPOINTS)]
        try:
            r = requests.post(url, data={"data": q}, headers=HDRS, timeout=180)
            if r.status_code == 200:
                return r.json().get("elements", [])
            if r.status_code in (429, 504):
                time.sleep(20)
        except Exception as exc:
            print("    ! %s: %s" % (label, type(exc).__name__), file=sys.stderr)
            time.sleep(5)
    return []


def category_of(tags):
    for key in ("amenity", "shop", "leisure", "craft"):
        v = tags.get(key)
        if v:
            return PRETTY.get(v, v.replace("_", " "))
    if tags.get("office") == "construction_company":
        return "construction company"
    return "local business"


def address_of(tags):
    bits = [
        " ".join(x for x in [tags.get("addr:housenumber"),
                             tags.get("addr:street")] if x),
        tags.get("addr:postcode"),
        tags.get("addr:city"),
    ]
    return ", ".join(b for b in bits if b)


CHAIN_WORDS = (
    "mcdonald", "subway", "starbucks", "kfc", "burger king", "domino",
    "papa john", "pizza hut", "nando", "costa coffee", "greggs", "wendy",
    "taco bell", "dunkin", "five guys", "chipotle", "tim horton", "pret a",
    "wetherspoon", "aldi", "lidl", "tesco", "sainsbury", "asda", "spar",
    "carrefour", "walgreens", "cvs", "7-eleven", "circle k", "shell",
    "planet fitness", "anytime fitness", "gold's gym", "pure gym", "puregym",
    "basic-fit", "mcfit", "supercuts", "great clips", "jiffy lube", "midas",
    "meineke", "o'reilly auto", "autozone", "enterprise rent", "u-haul",
    "h&r block", "sherwin-williams", "holiday inn", "hilton", "marriott",
    "ibis", "burger fi", "kwik fit", "halfords", "specsavers", "boots",
)


def is_chain(t, name):
    """Big brands are not your customer - drop them."""
    if t.get("brand") or t.get("brand:wikidata") or t.get("brand:wikipedia"):
        return True
    low = name.lower()
    return any(w in low for w in CHAIN_WORDS)


def to_row(el, city):
    label, cc, region, *_ = city
    t = el.get("tags", {})
    name = t.get("name")
    if name and is_chain(t, name):
        return None
    phone = (t.get("phone") or t.get("contact:phone")
             or t.get("phone:mobile") or "")
    if not name:
        return None
    site = (t.get("website") or t.get("contact:website")
            or t.get("url") or "")
    if not site and t.get("contact:facebook"):
        fb = t["contact:facebook"]
        site = fb if fb.startswith("http") else "https://facebook.com/" + fb
    lat = el.get("lat") or (el.get("center") or {}).get("lat", "")
    lon = el.get("lon") or (el.get("center") or {}).get("lon", "")
    return {
        "business_name": name,
        "category": category_of(t),
        "city": label,
        "country_or_state": cc,
        "region": region,
        "address": address_of(t),
        "phone": phone,
        "rating": "",
        "review_count": "",
        "google_maps_url": ("https://www.google.com/maps/search/?api=1&query="
                            "%s,%s" % (lat, lon)) if lat else "",
        "website": site,
        "place_id": "osm:%s/%s" % (el.get("type"), el.get("id")),
        "collected_at": datetime.now(timezone.utc).strftime("%Y-%m-%d"),
    }


def rank(row):
    """Verified-broken beats unverified-missing. Contactable beats not."""
    s = {
        "DEAD": 70, "OUTDATED": 55, "SOCIAL_ONLY": 50,
        "BUILDER_PAGE": 45, "NO_WEBSITE": 40, "OK": 0,
    }.get(row["web_status"], 25)
    if row["phone"]:
        s += 10
    if row["address"]:
        s += 5
    if row.get("mobile_friendly") == "no":
        s += 5
    if row.get("https") in ("no", "broken"):
        s += 4
    # more distinct faults = easier conversation
    s += min(row.get("web_issue", "").count(";") * 4, 12)
    return min(s, 100)


def stratify(rows, target):
    """Return a mixed file, not 250 rows of the same failure mode."""
    broken_kinds = ("DEAD", "OUTDATED", "SOCIAL_ONLY", "BUILDER_PAGE")
    broken = [r for r in rows if r["web_status"] in broken_kinds]
    missing = [r for r in rows if r["web_status"] == "NO_WEBSITE"]
    broken.sort(key=lambda r: -r["lead_score"])
    missing.sort(key=lambda r: -r["lead_score"])

    want_missing = int(target * 0.45)
    take_missing = missing[:want_missing]
    take_broken = broken[:target - len(take_missing)]
    # backfill if one bucket ran dry
    out = take_broken + take_missing
    if len(out) < target:
        spare = [r for r in broken[len(take_broken):]
                 + missing[len(take_missing):]]
        out += spare[:target - len(out)]
    out.sort(key=lambda r: -r["lead_score"])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", type=int, default=250)
    ap.add_argument("--out", default="leads.csv")
    ap.add_argument("--region", choices=["eu", "usa", "both"], default="both")
    ap.add_argument("--require-phone", action="store_true", default=True)
    ap.add_argument("--allow-no-phone", dest="require_phone",
                    action="store_false")
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--keep-ok", action="store_true")
    ap.add_argument("--pool", type=int, default=4,
                    help="oversample factor: how many candidates per wanted row")
    args = ap.parse_args()

    cities = [c for c in CITIES
              if args.region == "both"
              or (args.region == "eu" and c[2] == "Europe")
              or (args.region == "usa" and c[2] == "USA")]
    random.shuffle(cities)

    print("Pulling businesses from %d cities via Overpass...\n" % len(cities))

    candidates, seen_key = [], set()
    per_city_cap = max(25, args.target // 4)
    for city in cities:
        if len(candidates) >= args.target * args.pool:
            break
        els = fetch_city(city)
        random.shuffle(els)
        added = 0
        for el in els:
            if added >= per_city_cap:
                break
            row = to_row(el, city)
            if not row:
                continue
            if args.require_phone and not row["phone"]:
                continue
            # include state/country: there is a Springfield in both MO and MA
            key = (row["business_name"].lower(), row["city"],
                   row["country_or_state"])
            if key in seen_key:
                continue
            seen_key.add(key)
            candidates.append(row)
            added += 1
        print("  %-16s %-14s +%-4d  (pool %d)"
              % (city[0], city[1], added, len(candidates)))
        time.sleep(1.5)

    print("\nPool: %d contactable businesses. Verifying websites...\n"
          % len(candidates))

    random.shuffle(candidates)
    no_site = [c for c in candidates if not c["website"]]
    with_site = [c for c in candidates if c["website"]]

    # Check every listed website - this is where DEAD / OUTDATED come from.
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(
            lambda c: classify_website(c["website"], timeout=9), with_site))
    for c, info in zip(with_site, results):
        c.update(info)

    # Second pass: a single failed request can be a blip. Re-test everything
    # marked DEAD with a longer timeout before we let you phone someone and
    # tell them their website is down.
    suspects = [c for c in with_site if c["web_status"] == "DEAD"]
    if suspects:
        print("Re-testing %d 'dead' sites to rule out transient failures..."
              % len(suspects))
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            recheck = list(pool.map(
                lambda c: classify_website(c["website"], timeout=20), suspects))
        confirmed = 0
        for c, info in zip(suspects, recheck):
            c.update(info)
            if c["web_status"] == "DEAD":
                c["web_issue"] += " (confirmed on 2 attempts)"
                confirmed += 1
        print("  %d of %d still down.\n" % (confirmed, len(suspects)))

    for c in no_site:
        c.update(classify_website(""))
        c["web_issue"] = "no website recorded on the public listing"

    # BLOCKED = bot protection, tells us nothing about site quality. Drop it.
    rows = [c for c in with_site + no_site
            if c["web_status"] not in ("BLOCKED",)
            and (c["web_status"] != "OK" or args.keep_ok)]
    for r in rows:
        r["lead_score"] = rank(r)
    rows = stratify(rows, args.target)

    with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    print("Wrote %d rows -> %s\n" % (len(rows), args.out))
    for k, v in Counter(r["web_status"] for r in rows).most_common():
        print("  %-14s %d" % (k, v))
    print("\nData (c) OpenStreetMap contributors, ODbL.")


if __name__ == "__main__":
    main()
