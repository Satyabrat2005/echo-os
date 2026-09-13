#!/usr/bin/env python3
"""
make_emails.py - Generate personalised outreach drafts for leads that published
an email address. Writes a review CSV plus one .txt per email.

It does NOT send anything. Review the drafts, then send them from your own
mail system so they come from your domain and your reputation.

    python make_emails.py leads_email.csv --outdir drafts
"""

import argparse
import csv
import os
import re
import sys
from collections import Counter

# ---------------------------------------------------------------- YOUR DETAILS
# Fill these in before sending. The postal address is a legal requirement for
# commercial email in the US (CAN-SPAM) and good practice everywhere.
SENDER = {
    "agency": "NeuroTitan",
    "person": "TODO your name",
    "role": "TODO your role, e.g. Founder",
    "email": "TODO you@neurotitan.in",
    "phone": "TODO your phone",
    "site": "https://neurotitan.in",
    "postal": "TODO registered postal address",
}

PRICE_BUILD = "$899"
PRICE_MONTHLY = "$60/month"

PORTFOLIO = [
    ("OpenUI", "https://openui.neurotitan.in", "an AI assistant interface"),
    ("ECHO", "https://echo.neurotitan.in", "AI powered assistive smart glasses"),
]

# Countries where B2B cold email needs prior consent rather than opt-out.
# Drafts for these are flagged CONSENT_REQUIRED - do not bulk-send them.
STRICT_CONSENT = {"Germany", "Austria", "Italy", "Spain", "Greece", "Portugal",
                  "Poland", "Czechia", "Slovakia", "Hungary", "Croatia",
                  "Slovenia", "Romania", "Bulgaria", "Serbia", "Netherlands",
                  "Belgium", "France", "Denmark", "Sweden", "Norway",
                  "Finland", "Estonia", "Latvia", "Lithuania", "Ireland",
                  "Switzerland", "UK"}


def portfolio_block():
    lines = []
    for name, url, what in PORTFOLIO:
        lines.append("  %s, %s\n  %s" % (name, what, url))
    return "\n\n".join(lines)


def subject_for(row):
    name = row["business_name"]
    st = row["web_status"]
    issue = (row.get("web_issue") or "").lower()
    if st == "DEAD":
        if "does not resolve" in issue or "nxdomain" in issue:
            return "%s: your website domain appears to have lapsed" % name
        if "404" in issue:
            return "%s: your website link is returning an error" % name
        return "%s: your website is not loading" % name
    if st == "OUTDATED":
        return "%s: how your website appears on a mobile phone" % name
    if st == "SOCIAL_ONLY":
        return "%s: your only web presence is a social page" % name
    if st == "BUILDER_PAGE":
        return "%s: your website is on a rented subdomain" % name
    return "%s: no website is listed for your business" % name


def opening_for(row):
    """The specific, checkable observation. No flattery, no invented facts."""
    st = row["web_status"]
    issue = (row.get("web_issue") or "").lower()
    site = row.get("website", "")
    cat = row.get("category", "business")
    city = row.get("city", "")

    if st == "DEAD":
        if "does not resolve" in issue or "nxdomain" in issue:
            return ("I tried to visit %s today and the domain no longer "
                    "resolves. It appears the registration has lapsed. "
                    "Anyone who clicks through to your website right now "
                    "sees a browser error instead of your business." % site)
        if "404" in issue:
            return ("I tried to visit %s today and it returns a page not "
                    "found error. I checked it twice to be certain."
                    % site)
        if "https is broken" in issue or "ssl" in issue:
            return ("I tried to visit %s today and the security certificate "
                    "has failed, so browsers display a warning screen before "
                    "anyone can reach your page." % site)
        return ("I tried to open %s today over both http and https, and "
                "received no response either time." % site)

    if st == "OUTDATED":
        bits = []
        if "not mobile-responsive" in issue:
            bits.append("there is no mobile layout, so on a phone it appears "
                        "as a shrunken desktop page")
        if "no https" in issue:
            bits.append("it still runs over http, so browsers label it as "
                        "Not Secure")
        if "copyright stuck" in issue:
            yr = row.get("last_copyright_year") or "some years ago"
            bits.append("the footer still reads %s" % yr)
        if "placeholder" in issue:
            bits.append("the page is essentially empty")
        if "table-based" in issue:
            bits.append("it is built on a table layout, which predates "
                        "smartphones entirely")
        detail = ", and ".join(bits[:2]) if bits else "it is showing its age"
        return ("I had a look at %s today and noticed that %s."
                % (site, detail))

    if st == "SOCIAL_ONLY":
        return ("I noticed the only web address on your public listing is a "
                "social media page. That leaves the platform deciding who "
                "sees your business, and gives you no way to take enquiries "
                "on your own terms.")

    if st == "BUILDER_PAGE":
        return ("I noticed your website runs on a free builder subdomain "
                "rather than your own domain name. It reads as temporary to "
                "customers, and it competes poorly in search results.")

    return ("I was looking through %ss in %s and noticed yours has no website "
            "listed at all. Unless someone already knows your name, there is "
            "very little for them to find online." % (cat, city))


def body_for(row):
    name = row["business_name"]
    strict = row["country_or_state"] in STRICT_CONSENT

    source = ("I found your business on its public OpenStreetMap listing, "
              "which is where your contact details are published.")

    return """Hello,

I am writing to you about {name}.

{opening}

My name is {person} and I run {agency}, a web studio that builds websites for
local businesses. We focus on sites that load quickly, work properly on
mobile, and are structured so that customers can actually find you in search
results.

A couple of examples of our recent work:

{portfolio}

I would be glad to put together a free one page mockup showing how a new site
for {name} could look, so you can see something concrete rather than take my
word for it. There is no cost for this and no obligation to go any further.

Would you like me to send it across?

{source}

Kind regards,
{person}
{role}, {agency}
{email} | {phone}
{site}

{postal}
If you would prefer not to receive any further messages from me, simply reply
with the word unsubscribe and I will remove your details immediately.
""".format(
        name=name,
        opening=opening_for(row),
        agency=SENDER["agency"],
        portfolio=portfolio_block(),
        source=source,
        person=SENDER["person"],
        role=SENDER["role"],
        email=SENDER["email"],
        phone=SENDER["phone"],
        site=SENDER["site"],
        postal=SENDER["postal"],
    )


_MX_CACHE = {}


def deliverable(email):
    """Does the domain actually accept mail?

    Many of these leads have a dead website, and the mailbox often sits on the
    same lapsed domain - so the address bounces. A batch with a 20%+ bounce
    rate will get a new sending domain flagged as spam almost immediately.
    """
    domain = email.split("@")[-1].lower()
    if domain in _MX_CACHE:
        return _MX_CACHE[domain]
    try:
        import dns.resolver
    except ImportError:
        return True, "MX not checked (pip install dnspython)"
    try:
        answers = dns.resolver.resolve(domain, "MX", lifetime=8)
        result = (True, "MX ok (%d)" % len(answers))
    except Exception as exc:
        result = (False, "no MX - would bounce (%s)" % type(exc).__name__)
    _MX_CACHE[domain] = result
    return result


def slug(s, n=48):
    s = re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-").lower()
    return s[:n] or "lead"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--outdir", default="drafts")
    ap.add_argument("--index", default="email_drafts.csv")
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = [r for r in csv.DictReader(f) if r.get("email")]

    if not rows:
        sys.exit("No rows with an email address in " + args.infile)

    os.makedirs(args.outdir, exist_ok=True)
    todo = [k for k, v in SENDER.items() if str(v).startswith("TODO")]

    print("Checking MX records so nothing is sent to a dead mailbox...")
    index = []
    written = 0
    for i, r in enumerate(rows, 1):
        ok, mx_note = deliverable(r["email"])
        subj = subject_for(r)
        strict = r["country_or_state"] in STRICT_CONSENT

        path = ""
        if ok:
            body = body_for(r)
            fname = "%03d-%s-%s.txt" % (i, slug(r["country_or_state"], 12),
                                        slug(r["business_name"]))
            path = os.path.join(args.outdir, fname)
            with open(path, "w", encoding="utf-8") as f:
                f.write("To: %s\nSubject: %s\n\n%s" % (r["email"], subj, body))
            written += 1

        index.append({
            "deliverable": "yes" if ok else "NO - call instead",
            "mx_check": mx_note,
            "business_name": r["business_name"],
            "email": r["email"],
            "phone": r["phone"],
            "city": r["city"],
            "country_or_state": r["country_or_state"],
            "region": r["region"],
            "web_status": r["web_status"],
            "priority": r.get("priority", ""),
            "legal_note": ("CONSENT_REQUIRED - do not bulk send"
                           if strict else "opt-out basis (CAN-SPAM)"),
            "subject": subj,
            "draft_file": path,
        })

    with open(args.index, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=list(index[0].keys()))
        w.writeheader()
        w.writerows(index)

    bounced = len(index) - written
    print("\nWrote %d drafts -> %s/" % (written, args.outdir))
    print("Index -> %s  (all %d rows, including the undeliverable)\n"
          % (args.index, len(index)))
    if bounced:
        print("  %d addresses have no MX record and would have bounced - no "
              "draft written for those; they are phone-only leads.\n" % bounced)
    print("  consent-required (EU/UK):",
          sum(1 for r in index if r["legal_note"].startswith("CONSENT")))
    print("  opt-out basis (US):",
          sum(1 for r in index if not r["legal_note"].startswith("CONSENT")))
    if todo:
        print("\n  !! Fill in SENDER fields before sending: " + ", ".join(todo))
    print("\nNothing has been sent. Review the drafts, then send from your "
          "own mail system.")


if __name__ == "__main__":
    main()
