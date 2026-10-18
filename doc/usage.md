# Usage Guide

This guide covers every feature of the email-generator library. For a quick overview, see the [README](../README.md). For the data and how it was built, see [Data and Method](data.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating">Generating</a></li>
<li><a href="#safe-addresses-for-tests">Safe Addresses for Tests</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#batch-generation">Batch Generation</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/emailgen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::emg::instance();
    std::cout << gen.get_email({.given = "Ana", .family = "Souza"}) << '\n';
    // ana.souza@gmail.com
}
```

## Installation

Copy `dasmig/emailgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. Supply `resources/` next to the program, or define `DASMIG_EMAIL_EMBED` to compile the `lite` tier in.

## Loading Resources

`instance()` loads the `lite` tier on first use (or `full` if there is no `lite`), from the compiled-in copy or from the first of `resources/`, `../resources`, `email-generator/resources` and `../email-generator/resources` that has it. `lite` holds the 195 sovereign states, `full` the territories as well.

```cpp
dasmig::emg gen;                            // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite);  // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();
gen.country_count();
gen.countries();             // ["AD", "AE", ...]: the countries an address can be generated for
gen.has_country("br");
```

Each `load` replaces the data and every layer loaded before.

## Generating

```cpp
auto e = gen.get_email({.country = "DE", .given = "Zoë", .family = "Müller",
                        .nickname = "zm", .birth_year = 1988});
e.address();   // "zoe.muller88@web.de"
e.local;       // "zoe.muller88"
e.domain;      // "web.de"
e.kind;        // "free" (webmail) or "isp"; empty for a reserved domain
e.reserved;    // false
e.country_code; // "DE" (ISO 3166-1 alpha-2, as in the other generators)
```

- **Styles.** The country maps to a profile (Anglo, Germanic, Romance, Slavic, East Asian, Japan, Arabic, Brazil, default) that weights the shapes of the local part: `first.last`, `flast`, `last_first`, `first85`, `first.last1988`, the nickname alone, handles such as `quietfox42`. Only the styles the request can fill are eligible: a call with no names draws among the handle styles, a call with no birth year never produces a year.
- **Names** are plain UTF-8 values. They are folded to ASCII (`Ø` is `o`, `ß` is `ss`), the given name is its first word, the family name its first word that is not a particle (`de la Cruz` is `cruz`, `Müller-Schmidt` is `muller`). A name in a script with no ASCII form is dropped; pass its Latin form to use it.
- **Local part.** Always `[a-z0-9._-]`, no leading, trailing or doubled marks, at most 64 characters.
- **Domains** are the country's own providers and the global webmail, by weight.
- **Country.** Absent, it is drawn by population. `weighted = false` draws countries, styles and domains uniformly.

## Safe Addresses for Tests

`safe = true` makes an address that cannot reach a real person: the domain is `example.com`, `example.net` or `example.org` (RFC 2606), `reserved` is true and `kind` is empty. `phone.reserved` means the same for numbers. The local part is unchanged.

## Seeding and Deterministic Generation

- `get_email(request, seed)` gives the same result for the same request, seed and data, on every platform, without advancing the instance engine.
- `get_email(request)` draws a fresh seed from the instance engine, initially seeded from system entropy. There are no generator-level `seed()` or `unseed()` controls; for deterministic sequences, keep a `dasmig::core::rng` in the caller and pass its next value as the seed.
- `e.seed()` is the seed a result was generated from: it replays it with the same request and data.
- The call draws from streams split from the call seed (`"country"`, `"style"`, `"local"`, `"number"`, `"domain"`), so adding a field later does not change the others.

## Extending Data

The shipped data is a base layer in seven tables (see [Data and Method](data.md)): `countries`, `fold`, `particles`, `profiles`, `email_styles`, `providers` and `words`. A layer adds, changes or removes rows of one table without editing the shipped files.

```cpp
using dasmig::email_table;

// Add a webmail domain for one country, from text.
gen.extend_from_string(email_table::providers,
    "domain\tcountry\tkind\tweight\nmail.example\tBR\tfree\t3\n", "scenario");

// Switch a global domain off in one country (its row for that country replaces the global one).
gen.extend_from_string(email_table::providers,
    "domain\tcountry\tkind\tweight\ngmail.com\tCN\tfree\t0\n", "cn");

// A new style for one profile.
gen.extend_from_string(email_table::email_styles, "profile\tpattern\tweight\nbrazil\t{l}-{f}\t5\n", "styles");

// Remove rows: a layer needs only the key and a disabled column.
gen.extend_from_string(email_table::providers, "domain\tcountry\tdisabled\nmail.example\tBR\t1\n", "cleanup");

// A whole directory of tables, or one file named like a table.
gen.extend("mods/email-data");
gen.extend("mods/providers.tsv", dasmig::core::layer_mode::add);
```

- Every value in a layer is checked before anything changes: patterns may use only the documented parts, domains must be lowercase names, profiles and regions must exist. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was.
- `layers(table)` lists what is loaded for a table: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto v = gen.get_email(dasmig::email_request{});
v.signature().seed; // the call seed
v.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume. Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

## Compiled-in Data

Define `DASMIG_EMAIL_EMBED` before including the header to compile the `lite` tier into the program. `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same `data_hash()` as the files, so seeds replay the same either way.

`dasmig/emailgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date. It is about 185 KB.

## Thread Safety

Each `emg` instance is independent. Concurrent calls to `get_email(request, seed)` on the **same** instance are safe; calls that use the instance engine (`get_email(request)`), loading and extending require external synchronisation (e.g. a `std::mutex`).

## Batch Generation

`get_email_batch` returns a `std::vector`, and `for_each_email` streams the items to a sink in index order without holding the batch.

```cpp
auto& gen = dasmig::emg::instance();
const dasmig::email_request request{.country = "US", .given = "Sam", .family = "Lee", .safe = true};

// 1,000 addresses from one batch seed, on four threads.
auto items = gen.get_email_batch(request, {.count = 1000, .threads = 4}, 42);

// Item 1 replays on its own: the batch seed and the index give its seed.
auto again = gen.get_email(request, dasmig::core::item_seed(42, 1));

// Unique addresses.
auto rule = dasmig::core::unique_rule::by<dasmig::email>([](const dasmig::email& e) { return e.address(); });
auto unique = gen.get_email_batch({}, {.count = 5000, .unique = rule}, 7);

// A million addresses to a file as JSON Lines, never all in memory.
std::ofstream out{"emails.jsonl"};
dasmig::core::jsonl_writer lines{out};
gen.for_each_email(request, {.count = 1'000'000, .threads = 0}, 42, lines);
```

- **Item seeds.** Item `i` is exactly `get_email(request, core::item_seed(batch_seed, first + i))`. A range can be generated directly (`first = 1000` starts at item 1000), two batches that follow each other equal one, and the output is the same for any `threads` (0 means the hardware count). Without a `batch_seed` one is drawn like the single getter draws its own.
- **Options.** `core::batch_options` has `count`, `first`, `threads` and `unique`. The request describes one item; the options describe the batch.
- **Uniqueness.** `.unique = core::unique_rule::by<email>(key)` regenerates an item whose key already appeared in the batch, with a deterministic retry seed, and throws `std::runtime_error` naming the item when the attempts run out. With a fixed name in one country the styles and domains give only so many distinct addresses; vary the request or leave the names out for large unique batches.
- **Sinks.** `core::jsonl_writer` writes one `to_json()` per line, `core::csv_writer` flattens each result's JSON into dotted columns. Any callable `(std::uint64_t index, email&)` works.
- Batch calls use the same const getters from several threads, which the thread-safety rules above allow; the CI runs the batch tests under ThreadSanitizer.

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error` | Any generation call before data is loaded; no style fits or no domain exists for the country; a unique batch that runs out of attempts |
| `std::invalid_argument` | An unknown country |
| `dasmig::core::tsv_error` | A malformed table or layer (message gives file, line and column); the data is unchanged |
