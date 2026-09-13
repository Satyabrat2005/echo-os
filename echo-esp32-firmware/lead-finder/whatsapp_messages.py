#!/usr/bin/env python3
"""
whatsapp_messages.py - Write the first WhatsApp message for each Phase 1 lead,
in the language that business actually speaks.

Short on purpose. A cold WhatsApp message gets about one screen before the
reader decides to reply, block, or ignore. One specific verifiable observation,
then a straight question about interest. No pricing, nothing given away.

Structure is ISSUE + CLOSE, so the close can be reworded once per language
rather than in every variant.

    python whatsapp_messages.py phase1_whatsapp.csv
"""

import argparse
import csv
from collections import Counter

LANG_BY_COUNTRY = {
    "Poland": "pl", "Romania": "ro", "France": "fr",
    "Netherlands": "nl", "UK": "en", "Ireland": "en",
}

# The observation. {name} and {site} get substituted.
ISSUES = {
    "pl": {
        "DEAD": ("Dzień dobry! Piszę w sprawie {name}. Próbowałem dziś wejść "
                 "na Państwa stronę {site} i domena już nie działa, wygląda "
                 "na to, że wygasła. Klienci, którzy Państwa szukają, widzą "
                 "komunikat o błędzie."),
        "NO_WEBSITE": ("Dzień dobry! Piszę w sprawie {name}. Zauważyłem, że "
                       "nie mają Państwo strony internetowej, więc klienci "
                       "mogą Państwa znaleźć tylko wtedy, gdy już znają "
                       "nazwę firmy."),
        "OUTDATED": ("Dzień dobry! Piszę w sprawie {name}. Oglądałem dziś "
                     "Państwa stronę {site} i nie wyświetla się ona poprawnie "
                     "na telefonie, a przeglądarki oznaczają ją jako "
                     "niezabezpieczoną."),
    },
    "ro": {
        "DEAD": ("Bună ziua! Vă scriu în legătură cu {name}. Am încercat "
                 "astăzi să accesez {site} și domeniul nu mai funcționează, "
                 "se pare că a expirat. Clienții care vă caută online văd o "
                 "eroare."),
        "NO_WEBSITE": ("Bună ziua! Vă scriu în legătură cu {name}. Am "
                       "observat că nu aveți un site web, așa că vă pot găsi "
                       "doar clienții care vă știu deja numele."),
        "OUTDATED": ("Bună ziua! Vă scriu în legătură cu {name}. Am privit "
                     "astăzi site-ul {site} și nu se afișează corect pe "
                     "telefon."),
    },
    "fr": {
        "DEAD": ("Bonjour ! Je vous écris au sujet de {name}. J'ai essayé "
                 "d'ouvrir {site} aujourd'hui et le domaine ne fonctionne "
                 "plus, il semble avoir expiré. Les clients qui vous "
                 "cherchent en ligne tombent sur une erreur."),
        "NO_WEBSITE": ("Bonjour ! Je vous écris au sujet de {name}. J'ai "
                       "remarqué que vous n'avez pas de site internet, donc "
                       "seuls les clients qui connaissent déjà votre nom "
                       "peuvent vous trouver."),
        "OUTDATED": ("Bonjour ! Je vous écris au sujet de {name}. J'ai "
                     "regardé {site} aujourd'hui et le site ne s'affiche pas "
                     "correctement sur téléphone."),
    },
    "nl": {
        "DEAD": ("Goedendag! Ik schrijf u over {name}. Ik probeerde vandaag "
                 "{site} te openen, maar het domein werkt niet meer, het "
                 "lijkt verlopen te zijn. Klanten die u online zoeken krijgen "
                 "een foutmelding."),
        "NO_WEBSITE": ("Goedendag! Ik schrijf u over {name}. Het viel me op "
                       "dat u geen website heeft, waardoor alleen klanten die "
                       "uw naam al kennen u kunnen vinden."),
        "OUTDATED": ("Goedendag! Ik schrijf u over {name}. Ik bekeek vandaag "
                     "{site} en de site wordt niet goed weergegeven op een "
                     "telefoon."),
    },
    "en": {
        "DEAD": ("Hello! I am writing about {name}. I tried to open {site} "
                 "today and the domain no longer works, it looks like the "
                 "registration has lapsed. Anyone searching for you online "
                 "gets an error page."),
        "NO_WEBSITE": ("Hello! I am writing about {name}. I noticed you do "
                       "not have a website, so customers can only find you if "
                       "they already know your name."),
        "OUTDATED": ("Hello! I am writing about {name}. I looked at {site} "
                     "today and it does not display properly on a phone."),
    },
}

# The close. Straight interest check, nothing offered for free.
CLOSE = {
    "pl": ("Zajmuję się tworzeniem stron internetowych dla lokalnych firm. "
           "Jeśli są Państwo zainteresowani, możemy omówić stworzenie strony "
           "dla {name}. Czy chcieliby Państwo o tym porozmawiać?"),
    "ro": ("Realizez site-uri web pentru afaceri locale. Dacă sunteți "
           "interesat, putem discuta despre realizarea unui site pentru "
           "{name}. Doriți să vorbim?"),
    "fr": ("Je crée des sites internet pour les commerces locaux. Si cela "
           "vous intéresse, nous pouvons envisager la création d'un site pour "
           "{name}. Souhaitez-vous en discuter ?"),
    "nl": ("Ik maak websites voor lokale bedrijven. Als u interesse heeft, "
           "kunnen we bespreken hoe een website voor {name} eruit zou zien. "
           "Wilt u hierover praten?"),
    "en": ("I build websites for local businesses. If you are interested, we "
           "can look at building one for {name}. Would you like to discuss "
           "it?"),
}


def message_for(row):
    lang = LANG_BY_COUNTRY.get(row["country_or_state"], "en")
    status = row["web_status"]
    issue = ISSUES[lang].get(status) or ISSUES[lang]["NO_WEBSITE"]
    site = row.get("website", "").replace("https://", "").replace(
        "http://", "").rstrip("/")
    text = "%s\n\n%s" % (issue, CLOSE[lang])
    return lang, text.format(name=row["business_name"], site=site)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("--out", default="phase1_messages.csv")
    args = ap.parse_args()

    with open(args.infile, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))

    for r in rows:
        lang, msg = message_for(r)
        r["language"] = lang
        r["whatsapp_message"] = msg
        digits = "".join(ch for ch in r["phone"] if ch.isdigit())
        r["whatsapp_link"] = "https://wa.me/%s" % digits

    cols = ["lead_score", "priority", "business_name", "category", "phone",
            "whatsapp_link", "city", "country_or_state", "language",
            "call_window_IST", "web_status", "whatsapp_message", "website"]
    with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    print("Wrote %d messages -> %s\n" % (len(rows), args.out))
    print("  languages:", dict(Counter(r["language"] for r in rows)))
    print("  avg length: %d chars"
          % (sum(len(r["whatsapp_message"]) for r in rows) // len(rows)))


if __name__ == "__main__":
    main()
