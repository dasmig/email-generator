"""Build the email tables.

    python scripts/prepare_email.py [--out resources] [--faker build/faker]
                                    [--nicknames ../nickname-generator/resources/lite]
                                    [--countries ../country-generator/resources]

Writes, to <out>/lite and <out>/full:

  countries.tsv    region, population: country-generator's countries and populations
  fold.tsv         from, to: a letter and its ASCII form ('ø' 'o', 'ß' 'ss'),
                   built from Unicode decomposition of the Latin blocks plus a
                   few letters that do not decompose
  particles.tsv    particle: family-name particles that email names skip
  profiles.tsv     region, profile: which style profile a country uses
  email_styles.tsv profile, pattern, weight: the shapes of an email's local
                   part (see doc/data.md for the pattern language)
  providers.tsv    domain, country, kind, weight: the domains addresses use;
                   country '*' is every country
  words.tsv        list, word: the handle words (copied from nickname-generator)

Providers are seeded from Faker's locale data (MIT; the free_email_domains of
each locale, downloaded into --faker), then curated: every weight, the global
webmail shares, the extra domains for countries Faker lacks and the profile
of each country are curated for this library (rule 6, see doc/data.md).
"""

import argparse
import ast
import csv
import re
import sys
import unicodedata
import urllib.request
from pathlib import Path

FAKER_URL = "https://raw.githubusercontent.com/joke2k/faker/master/faker/providers/internet/%s/__init__.py"

# Faker locale -> country. bn_BD is left out: its list is Russian webmail copied from ru_RU.
LOCALES = {
    "az_AZ": "AZ", "bg_BG": "BG", "bs_BA": "BA", "cs_CZ": "CZ", "de_AT": "AT", "de_DE": "DE",
    "el_GR": "GR", "en_AU": "AU", "en_GB": "GB", "en_NZ": "NZ", "en_PH": "PH", "fa_IR": "IR", "fi_FI": "FI",
    "fr_CH": "CH", "fr_FR": "FR", "hr_HR": "HR", "hu_HU": "HU", "it_IT": "IT", "ko_KR": "KR", "mk_MK": "MK",
    "pl_PL": "PL", "pt_BR": "BR", "pt_PT": "PT", "ro_RO": "RO", "ru_RU": "RU", "sk_SK": "SK", "sl_SI": "SI",
    "sv_SE": "SE", "th_TH": "TH", "tr_TR": "TR", "uk_UA": "UA",
}

# Curated. Global webmail, shares of the mass of an address outside the
# countries with their own list.
GLOBAL = [
    ("gmail.com", "free", 5.0), ("outlook.com", "free", 1.3), ("hotmail.com", "free", 1.2),
    ("yahoo.com", "free", 1.1), ("icloud.com", "free", 1.0), ("proton.me", "free", 0.4),
]
GLOBAL_NAMES = {d for d, _, _ in GLOBAL} | {"googlemail.com", "live.com", "msn.com"}
# Faker's domains that are webmail rather than an internet provider's.
WEBMAIL = set("""
mail.ru yandex.ru rambler.ru gmx.at gmx.de web.de libero.it laposte.net seznam.cz email.cz post.cz
centrum.cz centrum.sk zoznam.sk post.sk onet.pl interia.pl o2.pl abv.bg mail.bg dir.bg email.ro
email.ua i.ua meta.ua ukr.net email.si daum.net hanmail.net naver.com nate.com box.az amail.az
freemail.hu citromail.hu indamail.hu mailbox.hu live.se spray.se yaani.com yandex.com chmail.ir mailfa.com
zohomail.com ymail.com protonmail.com hotmail.de hotmail.fr hotmail.gr hotmail.it hotmail.co.uk yahoo.fr
aol.de yahoo.de yahoo.gr yahoo.co.uk yahoo.com.br yahoo.com.au hotmail.com.au outlook.com.br dbmail.com
""".split())

# Curated extras for countries Faker has no list for, or where its list is
# not the whole picture: country -> [(domain, kind, weight)], and the total
# share of local domains for the country (default LOCAL_SHARE). A global
# domain listed here replaces the global row for that country.
EXTRA = {
    "US": [("aol.com", "free", 0.7), ("comcast.net", "isp", 0.6), ("att.net", "isp", 0.3), ("verizon.net", "isp", 0.3)],
    "CA": [("sympatico.ca", "isp", 0.3), ("shaw.ca", "isp", 0.3), ("rogers.com", "isp", 0.3), ("videotron.ca", "isp", 0.2)],
    "IN": [("rediffmail.com", "free", 1.5), ("yahoo.co.in", "free", 1.0)],
    "CN": [("qq.com", "free", 40), ("163.com", "free", 25), ("126.com", "free", 8), ("sina.com", "free", 5),
           ("foxmail.com", "free", 3), ("gmail.com", "free", 1), ("outlook.com", "free", 1), ("hotmail.com", "free", 0),
           ("yahoo.com", "free", 0), ("icloud.com", "free", 1), ("proton.me", "free", 0)],
    "JP": [("yahoo.co.jp", "free", 3), ("docomo.ne.jp", "isp", 2), ("ezweb.ne.jp", "isp", 1.5), ("softbank.ne.jp", "isp", 1.5),
           ("i.softbank.jp", "isp", 1), ("icloud.com", "free", 2), ("nifty.com", "isp", 0.5)],
    "TW": [("yahoo.com.tw", "free", 3), ("hotmail.com.tw", "free", 1), ("hinet.net", "isp", 1)],
    "HK": [("yahoo.com.hk", "free", 2), ("hotmail.com", "free", 1.5), ("netvigator.com", "isp", 1)],
    "VN": [("yahoo.com.vn", "free", 2), ("hotmail.com", "free", 1)],
    "ID": [("yahoo.co.id", "free", 3), ("yahoo.com", "free", 1.5)],
    "MY": [("yahoo.com.my", "free", 2), ("hotmail.my", "free", 0.5)],
    "SG": [("yahoo.com.sg", "free", 1.5), ("singnet.com.sg", "isp", 0.5)],
    "NL": [("ziggo.nl", "isp", 1), ("kpnmail.nl", "isp", 1), ("planet.nl", "isp", 0.5), ("hotmail.nl", "free", 0.8), ("live.nl", "free", 0.5)],
    "BE": [("skynet.be", "isp", 1), ("telenet.be", "isp", 1), ("hotmail.be", "free", 0.7), ("proximus.be", "isp", 0.5)],
    "DK": [("hotmail.dk", "free", 1), ("live.dk", "free", 0.6), ("mail.dk", "free", 0.6), ("jubii.dk", "free", 0.3), ("yahoo.dk", "free", 0.3)],
    "NO": [("online.no", "isp", 0.7), ("live.no", "free", 0.8), ("hotmail.no", "free", 0.6), ("start.no", "isp", 0.3), ("yahoo.no", "free", 0.3)],
    "ES": [("hotmail.es", "free", 1.5), ("telefonica.net", "isp", 0.7), ("yahoo.es", "free", 1.0), ("terra.es", "isp", 0.3), ("movistar.es", "isp", 0.3)],
    "MX": [("hotmail.com", "free", 1.0), ("yahoo.com.mx", "free", 1.5), ("prodigy.net.mx", "isp", 0.6), ("live.com.mx", "free", 0.5)],
    "AR": [("hotmail.com.ar", "free", 1.2), ("yahoo.com.ar", "free", 1.5), ("fibertel.com.ar", "isp", 0.4), ("speedy.com.ar", "isp", 0.3)],
    "CL": [("hotmail.cl", "free", 1.0), ("yahoo.es", "free", 0.5), ("vtr.net", "isp", 0.3)],
    "CO": [("hotmail.es", "free", 1.0), ("yahoo.es", "free", 0.8), ("une.net.co", "isp", 0.2)],
    "PE": [("hotmail.es", "free", 1.0), ("yahoo.es", "free", 0.8)],
    "VE": [("hotmail.es", "free", 1.0), ("yahoo.es", "free", 1.0), ("cantv.net", "isp", 0.3)],
    "EG": [("yahoo.com", "free", 1.0), ("hotmail.com", "free", 1.0)],
    "SA": [("hotmail.com", "free", 1.0), ("yahoo.com", "free", 1.0), ("stc.com.sa", "isp", 0.2)],
    "AE": [("hotmail.com", "free", 1.0), ("yahoo.com", "free", 1.0), ("eim.ae", "isp", 0.3)],
    "ZA": [("webmail.co.za", "free", 1.0), ("mweb.co.za", "isp", 0.8), ("telkomsa.net", "isp", 0.6), ("vodamail.co.za", "isp", 0.3), ("yahoo.co.za", "free", 0.6)],
    "NG": [("yahoo.com", "free", 3.0), ("yahoo.co.uk", "free", 0.4)],
    "KE": [("yahoo.com", "free", 1.5)],
    "PK": [("yahoo.com", "free", 1.5), ("hotmail.com", "free", 1.0)],
    "BD": [("yahoo.com", "free", 1.5), ("live.com", "free", 0.5)],
    "IE": [("eircom.net", "isp", 0.7), ("hotmail.ie", "free", 0.3), ("yahoo.ie", "free", 0.5), ("live.ie", "free", 0.3)],
    "KZ": [("mail.ru", "free", 4.0), ("yandex.ru", "free", 2.0), ("inbox.ru", "free", 0.5)],
    "BY": [("mail.ru", "free", 4.0), ("yandex.ru", "free", 2.0), ("tut.by", "free", 2.0)],
    "LT": [("one.lt", "free", 0.7), ("inbox.lt", "free", 1.0), ("takas.lt", "isp", 0.3)],
    "LV": [("inbox.lv", "free", 1.0), ("apollo.lv", "free", 0.7), ("tvnet.lv", "isp", 0.3)],
    "EE": [("hotmail.com", "free", 0.8), ("online.ee", "free", 0.7), ("mail.ee", "free", 0.5)],
    "RS": [("yahoo.com", "free", 1.0), ("eunet.rs", "isp", 0.7), ("ptt.rs", "isp", 0.4), ("sbb.rs", "isp", 0.4)],
    "AT": [("aon.at", "isp", 0.4), ("A1.net", "isp", 0.3)],
}
LOCAL_SHARE = 5.0  # default total weight of a Faker list (global mass is 10)
LOCAL_SHARE_BY = {"RU": 12, "UA": 10, "KR": 14, "DE": 12, "IT": 9, "FR": 8, "PL": 10, "CZ": 10, "SK": 10, "HU": 8,
                  "TR": 5, "IR": 8, "BG": 6, "RO": 6, "HR": 6, "MK": 5, "BA": 5, "SI": 5, "AT": 5, "CH": 5}

# Curated. Style profile by country (default for the rest).
PROFILES = {
    "anglo": "US CA GB IE AU NZ ZA PH SG MY NG GH KE UG TZ ZM JM BS BB TT IN PK BD LK NP ZW BW NA MW FJ",
    "germanic": "DE AT CH LI LU NL BE DK NO SE FI IS",
    "romance": "FR ES IT PT RO MC AD SM VA MT MX CO VE CL PE EC UY PY BO CR PA GT HN SV NI DO CU AO MZ",
    "slavic": "RU UA BY PL CZ SK BG RS HR SI BA MK ME LT LV EE MD KZ",
    "eastasian": "CN TW HK MO KR VN MN KP",
    "japan": "JP",
    "arabic": "SA AE EG DZ MA TN LY IQ JO LB SY KW QA BH OM YE SD PS IR",
    "brazil": "BR",
}
# Curated. Weight of a separator between the parts of a name, by profile.
SEPARATORS = {
    "anglo": {".": 5.0, "_": 2.5, "-": 0.2, "": 3.0},
    "germanic": {".": 6.0, "_": 1.0, "-": 0.5, "": 2.0},
    "romance": {".": 5.0, "_": 2.0, "-": 0.3, "": 3.0},
    "slavic": {".": 5.0, "_": 3.0, "-": 1.0, "": 1.5},
    "eastasian": {".": 3.0, "_": 3.0, "-": 0.2, "": 4.0},
    "japan": {".": 3.0, "_": 3.0, "-": 0.5, "": 3.0},
    "arabic": {".": 3.0, "_": 3.0, "-": 0.2, "": 4.0},
    "brazil": {".": 4.0, "_": 3.0, "-": 0.3, "": 4.0},
    "default": {".": 5.0, "_": 2.5, "-": 0.3, "": 3.0},
}
# Curated. Pattern: {f} given name, {l} family name, {fi} {li} their initials, {n} nickname,
# {y2} {y4} birth year, {r} {r2} random digits, {s} separator; {adj} {animal} {jp} handle words.
# Each profile starts from STYLES and its own changes (a weight, or None to drop a pattern).
STYLES = {
    "{f}{s}{l}": 28, "{l}{s}{f}": 4, "{fi}{l}": 8, "{fi}{s}{l}": 3, "{f}{li}": 3, "{f}": 3,
    "{f}{s}{l}{y2}": 6, "{f}{s}{l}{y4}": 2, "{f}{y2}": 4, "{f}{y4}": 4, "{f}{r2}": 4, "{f}{r}": 2,
    "{f}{s}{l}{r2}": 5, "{l}{r2}": 2, "{l}{y2}": 1.5,
    "{n}": 30, "{n}{y2}": 6, "{n}{r2}": 8, "{n}{y4}": 3,
    "{adj}{animal}": 3, "{adj}{s}{animal}": 2, "{adj}{animal}{r2}": 3, "{animal}{r}": 1,
}
CHANGES = {
    "germanic": {"{l}{s}{f}": 7, "{fi}{l}": 9, "{f}{s}{l}": 32},
    "romance": {"{f}{s}{l}": 30, "{f}{y4}": 5, "{f}{y2}": 5},
    "slavic": {"{l}{s}{f}": 9, "{f}{r2}": 7, "{f}{s}{l}{y2}": 7, "{f}{y4}": 5, "{f}": 4},
    "eastasian": {"{l}{s}{f}": 14, "{f}{s}{l}": 14, "{f}{r2}": 7, "{fi}{l}": 5, "{f}{y2}": 6},
    "japan": {"{l}{s}{f}": 12, "{f}{s}{l}": 12, "{f}{r2}": 8, "{f}{y2}": 6, "{jp}{jp}": 4, "{jp}{s}{jp}{r2}": 3,
              "{adj}{animal}": 1, "{adj}{s}{animal}": 0.5, "{adj}{animal}{r2}": 1, "{animal}{r}": 0.5},
    "arabic": {"{f}{s}{l}": 25, "{f}{r2}": 7, "{f}{y2}": 5, "{f}": 6, "{fi}{l}": 6},
    "brazil": {"{f}{s}{l}": 26, "{f}{y2}": 6, "{f}{y4}": 5, "{f}{r2}": 5, "{f}{s}{l}{y2}": 6, "{f}": 5},
    "default": {},
}

PARTICLES = """
a al am an ap auf aus bar bat bin binti bint da dal dalla das de degli dei del della dem den der des di do dos
du el en ibn la las le les lo los op ten ter und van vande vander vom von
""".split()

# Letters that do not decompose.
SPECIAL = {"ß": "ss", "ø": "o", "æ": "ae", "œ": "oe", "đ": "d", "ð": "d", "þ": "th", "ł": "l", "ħ": "h", "ı": "i",
           "ŋ": "n", "ŧ": "t", "ĸ": "k", "ſ": "s", "ƒ": "f", "ǆ": "dz", "ǉ": "lj", "ǌ": "nj"}


def fold_rows():
    rows = []
    for cp in list(range(0xC0, 0x250)) + list(range(0x1E00, 0x1F00)):
        ch = chr(cp)
        if not ch.isalpha():
            continue
        low = ch.lower()
        if low in SPECIAL:
            to = SPECIAL[low]
        else:
            nfd = unicodedata.normalize("NFD", low)
            to = "".join(c for c in nfd if not unicodedata.combining(c))
            if not to or not all("a" <= c <= "z" for c in to):
                continue
        if ch in ("×", "÷"):
            continue
        rows.append((ch, to))
    return rows


def faker_domains(cache):
    out = {}
    for locale, country in LOCALES.items():
        f = cache / f"{locale}.py"
        if not f.exists():
            cache.mkdir(parents=True, exist_ok=True)
            f.write_bytes(urllib.request.urlopen(FAKER_URL % locale).read())
        for node in ast.walk(ast.parse(f.read_text(encoding="utf-8"))):
            if (isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
                    and node.targets[0].id == "free_email_domains"):
                out[country] = list(ast.literal_eval(node.value))
    return out


def provider_rows(faker):
    rows = [(d, "*", k, w) for d, k, w in GLOBAL]
    countries = set(faker) | set(EXTRA)
    for c in sorted(countries):
        local = {}
        for d in faker.get(c, []):
            if d.lower() in GLOBAL_NAMES:
                continue
            local[d.lower()] = local.get(d.lower(), 0) + 1
        total = sum(local.values())
        if total:
            share = LOCAL_SHARE_BY.get(c, LOCAL_SHARE)
            for d, n in local.items():
                rows.append((d, c, "free" if d in WEBMAIL else "isp", round(share * n / total, 3)))
        for d, k, w in EXTRA.get(c, []):
            d = d.lower()
            rows = [r for r in rows if not (r[0] == d and r[1] == c)]
            rows.append((d, c, k, w))
    return rows


def style_rows():
    rows = []
    for prof in list(PROFILES) + ["default"]:
        styles = dict(STYLES)
        for k, v in CHANGES.get(prof, {}).items():
            styles[k] = v
        seps = SEPARATORS[prof]
        total = sum(seps.values())
        for pat, w in styles.items():
            if "{jp}" in pat and prof != "japan":
                continue
            if "{s}" not in pat:
                rows.append((prof, pat, round(w, 4)))
                continue
            for s, sw in seps.items():
                rows.append((prof, pat.replace("{s}", s), round(w * sw / total, 4)))
    # patterns that collapse to the same text in one profile add up
    merged = {}
    for prof, pat, w in rows:
        merged[(prof, pat)] = merged.get((prof, pat), 0) + w
    return [(p, pat, round(w, 4)) for (p, pat), w in merged.items() if w > 0]


def word_rows(nick):
    rows = []
    for name, lst in (("adjectives", "adj"), ("animals", "animal"), ("japanese", "jp")):
        seen = set()
        for line in (nick / f"{name}.words").read_text(encoding="utf-8").splitlines():
            w = re.sub(r"[^a-z0-9]", "", line.strip().lower())
            if 2 <= len(w) <= 14 and w not in seen:
                seen.add(w)
                rows.append((lst, w))
    return rows


def write(folder, name, header, rows):
    folder.mkdir(parents=True, exist_ok=True)
    with open(folder / name, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(header) + "\n")
        for r in rows:
            f.write("\t".join(map(str, r)) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="resources")
    ap.add_argument("--faker", default="build/faker")
    ap.add_argument("--nicknames", default="../nickname-generator/resources/lite")
    ap.add_argument("--countries", default="../country-generator/resources")
    args = ap.parse_args()

    faker = faker_domains(Path(args.faker))
    providers = provider_rows(faker)
    styles = style_rows()
    words = word_rows(Path(args.nicknames))
    fold = fold_rows()
    particles = sorted(set(PARTICLES))
    mapping = {c: prof for prof, cs in PROFILES.items() for c in cs.split()}

    for tier in ("lite", "full"):
        cfile = Path(args.countries) / tier / "countries.tsv"
        rows = list(csv.DictReader(open(cfile, encoding="utf-8"), delimiter="\t"))
        regions = [r["cca2"] for r in rows]
        folder = Path(args.out) / tier
        write(folder, "countries.tsv", ["region", "population"], [[r["cca2"], r["population"]] for r in rows])
        write(folder, "fold.tsv", ["from", "to"], fold)
        write(folder, "particles.tsv", ["particle"], [[p] for p in particles])
        write(folder, "profiles.tsv", ["region", "profile"], [[r, mapping.get(r, "default")] for r in regions])
        write(folder, "email_styles.tsv", ["profile", "pattern", "weight"], styles)
        keep = set(regions) | {"*"}
        write(folder, "providers.tsv", ["domain", "country", "kind", "weight"], [r for r in providers if r[1] in keep])
        write(folder, "words.tsv", ["list", "word"], words)
        print(f"{tier}: {len(regions)} profiles, {len(styles)} styles, "
              f"{sum(1 for r in providers if r[1] in keep)} providers, {len(words)} words, {len(fold)} folds")


if __name__ == "__main__":
    sys.exit(main())
