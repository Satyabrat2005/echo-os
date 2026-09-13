#!/usr/bin/env python3
"""
send_emails.py - Send the generated drafts over SMTP from your own mailbox.

Defaults to a DRY RUN. Nothing leaves the machine unless you pass --live.

    set SMTP_HOST=smtp.zoho.in
    set SMTP_USER=hello@neurotitan.in
    set SMTP_PASS=your_app_password

    python send_emails.py --region USA                 (dry run, shows plan)
    python send_emails.py --region USA --live          (actually sends)

Safety behaviour:
  * refuses to send any draft still containing a TODO placeholder
  * skips addresses already recorded in sent_log.csv, so a re-run is safe
  * waits between messages so a new domain does not look like a spam cannon
  * stops on the first SMTP failure rather than hammering the server
"""

import argparse
import csv
import os
import smtplib
import ssl
import sys
import time
from email.message import EmailMessage
from email.utils import formatdate, make_msgid

SENT_LOG = "sent_log.csv"


def load_sent():
    if not os.path.exists(SENT_LOG):
        return set()
    with open(SENT_LOG, newline="", encoding="utf-8-sig") as f:
        return {r["email"].lower() for r in csv.DictReader(f)}


def record_sent(row, message_id):
    exists = os.path.exists(SENT_LOG)
    with open(SENT_LOG, "a", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        if not exists:
            w.writerow(["sent_at", "email", "business_name", "subject",
                        "message_id"])
        w.writerow([formatdate(localtime=True), row["email"],
                    row["business_name"], row["subject"], message_id])


def read_draft(path):
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    head, _, body = raw.partition("\n\n")
    subject = ""
    for line in head.splitlines():
        if line.lower().startswith("subject:"):
            subject = line.split(":", 1)[1].strip()
    return subject, body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--index", default="email_drafts.csv")
    ap.add_argument("--region", default="", help="USA or Europe; blank = all")
    ap.add_argument("--limit", type=int, default=0, help="cap this batch")
    ap.add_argument("--delay", type=int, default=90,
                    help="seconds between messages")
    ap.add_argument("--live", action="store_true",
                    help="actually send; without this it is a dry run")
    args = ap.parse_args()

    with open(args.index, newline="", encoding="utf-8-sig") as f:
        rows = [r for r in csv.DictReader(f) if r["deliverable"] == "yes"]

    if args.region:
        rows = [r for r in rows if r["region"].lower() == args.region.lower()]

    already = load_sent()
    rows = [r for r in rows if r["email"].lower() not in already]
    if args.limit:
        rows = rows[:args.limit]

    if not rows:
        sys.exit("Nothing to send (all already sent, or filter matched none).")

    # Guard: never let placeholder text reach a real business.
    problems = []
    for r in rows:
        if not os.path.exists(r["draft_file"]):
            problems.append("%s: draft file missing" % r["business_name"])
            continue
        _, body = read_draft(r["draft_file"])
        if "TODO" in body:
            problems.append("%s: draft still contains TODO placeholders"
                            % r["business_name"])
    if problems:
        print("Refusing to send. Fix these first:\n")
        for p in problems[:10]:
            print("  " + p)
        if len(problems) > 10:
            print("  ... and %d more" % (len(problems) - 10))
        print("\nEdit the SENDER block in make_emails.py, then re-run it.")
        sys.exit(1)

    host = os.environ.get("SMTP_HOST", "")
    user = os.environ.get("SMTP_USER", "")
    pw = os.environ.get("SMTP_PASS", "")
    port = int(os.environ.get("SMTP_PORT", "465"))

    mins = (len(rows) - 1) * args.delay / 60.0
    print("Batch: %d messages%s" % (len(rows),
                                    " (region %s)" % args.region
                                    if args.region else ""))
    print("From:  %s" % (user or "SMTP_USER not set"))
    print("Pace:  %ds apart, about %.0f minutes total\n" % (args.delay, mins))
    for r in rows:
        print("  %-34s %-34s %s" % (r["business_name"][:34],
                                    r["email"][:34], r["web_status"]))

    if not args.live:
        print("\nDRY RUN. Nothing sent. Add --live to send for real.")
        return

    if not (host and user and pw):
        sys.exit("\nSet SMTP_HOST, SMTP_USER and SMTP_PASS before sending.")

    ctx = ssl.create_default_context()
    sent = 0
    print("\nSending...\n")
    try:
        with smtplib.SMTP_SSL(host, port, context=ctx) as smtp:
            smtp.login(user, pw)
            for i, r in enumerate(rows):
                subject, body = read_draft(r["draft_file"])
                msg = EmailMessage()
                msg["From"] = user
                msg["To"] = r["email"]
                msg["Subject"] = subject
                msg["Date"] = formatdate(localtime=True)
                mid = make_msgid(domain=user.split("@")[-1])
                msg["Message-ID"] = mid
                msg.set_content(body)
                smtp.send_message(msg)
                record_sent(r, mid)
                sent += 1
                print("  [%d/%d] sent to %s" % (sent, len(rows), r["email"]))
                if i < len(rows) - 1:
                    time.sleep(args.delay)
    except Exception as exc:
        print("\nStopped after %d message(s): %s: %s"
              % (sent, type(exc).__name__, exc))
        print("Already-sent addresses are logged in %s and will be skipped "
              "on the next run." % SENT_LOG)
        sys.exit(1)

    print("\nSent %d. Logged in %s." % (sent, SENT_LOG))


if __name__ == "__main__":
    main()
