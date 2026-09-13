# Lead Finder — businesses with no website or a broken one (Europe + USA)

Two collectors and a pitch generator.

| File | Needs a key? | What it gives you |
|---|---|---|
| `osm_leads.py` | **No** | Real businesses from OpenStreetMap, each website **actually loaded and verified**. Start here. |
| `find_leads.py` | Google API key | Same idea via Google Places — adds ratings & review counts. |
| `add_pitch.py` | No | Adds `priority` (A/B/C) and a ready-to-say `opening_line` to any leads CSV. |

Coverage: **160 cities — 86 across Europe, 74 across the USA**, 63 countries/states,
spanning cafés, restaurants, bakeries, gyms, salons, builders, plumbers,
electricians, roofers, dentists, auto repair and ~40 other trades.

---

## Quick start (no API key)

```bash
pip install -r requirements.txt
```

```bash
python osm_leads.py --target 250 --out leads.csv
```

```bash
python add_pitch.py leads.csv --out leads_ready.csv
```

Open `leads_ready.csv`, sort by `priority`, and start at the top.

Options: `--region eu|usa|both`, `--target N`, `--workers N`,
`--allow-no-phone` (default is phone-only), `--keep-ok`.

---

## What "verified" means

For every business with a listed website, the script **makes a real HTTP
request** — both `https://` and `http://` — and records what happened:

| `web_status` | How it's determined | Your pitch |
|---|---|---|
| `DEAD` | Request failed twice, with a longer timeout on retry. DNS is checked separately so the row says *why*: expired domain (NXDOMAIN), server not answering, refused connection, or a 404/5xx. | "Your site is down — customers hit an error." |
| `OUTDATED` | Loads, but no mobile viewport / no HTTPS / broken SSL / copyright 3+ years stale / table layout / Flash remnants / near-empty page. | "Your site doesn't work on phones." |
| `SOCIAL_ONLY` | The only link is Facebook, Instagram, Linktree etc. | "Facebook owns your storefront, not you." |
| `BUILDER_PAGE` | Free `wixsite.com` / `weebly.com` / `godaddysites.com` subdomain. | "You're renting a subdomain, not your brand." |
| `NO_WEBSITE` | No website recorded on the listing. | "You're invisible outside maps." |
| `BLOCKED` | Site returned 401/403/406/429 — bot protection, **not** a broken site. **Excluded from output.** | — |
| `OK` | Modern working site. Dropped unless `--keep-ok`. | — |

**`BLOCKED` exists because of a real bug I hit:** the first version marked
Nando's as DEAD when Cloudflare returned 403. Telling a business their working
site is down destroys the call. Bot-blocked sites are now filtered out rather
than guessed about.

Chains are also dropped (via OSM `brand` tags plus a name blocklist) — you want
independents, not McDonald's.

---

## Output columns

`business_name, category, city, country_or_state, region, address, phone,
rating, review_count, google_maps_url, website, web_status, web_issue, https,
mobile_friendly, last_copyright_year, has_online_booking, booking_platform,
social_only_link, lead_score, place_id, collected_at`

After `add_pitch.py`: `+ priority, opening_line`.

- **`web_issue`** — the literal sentence to open with, e.g. *"domain does not
  resolve — expired or never set up (NXDOMAIN)"*.
- **`booking_platform`** — who owns their bookings today (OpenTable, TheFork,
  Fresha, Booksy, Deliveroo, DoorDash…). A business paying commission is the
  easiest sale for direct booking.
- **`lead_score` / `priority`** — severity of the web gap + reachability.

---

## Honest limits

- **`NO_WEBSITE` is the one unverified field.** It means *no website is recorded
  on the public listing* — not proof none exists. It's a strong signal, not a
  fact. `DEAD` / `OUTDATED` / `SOCIAL_ONLY` **are** verified.
- OSM coverage is uneven — dense in Western Europe, thinner in parts of the US
  South and rural areas.
- **Ad spend is not in here.** You asked for businesses running ads on other
  platforms; no public API exposes that per-business. Meta's Ad Library is
  searchable by advertiser name but has no bulk lookup, so it can't be joined
  onto 250 rows reliably. I left it out rather than fake the column.
- `rating` / `review_count` are blank in the OSM path — OSM has no reviews. Use
  `find_leads.py` with a Google key if you want those.

## Google Places path (optional)

Needs a key with **Places API (New)** enabled and billing on; roughly $32 per
1,000 requests, 20 businesses per request — 200 leads costs well under a dollar.

```bash
$env:GOOGLE_MAPS_API_KEY="your_key"; python find_leads.py --target 200 --out leads_google.csv
```

## Legal

OSM data © OpenStreetMap contributors, [ODbL](https://www.openstreetmap.org/copyright)
— attribution required if you redistribute it.

These are public business listings, but GDPR still applies to your outreach in
the EU: identify yourself, say where you got the data, honour opt-outs. Check
national do-not-call registries before dialling — cold-calling registered
numbers is illegal in most of these countries.
