#include "catch_amalgamated.hpp"
#include "../dasmig/emailgen.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Recorded answers (see "Known answers" below); re-record them with the data.
// ---------------------------------------------------------------------------

#define KAT_DATA_HASH 0x2efa1c683c6caa57ULL
#define KAT_EMAIL_JSON_1 R"({"country":"ET","address":"shatteringturtle85@outlook.com","local":"shatteringturtle85","domain":"outlook.com","kind":"free","signature":{"seed":"0000000000000001","data":"2efa1c683c6caa57"}})"
#define KAT_EMAIL_JSON_2 R"({"country":"BR","address":"ana.silva90@gmail.com","local":"ana.silva90","domain":"gmail.com","kind":"free","signature":{"seed":"0000000000000002","data":"2efa1c683c6caa57"}})"
#define KAT_EMAIL_HASH 0x1202b043899743fcULL

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dasmig::emg make_loaded_generator()
{
    dasmig::emg gen;
    if (!gen.load(dasmig::dataset::lite))
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(std::filesystem::path{candidate});
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

static std::uint64_t fnv(const std::string& text)
{
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const char c : text)
    {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads the sovereign states", "[loading]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.country_count() == 195);
    REQUIRE(gen.has_country("br"));
    REQUIRE_FALSE(gen.has_country("ZZ"));
    REQUIRE(gen.layers(dasmig::email_table::countries).size() == 1);
    REQUIRE(gen.layers(dasmig::email_table::providers).size() == 1);
}

TEST_CASE("load - non-existent path is a no-op", "[loading]")
{
    dasmig::emg gen;
    gen.load(std::filesystem::path{"no/such/directory"});
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.data_hash() == 0);
}

#ifndef DASMIG_EMAIL_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for a missing tier", "[loading]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "emg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::emg gen;
    const bool loaded = gen.load(dasmig::dataset::full);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(gen.has_data());
}
#endif

TEST_CASE("the singleton loads data on first use", "[loading]")
{
    REQUIRE(dasmig::emg::instance().has_data());
}

TEST_CASE("email: to_json, signature and conversion", "[json]")
{
    const auto gen = make_loaded_generator();
    const auto v = gen.get_email({.given = "Ana", .family = "Souza"}, std::uint64_t{99});
    REQUIRE(v.signature().seed == 99);
    REQUIRE(v.seed() == 99);
    REQUIRE(v.signature().data == gen.data_hash());
    const auto json = v.to_json();
    REQUIRE(json.starts_with(R"({"country":")"));
    REQUIRE(json.find(R"("signature":{"seed":"0000000000000063")") != std::string::npos);
    dasmig::core::json_writer w;
    w.begin_array();
    v.to_json(w);
    w.end_array();
    REQUIRE(w.str() == "[" + json + "]");
    const std::string s = v;
    std::ostringstream os;
    os << v;
    REQUIRE(s == v.address());
    REQUIRE(os.str() == v.address());
}

#ifdef DASMIG_EMAIL_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "emg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::emg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers(dasmig::email_table::providers).front().source == "embedded:lite/providers.tsv");
    REQUIRE(gen.data_hash() == KAT_DATA_HASH); // same as the files
    REQUIRE(gen.get_email({}, std::uint64_t{1}).to_json() == KAT_EMAIL_JSON_1);
}
#endif

// ---------------------------------------------------------------------------
// Email
// ---------------------------------------------------------------------------

static bool valid_local(const std::string& s)
{
    static const std::regex ok{"^[a-z0-9]([a-z0-9._-]*[a-z0-9])?$"};
    static const std::regex marks{"[._-]{2}"};
    return s.size() <= 64 && std::regex_match(s, ok) && !std::regex_search(s, marks);
}

static bool valid_domain(const std::string& s)
{
    static const std::regex ok{"^([a-z0-9]([a-z0-9-]*[a-z0-9])?\\.)+[a-z]{2,}$"};
    return std::regex_match(s, ok);
}

TEST_CASE("get_email throws when no data is loaded or the country is unknown", "[email][errors]")
{
    dasmig::emg empty;
    REQUIRE_THROWS_AS(empty.get_email(dasmig::email_request{}), std::runtime_error);
    const auto gen = make_loaded_generator();
    REQUIRE_THROWS_AS(gen.get_email({.country = "ZZ"}, std::uint64_t{1}), std::invalid_argument);
}

TEST_CASE("get_email is deterministic and replays from its seed", "[email][seed]")
{
    auto gen = make_loaded_generator();
    const auto second = make_loaded_generator();
    const dasmig::email_request request{.given = "Ana", .family = "Souza", .birth_year = 1990};
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        REQUIRE(gen.get_email(request, s).to_json() == gen.get_email(request, s).to_json());
        REQUIRE(gen.get_email(request, s).to_json() == second.get_email(request, s).to_json());
    }
    const auto v = gen.get_email(request);
    REQUIRE(gen.get_email(request, v.seed()).to_json() == v.to_json());
}

TEST_CASE("every address is syntactically valid, whatever the input", "[email][validity]")
{
    const auto gen = make_loaded_generator();
    const std::vector<dasmig::email_request> requests = {
        {},
        {.given = "Ana", .family = "Souza", .birth_year = 1990},
        {.given = "José María", .family = "de la Cruz Ørsted", .nickname = "Zé Do Pão"},
        {.given = "Zoë", .family = "Müller-Schmidt", .birth_year = 2001},
        {.given = "Åsa", .family = "van der Berg"},
        {.given = "山田", .family = "太郎"},
        {.given = "  ", .family = "O'Brien", .nickname = "..__--"},
        {.nickname = "Dark_Knight!!"},
        {.given = std::string(300, 'a'), .family = std::string(300, 'b'), .nickname = std::string(300, 'c')},
        {.given = "Ana", .birth_year = 12},
    };
    for (const auto& request : requests)
    {
        for (std::uint64_t s = 0; s < 1500; ++s)
        {
            const auto e = gen.get_email(request, s);
            INFO(e.to_json());
            REQUIRE(valid_local(e.local));
            REQUIRE(valid_domain(e.domain));
            REQUIRE(e.address() == e.local + "@" + e.domain);
        }
    }
}

TEST_CASE("names are folded to ASCII and particles are skipped", "[email][fold]")
{
    const auto gen = make_loaded_generator();
    std::set<std::string> seen;
    const auto any_with = [&](const std::string& part) {
        return std::ranges::any_of(seen, [&](const std::string& l) { return l.find(part) != std::string::npos; });
    };
    for (std::uint64_t s = 0; s < 4000; ++s)
    {
        seen.insert(gen.get_email({.country = "DE", .given = "Zoë", .family = "von Müller-Ørsted ß"}, s).local);
    }
    REQUIRE(any_with("zoe"));
    REQUIRE(any_with("muller")); // von is a particle; the first part of a hyphenated name is used
    REQUIRE_FALSE(any_with("von"));
    REQUIRE_FALSE(any_with("orsted"));

    seen.clear();
    for (std::uint64_t s = 0; s < 2000; ++s)
    {
        seen.insert(gen.get_email({.country = "PT", .given = "João", .family = "da Silva"}, s).local);
    }
    REQUIRE(any_with("silva"));
    REQUIRE(any_with("joao"));
    REQUIRE_FALSE(any_with("da.s"));
}

TEST_CASE("a name in another script falls back to a handle", "[email][fold]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 500; ++s)
    {
        const auto e = gen.get_email({.country = "JP", .given = "太郎", .family = "山田"}, s);
        REQUIRE(valid_local(e.local));
    }
}

TEST_CASE("the birth year and the nickname only appear when given", "[email][inputs]")
{
    const auto gen = make_loaded_generator();
    bool year_used = false;
    bool nick_used = false;
    for (std::uint64_t s = 0; s < 3000; ++s)
    {
        const auto e = gen.get_email({.country = "US", .given = "Sam", .family = "Lee", .nickname = "skater", .birth_year = 1987}, s);
        year_used = year_used || e.local.find("87") != std::string::npos;
        nick_used = nick_used || e.local.find("skater") != std::string::npos;
    }
    REQUIRE(year_used);
    REQUIRE(nick_used);
    for (std::uint64_t s = 0; s < 3000; ++s)
    {
        const auto e = gen.get_email({.country = "US", .given = "Sam", .family = "Lee"}, s);
        REQUIRE(e.local.find("skater") == std::string::npos);
        REQUIRE(e.local.find("1987") == std::string::npos);
    }
}

TEST_CASE("safe addresses use the reserved domains", "[email][safe]")
{
    const auto gen = make_loaded_generator();
    const std::set<std::string> reserved{"example.com", "example.net", "example.org"};
    std::set<std::string> used;
    for (std::uint64_t s = 0; s < 300; ++s)
    {
        const auto e = gen.get_email({.given = "Ana", .family = "Souza", .safe = true}, s);
        REQUIRE(reserved.contains(e.domain));
        REQUIRE(e.kind == "reserved");
        used.insert(e.domain);
    }
    REQUIRE(used == reserved);
}

TEST_CASE("domains follow the country", "[email][stats]")
{
    const auto gen = make_loaded_generator();
    constexpr int draws = 4000;
    std::map<std::string, int> cn;
    std::map<std::string, int> us;
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        ++cn[gen.get_email({.country = "CN", .given = "Wei", .family = "Zhang"}, s).domain];
        ++us[gen.get_email({.country = "US", .given = "Sam", .family = "Lee"}, s).domain];
    }
    REQUIRE(cn["qq.com"] > draws / 3);
    REQUIRE(cn["yahoo.com"] == 0); // switched off for China
    REQUIRE(us["gmail.com"] > draws / 3);
    REQUIRE(us["qq.com"] == 0);
}

TEST_CASE("email: countries are drawn by population by default", "[email][stats]")
{
    const auto gen = make_loaded_generator();
    constexpr int draws = 5000;
    int india = 0;
    int iceland = 0;
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        const auto c = gen.get_email({}, s).country;
        india += c == "IN" ? 1 : 0;
        iceland += c == "IS" ? 1 : 0;
    }
    REQUIRE(india > draws / 25);
    REQUIRE(iceland < draws / 200);
}

TEST_CASE("an email layer changes the data and a bad one is rejected", "[email][extend]")
{
    using dasmig::email_table;
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();

    gen.extend_from_string(email_table::providers, "domain\tcountry\tkind\tweight\nmail.test\tBR\tfree\t1000000\n", "p.tsv");
    int hits = 0;
    for (std::uint64_t s = 0; s < 500; ++s)
    {
        hits += gen.get_email({.country = "BR", .given = "Ana", .family = "Souza"}, s).domain == "mail.test" ? 1 : 0;
    }
    REQUIRE(hits > 480);

    gen.extend_from_string(email_table::email_styles, "profile\tpattern\tweight\nbrazil\t{l}-{f}\t1000000\n", "s.tsv");
    int styled = 0;
    for (std::uint64_t s = 0; s < 500; ++s)
    {
        styled += gen.get_email({.country = "BR", .given = "Ana", .family = "Souza"}, s).local == "souza-ana" ? 1 : 0;
    }
    REQUIRE(styled > 480);

    REQUIRE(gen.load(dasmig::dataset::lite));
    REQUIRE(gen.data_hash() == hash);

    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::email_styles, "profile\tpattern\tweight\nx\t{q}\t1\n", "a.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::email_styles, "profile\tpattern\tweight\nx\tA.B\t1\n", "b.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::providers, "domain\tcountry\tkind\tweight\nnodot\t*\tfree\t1\n", "c.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::providers, "domain\tcountry\tkind\tweight\na.com\t*\tpaid\t1\n", "d.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::profiles, "region\tprofile\nBR\tnowhere\n", "e.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::words, "list\tword\nadj\tUpper\n", "f.tsv"), dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(email_table::fold, "from\tto\nab\ta\n", "g.tsv"), dasmig::core::tsv_error);
    REQUIRE(gen.data_hash() == hash);
}

TEST_CASE("email batch: items equal the single calls, and addresses can be unique", "[email][batch]")
{
    auto gen = make_loaded_generator();
    const dasmig::email_request request{.country = "DE", .given = "Hans", .family = "Meier"};
    const auto batch = gen.get_email_batch(request, {.count = 200, .threads = 3}, 11);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_email(request, dasmig::core::item_seed(11, i)).to_json());
    }

    const auto rule = dasmig::core::unique_rule::by<dasmig::email>([](const dasmig::email& e) { return e.address(); });
    const auto unique = gen.get_email_batch({}, {.count = 3000, .unique = rule}, 4);
    std::set<std::string> seen;
    for (const auto& e : unique) seen.insert(e.address());
    REQUIRE(seen.size() == unique.size());

    std::size_t n = 0;
    gen.for_each_email(request, {.count = 50}, 11, [&](std::uint64_t i, const dasmig::email& e) {
        REQUIRE(e.to_json() == batch[i].to_json());
        ++n;
    });
    REQUIRE(n == 50);
}

TEST_CASE("email: known answers", "[email][kat]")
{
    dasmig::emg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.get_email({}, std::uint64_t{1}).to_json() == KAT_EMAIL_JSON_1);
    REQUIRE(gen.get_email({.country = "BR", .given = "Ana", .family = "da Silva", .birth_year = 1990}, std::uint64_t{2}).to_json() == KAT_EMAIL_JSON_2);
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        h = dasmig::core::hash_combine(h, fnv(gen.get_email({.given = "José", .family = "Müller", .nickname = "jm", .birth_year = 1985}, s).to_json()));
    }
    REQUIRE(h == KAT_EMAIL_HASH);
}
