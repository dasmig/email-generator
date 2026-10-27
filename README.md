# Email Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Email Generator for C++](https://raw.githubusercontent.com/dasmig/email-generator/main/doc/email-generator.png)](https://github.com/dasmig/email-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/email-generator/main/LICENSE.MIT)
[![CI](https://github.com/dasmig/email-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/email-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/email-generator.svg)](https://github.com/dasmig/email-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/email-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/email-generator/)

**[API Reference](https://dasmig.github.io/email-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/email-generator/releases)**

Email addresses in each country's style, for tests, fixtures and generated users. The local part follows the country's naming habits from a given name, family name, nickname and birth year, and the domain is a provider people use there. A `safe` option uses the reserved `example.*` domains.

## Features

- **Locale-matched Local Parts**. `ana.souza`, `a.souza85`, `souza_ana`, handles: nine style profiles weight the shapes by country. Names are folded to ASCII (`Ørsted` is `orsted`) and particles skipped (`da Silva` is `silva`).

- **Provider Domains by Country**. Global webmail plus the providers of the country (`web.de`, `uol.com.br`, `qq.com`, ...), by weight.

- **Safe for Tests**. `safe` gives `example.com`, `example.net` or `example.org` (RFC 2606), so a message sent to the address reaches nobody.

- **Deterministic Seeding**. Per-call `get_email(request, seed)` for reproducible results, and `seed()` on the result for replaying a previous generation with the same request and data.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same result on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove entries (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_EMAIL_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `emg` instances with their own data and random engine.

## Integration

[`emailgen.hpp`](https://github.com/dasmig/email-generator/blob/main/dasmig/emailgen.hpp) is the library [released here](https://github.com/dasmig/email-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/email-generator/blob/main/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/email-generator/tree/main/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/emailgen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/email-generator/tree/main/resources) folder (or define `DASMIG_EMAIL_EMBED`).

## Usage

```cpp
#include <dasmig/emailgen.hpp>
#include <iostream>

auto& gen = dasmig::emg::instance(); // loads the lite tier

// The names are plain values; the country sets the style and the domain.
auto e = gen.get_email({.country = "DE", .given = "Zoë", .family = "Müller", .birth_year = 1988});
std::cout << e << '\n';        // zoe.muller88@web.de
std::cout << e.kind << '\n';   // free

// No names: a handle in the country's style.
std::cout << gen.get_email({.country = "JP"}) << '\n';

// An address that reaches nobody, for tests.
std::cout << gen.get_email({.given = "Sam", .family = "Lee", .safe = true}) << '\n'; // sam.lee@example.org

// The same seed gives the same result everywhere.
auto again = gen.get_email({.country = "DE", .given = "Zoë", .family = "Müller", .birth_year = 1988}, e.seed());
```

See the [usage guide](doc/usage.md) for everything else.

## Building

```bash
make            # example
make test       # tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/emailgen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.2 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## Related Libraries

| Library | Description |
|---------|-------------|
| [address-generator](https://github.com/dasmig/address-generator) | Postal addresses with real streets by place |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [device-generator](https://github.com/dasmig/device-generator) | Devices, browsers, user agents and IP addresses that fit together |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
| [interest-generator](https://github.com/dasmig/interest-generator) | Religion, hobbies and favourite media by country, age and sex |
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [occupation-generator](https://github.com/dasmig/occupation-generator) | Employment status, education and occupation by country, age and sex |
| [persona-generator](https://github.com/dasmig/persona-generator) | Complete characters composed from the other generators |
| [phone-generator](https://github.com/dasmig/phone-generator) | Phone numbers valid in each country's numbering plan |
| [relationship-generator](https://github.com/dasmig/relationship-generator) | Families around a person: parents, siblings, partners and children |
| [trait-generator](https://github.com/dasmig/trait-generator) | Personality traits with conflicts, synergies and Big Five axes |
| [user-generator](https://github.com/dasmig/user-generator) | Complete user records: account, username, email, phone and address |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2026 Diego Dasso Migotto
```
