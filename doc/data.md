# Data and Method

The data in `resources/` and how email-generator uses it (rule 6 of the dasmig standard).

## Tiers

| Tier | Countries | What it holds |
| --- | --- | --- |
| `lite` | 195 | The sovereign states of `country-generator`'s `lite` tier |
| `full` | 250 | The territories of its `full` tier |

Both tiers hold the same style, particle, fold and word tables; `profiles.tsv` and `providers.tsv` list the tier's countries.

`countries.tsv` has `region` and `population`, copied from `country-generator` (the population weights the country draw).

## Files

Built by `scripts/prepare_email.py`. All are curated for this library (rule 6) except where noted; none comes from a survey of real addresses, so the shares are plausible, not measured.

| File | Columns | Source and reliability |
| --- | --- | --- |
| `fold.tsv` | `from`, `to` | Unicode decomposition of Latin-1, Latin Extended-A/B and Latin Extended Additional, plus letters that do not decompose (`ß` `ø` `æ` `œ` `đ` `ð` `þ` `ł`). Letters of other scripts are not folded |
| `particles.tsv` | `particle` | Curated list of family-name particles (`de`, `da`, `van`, `von`, `bin`, ...) that email names skip |
| `profiles.tsv` | `region`, `profile` | Curated: which style profile a country uses (`anglo`, `germanic`, `romance`, `slavic`, `eastasian`, `japan`, `arabic`, `brazil`, `default`) |
| `email_styles.tsv` | `profile`, `pattern`, `weight` | Curated. The shapes were modelled on Faker's username formats and on the name transforms of `nickname-generator`; the weights are a judgement. Separator variants (`.` `_` `-` none) are written out as rows, weighted per profile |
| `providers.tsv` | `domain`, `country`, `kind`, `weight` | Seeded from Faker's locale `free_email_domains` (MIT), curated: global webmail under `*`, local domains per country, extra domains for countries Faker lacks. A row for a country replaces the `*` row of the same domain (weight 0 switches it off there). `kind` is `free` (webmail) or `isp` |
| `words.tsv` | `list`, `word` | Copied from `nickname-generator`: `adj` (adjectives), `animal`, `jp` (romaji), lowercased to `[a-z0-9]` |

**Pattern language** (`email_styles.tsv`): `{f}` given name, `{l}` family name, `{fi}` `{li}` their initials, `{n}` nickname, `{y2}` `{y4}` birth year, `{r}` one to three random digits, `{r2}` two, `{adj}` `{animal}` `{jp}` a handle word, anything else a literal from `[a-z0-9._-]`. A style whose inputs are missing (no family name, no birth year, ...) is not eligible, so a call with no names draws among the handle styles.

**Names** are folded to lowercase ASCII; the given part is the first word, the family part the first word that is not a particle (so `de la Cruz` is `cruz`, `Müller-Schmidt` is `muller`). A name in a script with no ASCII form leaves no part, and a handle style is used.

## Generation Model

A country (by population, stream `"country"`), a style of the country's profile among those the request can fill (`"style"`, by weight), handle words (`"local"`), digits (`"number"`) and a domain (`"domain"`, by weight; the reserved `example.*` domains with `safe`). Digits in a style are the birth year (`{y2}`, `{y4}`) or random (`{r}`, `{r2}`).

## Rebuilding the Data

`python scripts/prepare_email.py` writes every table to `resources/lite` and `resources/full`. It reads Faker's per-locale `free_email_domains` (downloaded into `build/faker/`), the word lists from `../nickname-generator/resources/lite` and the countries from `../country-generator/resources`. All weights and the curated tables live in the script.

## Known Limitations

- The shares are plausible, not measured: no survey of real addresses was used. Treat the mix of styles and providers as realistic-looking, not statistically exact.
- Faker's locale lists cover 32 countries; about 40 more were added by hand from general knowledge and have not been verified domain by domain. Other countries use the global webmail only.
- Names in scripts other than Latin are not transliterated; pass the Latin form.
- Only the first word of a given name and the first non-particle word of a family name are used.
- The handle words are English adjectives and animals (and Japanese romaji for Japan), whatever the country.
- Provider shares are one set per country; they do not vary with age.
