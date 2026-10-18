// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_EMAILGEN_HPP
#define DASMIG_EMAILGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef DASMIG_EMAIL_EMBED
#include "emailgen_data.hpp"
#endif

/// @file emailgen.hpp
/// @brief Email Generator library — email addresses in each country's style for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 1);

namespace dasmig
{

/// @brief What an email call asks for.
///
/// Every generation call takes one; `email_request{}` asks for the defaults.
/// Choices are never stored on the generator, so one call cannot change the
/// next. The names are plain values in UTF-8: pass the Latin form of a name
/// written in another script, or nothing, and a handle style is used.
struct email_request
{
    /// @brief ISO-3166 two-letter country code (any case), or a country
    ///        drawn when absent. The country sets the style of the local part
    ///        and the provider domains.
    std::optional<std::string> country{std::nullopt};

    std::string given{};    ///< Given name; empty when unknown. // NOLINT(readability-redundant-member-init): keeps -Wextra quiet for partial designated initializers
    std::string family{};   ///< Family name; empty when unknown. // NOLINT(readability-redundant-member-init): keeps -Wextra quiet for partial designated initializers
    std::string nickname{}; ///< Nickname or handle; empty when unknown. // NOLINT(readability-redundant-member-init): keeps -Wextra quiet for partial designated initializers

    /// @brief Birth year, for the `ana85` and `ana1985` styles; those styles
    ///        are left out when absent.
    std::optional<int> birth_year{std::nullopt};

    /// @brief Use a reserved domain (`example.com`, `.net`, `.org`, RFC
    ///        2606), so a message sent to the address reaches nobody.
    bool safe{false};

    /// @brief Weighted selection (default): countries by population, styles
    ///        and domains by their weights. False selects uniformly.
    bool weighted{true};
};

/// @brief Return type for email generation.
///
/// Supports implicit conversion to std::string (the address), streaming via
/// operator<< and JSON output via to_json().
class email
{
  public:
    std::string country; ///< ISO 3166-1 alpha-2 of the country whose style the address follows.
    std::string local;   ///< The part before the `@`: `[a-z0-9._-]`, at most 64 characters.
    std::string domain;  ///< The part after the `@`.
    std::string kind;    ///< `free` (webmail), `isp` (a provider of the country) or `reserved` (RFC 2606).

    /// @brief The address.
    /// @return `local@domain`.
    [[nodiscard]] std::string address() const { return local + "@" + domain; }

    /// @brief Retrieve the random seed used to generate this address.
    /// @return The call seed; pass it to get_email(request, seed) with the
    ///         same request and data to replay it.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this address was generated from.
    /// @return The replay signature; `data` is emg::data_hash() at the time
    ///         of generation.
    [[nodiscard]] const core::seed_signature& signature() const { return _signature; }

    /// @brief Human-readable summary string.
    /// @return The address.
    [[nodiscard]] std::string to_string() const { return address(); }

    /// @brief Write this address as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("country", country)
            .field("address", address())
            .field("local", local)
            .field("domain", domain)
            .field("kind", kind)
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This address as a JSON document.
    /// @return Compact JSON, for example `{"country":"DE","address":"...",...}`.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return The address.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return to_string();
    }

    /// @brief Stream the address to an output stream.
    /// @param os Output stream.
    /// @param v The address to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const email& v)
    {
        os << v.to_string();
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class emg;
};

/// @brief The tables of the email data, for extending it.
enum class email_table : std::uint8_t
{
    countries,    ///< `countries.tsv`: the countries and their populations.
    fold,         ///< `fold.tsv`: letters and their ASCII form.
    particles,    ///< `particles.tsv`: family-name particles email names skip.
    profiles,     ///< `profiles.tsv`: the style profile of each country.
    email_styles, ///< `email_styles.tsv`: the shapes of an email's local part, by profile.
    providers,    ///< `providers.tsv`: the domains addresses use, by country.
    words         ///< `words.tsv`: the words of the handle styles.
};

/// @brief Email Generator: email addresses in each country's style.
///
/// A call draws a country (by population), a style of the country's profile
/// (`"style"`), the handle words (`"local"`), the digits (`"number"`) and the
/// domain (`"domain"`), each from a stream split from the call seed. Output is
/// identical on every compiler and platform (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed
/// independently.
///
/// @par Thread safety
/// Each instance is independent.  Concurrent calls to get_phone(request, seed) on the
/// **same** instance are safe; calls that use the instance engine, loading and
/// extending require external synchronisation.
class emg
{
  public:
    /// @brief Default constructor — creates an empty generator with no
    ///        data.
    emg() : _tables{make_tables_()} {}

    emg(const emg&) = delete;            ///< Not copyable.
    emg& operator=(const emg&) = delete; ///< Not copyable.
    emg(emg&&) noexcept = default;       ///< Movable.
    emg& operator=(emg&&) noexcept = default; ///< Movable.
    ~emg() = default;                       ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static emg& instance()
    {
        static emg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Email ------------------------------------------------------------

    /// @brief Generate a random email address.
    ///        Use email_request{} for the defaults.
    /// @param request What to generate.
    /// @return The address.
    /// @throws std::runtime_error If no email data has been loaded.
    /// @throws std::invalid_argument If the country is unknown.
    [[nodiscard]] email get_email(const email_request& request) { return get_email(request, draw_seed_()); }

    /// @brief Generate a deterministic email address.
    /// @param request What to generate.
    /// @param call_seed Seed; the same request, seed and data give the same
    ///        result. Does not touch the instance engine.
    /// @return The address.
    /// @throws std::runtime_error If no email data has been loaded.
    /// @throws std::invalid_argument If the country is unknown.
    [[nodiscard]] email get_email(const email_request& request, std::uint64_t call_seed) const
    {
        require_data_();
        if (_model.profiles.empty()) throw std::runtime_error("No email data loaded. Call load() first.");
        const core::rng call{call_seed};

        std::size_t ci = 0;
        if (request.country)
        {
            const auto it = _model.by_region.find(upper_(*request.country));
            if (it == _model.by_region.end())
            {
                throw std::invalid_argument("get_email: unknown country '" + *request.country + "'");
            }
            ci = it->second;
        }
        else
        {
            auto cr = call.split("country");
            ci = request.weighted ? _model.all_population.pick(cr) : _model.all_uniform.pick(cr);
        }
        return make_email_(request, _model.countries[ci], call_seed, call);
    }

    /// @brief Generate a batch of email results from a seed drawn like
    ///        get_email() draws its own.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<email> get_email_batch(const email_request& request,
                                                     const core::batch_options& options)
    {
        return get_email_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of email results, deterministically.
    ///
    /// Item `i` is exactly `get_email(request, core::item_seed(batch_seed,
    /// options.first + i))`; see get_phone_batch().
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (the rule
    ///        must be made for `email`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<email> get_email_batch(const email_request& request,
                                                     const core::batch_options& options,
                                                     std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed,
                               [&](std::uint64_t item_seed) { return get_email(request, item_seed); });
    }

    /// @brief Stream a batch of email results to a sink without holding the
    ///        whole batch.
    /// @tparam Sink Callable taking `(std::uint64_t, email&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_email(const email_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_email(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of email results with a batch seed drawn like
    ///        get_email() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, email&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_email(const email_request& request, const core::batch_options& options, Sink&& sink)
    {
        for_each_email(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when at least one country is loaded.
    [[nodiscard]] bool has_data() const { return !_model.countries.empty(); }

    /// @brief Return the number of countries loaded.
    /// @return The count.
    [[nodiscard]] std::size_t country_count() const { return _model.countries.size(); }

    /// @brief Whether a country is loaded.
    /// @param country ISO-3166 two-letter code (any case).
    /// @return True when countries.tsv has the country.
    [[nodiscard]] bool has_country(std::string_view country) const
    {
        return _model.by_region.contains(upper_(country));
    }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// The directory must contain `countries.tsv`; the other tables are read
    /// when present. A path without `countries.tsv` is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(email_table::countries), ec))
        {
            return;
        }
        apply_(read_directory_(dir), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_EMAIL_EMBED`, otherwise searches `resources/`, `../resources`,
    /// `email-generator/resources` and `../email-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(email_table::countries)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)},
                                                     embedded_files_(), root);
        }
        if (!docs.at(index_(email_table::countries))) return false;
        apply_(std::move(docs), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of the table files (see email_table); all
    /// are applied together or, if any fails, none. A file must have one of
    /// those names.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                (void)extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected one of the email tables (countries.tsv, fold.tsv, "
                                    "particles.tsv, profiles.tsv, email_styles.tsv, providers.tsv, words.tsv), got " +
                                    path.filename().string());
    }

    /// @brief Add a data layer to one table.
    ///
    /// Rows are keyed by the table's key columns (see doc/data.md). In the
    /// default override mode a row replaces the loaded row with the same key
    /// and new keys are added; a row with `disabled` set to 1 removes its key.
    ///
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did (rows added, replaced, removed, warnings).
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(email_table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text (for example
    ///        from a WebAssembly host).
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(email_table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(email_table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data (all tables), recorded in each
    ///        result's signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            h = core::hash_combine(h, t.data_hash());
        }
        return any ? h : 0;
    }

  private:
    using table = email_table;

    // -- Tables -------------------------------------------------------------

    static constexpr std::string_view library_name_ = "email-generator";
    static constexpr std::size_t table_count_ = 7;
    static constexpr std::array<table, table_count_> all_tables_{
        table::countries, table::fold, table::particles, table::profiles,
        table::email_styles, table::providers, table::words};

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        switch (t)
        {
        case table::countries: return "countries.tsv";
        case table::fold: return "fold.tsv";
        case table::particles: return "particles.tsv";
        case table::profiles: return "profiles.tsv";
        case table::email_styles: return "email_styles.tsv";
        case table::providers: return "providers.tsv";
        case table::words: return "words.tsv";
        }
        return "";
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::countries:
            return {.key = {"region"}, .required = {}, .optional = {"population"}};
        case table::fold: return {.key = {"from"}, .required = {"to"}, .optional = {}};
        case table::particles: return {.key = {"particle"}, .required = {}, .optional = {}};
        case table::profiles: return {.key = {"region"}, .required = {"profile"}, .optional = {}};
        case table::email_styles: return {.key = {"profile", "pattern"}, .required = {"weight"}, .optional = {}};
        case table::providers: return {.key = {"domain", "country"}, .required = {"kind", "weight"}, .optional = {}};
        case table::words: return {.key = {"list", "word"}, .required = {}, .optional = {}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::countries)},
                core::layered_table{schema_(table::fold)}, core::layered_table{schema_(table::particles)},
                core::layered_table{schema_(table::profiles)}, core::layered_table{schema_(table::email_styles)},
                core::layered_table{schema_(table::providers)}, core::layered_table{schema_(table::words)}};
    }

    // -- Parsed rows --------------------------------------------------------

    static constexpr std::size_t none_ = static_cast<std::size_t>(-1);
    // The pieces of an email pattern.
    enum class tok : std::uint8_t { lit, f, l, fi, li, n, y2, y4, r, r2, adj, animal, jp };

    struct token
    {
        tok kind{tok::lit};
        std::string text; // the characters of a literal
    };

    struct style_data
    {
        std::vector<token> tokens;
        double weight{0.0};
        bool needs_given{false};
        bool needs_family{false};
        bool needs_nickname{false};
        bool needs_year{false};
        std::array<bool, 3> needs_words{};
    };

    struct provider_data
    {
        std::string domain;
        std::string kind;
        double weight{0.0};
    };

    struct country_data
    {
        std::string region;
        double population{1.0};
        std::size_t profile{none_};
        std::vector<provider_data> providers;
        core::weighted_table<std::size_t> provider_weights; // index into providers
    };

    struct model
    {
        std::vector<country_data> countries;
        std::unordered_map<std::string, std::size_t> by_region;
        std::vector<std::vector<style_data>> profiles;               // styles by profile
        std::unordered_map<std::string, std::size_t> profile_index;
        std::unordered_map<std::uint32_t, std::string> fold;         // code point -> ASCII
        std::unordered_set<std::string> particles;
        std::array<std::vector<std::string>, 3> words;               // adj, animal, jp
        core::weighted_table<std::size_t> all_population;            // every country
        core::weighted_table<std::size_t> all_uniform;
    };

    model _model;
    tables _tables;
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit emg(auto_probe_tag /*tag*/) : _tables{make_tables_()}
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    // -- Helpers ----------------------------------------------------------

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_EMAIL_EMBED
        return embedded::emailgen::files;
#else
        return {};
#endif
    }

    std::uint64_t draw_seed_() { return _engine(); }

    static std::string upper_(std::string_view s)
    {
        std::string out{s};
        for (auto& c : out)
        {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return out;
    }

    // An ASCII character as names fold it: letters in lowercase, digits kept; a mark stays in a
    // nickname (@p keep_marks) and anything else is dropped there ('\0'); in a name every other
    // character is a space between parts.
    static char fold_ascii_(char c, bool keep_marks)
    {
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return c;
        if (keep_marks) return is_mark_(c) ? c : '\0';
        return ' ';
    }

    // Folds text to lowercase ASCII (see fold_ascii_); a letter with no ASCII form is dropped in a
    // nickname and is a space in a name.
    [[nodiscard]] std::string fold_(std::string_view text, bool keep_marks) const
    {
        std::string out;
        for (std::size_t i = 0; i < text.size();)
        {
            std::size_t used = 1;
            const auto cp = decode_(text, i, used);
            i += used;
            if (cp != 0 && cp < 0x80)
            {
                const char c = fold_ascii_(static_cast<char>(cp), keep_marks);
                if (c != '\0') out += c;
                continue;
            }
            const auto f = cp == 0 ? _model.fold.end() : _model.fold.find(cp);
            if (f != _model.fold.end())
            {
                out += f->second;
            }
            else if (!keep_marks)
            {
                out += ' ';
            }
        }
        return out;
    }

    static std::vector<std::string> split_words_(const std::string& text)
    {
        std::vector<std::string> out;
        std::string cur;
        for (const char c : text)
        {
            if (c == ' ')
            {
                if (!cur.empty()) out.push_back(std::move(cur));
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        if (!cur.empty()) out.push_back(std::move(cur));
        return out;
    }

    // First part of a given name; first part of a family name that is not a particle.
    [[nodiscard]] std::string name_part_(std::string_view text, bool family) const
    {
        auto parts = split_words_(fold_(text, false));
        if (family)
        {
            std::erase_if(parts, [&](const std::string& p) { return _model.particles.contains(p); });
        }
        return parts.empty() ? std::string{} : parts.front();
    }

    static bool is_mark_(char c) { return c == '.' || c == '_' || c == '-'; }

    // Keeps [a-z0-9._-], one mark at a time, none at the ends, 64 characters at most.
    static std::string tidy_local_(std::string_view text)
    {
        std::string out;
        for (const char c : text)
        {
            if (is_mark_(c) && (out.empty() || is_mark_(out.back()))) continue;
            out += c;
            if (out.size() == 64) break;
        }
        while (!out.empty() && is_mark_(out.back())) out.pop_back();
        return out;
    }

    static void append_digits_(std::string& out, core::rng& r, int count)
    {
        for (int i = 0; i < count; ++i) out += static_cast<char>('0' + core::uniform_int(r, 0, 9));
    }

    // What the request gives for the local part, folded.
    struct name_inputs
    {
        std::string given;
        std::string family;
        std::string nick;
        bool year_ok{false};
    };

    [[nodiscard]] name_inputs read_names_(const email_request& request) const
    {
        name_inputs n;
        n.given = name_part_(request.given, false);
        n.family = name_part_(request.family, true);
        n.nick = tidy_local_(fold_(request.nickname, true));
        n.year_ok = request.birth_year && *request.birth_year >= 1000 && *request.birth_year <= 9999;
        return n;
    }

    // Whether the inputs can fill the style; a weight of 0 switches a style off.
    [[nodiscard]] bool style_fits_(const style_data& st, const name_inputs& n) const
    {
        const bool names = (!st.needs_given || !n.given.empty()) && (!st.needs_family || !n.family.empty()) &&
                           (!st.needs_nickname || !n.nick.empty()) && (!st.needs_year || n.year_ok);
        bool words = true;
        for (std::size_t list = 0; list < st.needs_words.size(); ++list)
        {
            words = words && (!st.needs_words.at(list) || !_model.words.at(list).empty());
        }
        return names && words && st.weight > 0.0;
    }

    // A style of the profile the inputs allow, by weight (or evenly when not weighted).
    [[nodiscard]] const style_data& pick_style_(const std::vector<style_data>& styles, const name_inputs& n,
                                                bool weighted, const core::rng& call,
                                                const std::string& region) const
    {
        const auto share = [&](const style_data& st) { return weighted ? st.weight : 1.0; };
        double total = 0.0;
        const style_data* last = nullptr;
        for (const auto& st : styles)
        {
            if (!style_fits_(st, n)) continue;
            total += share(st);
            last = &st;
        }
        if (last == nullptr) throw std::runtime_error("get_email: no style fits the request in the profile of " + region);
        auto sr = call.split("style");
        double x = core::uniform01(sr) * total;
        for (const auto& st : styles)
        {
            if (!style_fits_(st, n)) continue;
            x -= share(st);
            if (x < 0.0) return st;
        }
        return *last;
    }

    // The local part: the style's pieces filled in, then tidied.
    [[nodiscard]] std::string render_local_(const style_data& style, const name_inputs& n, const email_request& request,
                                            const core::rng& call) const
    {
        auto lr = call.split("local");
        auto nr = call.split("number");
        const auto initial = [](const std::string& text) { return text.substr(0, 1); };
        std::string local;
        for (const auto& t : style.tokens)
        {
            switch (t.kind)
            {
            case tok::lit: local += t.text; break;
            case tok::f: local += n.given; break;
            case tok::l: local += n.family; break;
            case tok::fi: local += initial(n.given); break;
            case tok::li: local += initial(n.family); break;
            case tok::n: local += n.nick; break;
            case tok::y2: local += std::format("{:02}", *request.birth_year % 100); break;
            case tok::y4: local += std::format("{:04}", *request.birth_year); break;
            case tok::r: append_digits_(local, nr, core::uniform_int(nr, 1, 3)); break;
            case tok::r2: append_digits_(local, nr, 2); break;
            case tok::adj: local += pick_word_(0, lr); break;
            case tok::animal: local += pick_word_(1, lr); break;
            case tok::jp: local += pick_word_(2, lr); break;
            }
        }
        local = tidy_local_(local);
        return local.empty() ? "user" : local;
    }

    // The domain: a reserved one with safe, else one of the country's providers.
    static void pick_domain_(email& v, const email_request& request, const country_data& country, const core::rng& call)
    {
        auto dr = call.split("domain");
        if (request.safe)
        {
            static constexpr std::array<std::string_view, 3> reserved{"example.com", "example.net", "example.org"};
            v.domain = std::string{reserved.at(core::uniform_int<std::size_t>(dr, 0, reserved.size() - 1))};
            v.kind = "reserved";
            return;
        }
        if (country.providers.empty() || (request.weighted && country.provider_weights.empty()))
        {
            throw std::runtime_error("get_email: no provider domain for " + country.region);
        }
        const auto& p = country.providers[request.weighted ? country.provider_weights.pick(dr)
                                                           : core::uniform_int<std::size_t>(dr, 0, country.providers.size() - 1)];
        v.domain = p.domain;
        v.kind = p.kind;
    }

    [[nodiscard]] email make_email_(const email_request& request, const country_data& country, std::uint64_t call_seed,
                                    const core::rng& call) const
    {
        const std::size_t pi = country.profile != none_ ? country.profile : default_profile_();
        if (pi == none_ || _model.profiles[pi].empty()) throw std::runtime_error("No email styles loaded for " + country.region);
        const auto names = read_names_(request);
        const auto& style = pick_style_(_model.profiles[pi], names, request.weighted, call, country.region);

        email v;
        v._signature = {.seed = call_seed, .data = data_hash()};
        v.country = country.region;
        v.local = render_local_(style, names, request, call);
        pick_domain_(v, request, country, call);
        return v;
    }

    [[nodiscard]] std::size_t default_profile_() const
    {
        const auto it = _model.profile_index.find("default");
        return it == _model.profile_index.end() ? none_ : it->second;
    }

    const std::string& pick_word_(std::size_t list, core::rng& r) const
    {
        const auto& w = _model.words.at(list);
        return w[core::uniform_int<std::size_t>(r, 0, w.size() - 1)];
    }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec))
            {
                docs.at(index_(t)) = core::tsv_document::load(file);
            }
        }
        return docs;
    }

    // Apply layers to copies of the tables, then commit: all or nothing.
    // With @p reset (load), tables without a document are emptied.
    void apply_(documents docs, core::layer_mode mode, bool reset)
    {
        tables next = _tables;
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        auto built = build_model_(next);
        _tables = std::move(next);
        _model = std::move(built);
    }

    // -- Row checks and the model -------------------------------------------

    static void require_region_(const core::tsv_row& row)
    {
        if (row["region"].size() != 2) throw row.error("region", "must be an ISO 3166-1 alpha-2 code");
    }

    static void require_chars_(const core::tsv_row& row, std::string_view column, bool dots, std::string_view message)
    {
        for (const char c : row[column])
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (dots && (c == '.' || c == '-'));
            if (!ok) throw row.error(column, std::string{message});
        }
    }

    static void validate_provider_(const core::tsv_row& row)
    {
        const auto domain = row["domain"];
        if (!domain.contains('.') || domain.front() == '.' || domain.back() == '.')
        {
            throw row.error("domain", "must be a domain name, got '" + std::string{domain} + "'");
        }
        require_chars_(row, "domain", true, "must be lowercase letters, digits, dots and hyphens");
        if (row["country"] != "*" && row["country"].size() != 2)
        {
            throw row.error("country", "must be '*' or an ISO 3166-1 alpha-2 code");
        }
        if (row["kind"] != "free" && row["kind"] != "isp") throw row.error("kind", "must be 'free' or 'isp'");
        if (!(row.get<double>("weight") >= 0.0)) throw row.error("weight", "must not be negative");
    }

    static void validate_(table t, const core::tsv_row& row)
    {
        switch (t)
        {
        case table::countries:
            require_region_(row);
            if (row.get_or<double>("population", 1.0) < 0.0) throw row.error("population", "must not be negative");
            break;
        case table::fold:
            if (decode_one_(row["from"]) == 0) throw row.error("from", "must be one character");
            for (const char c : row["to"])
            {
                if (c < 'a' || c > 'z') throw row.error("to", "must be lowercase ASCII letters");
            }
            break;
        case table::particles:
            if (row["particle"].empty()) throw row.error("particle", "must not be empty");
            break;
        case table::profiles:
            require_region_(row);
            if (row["profile"].empty()) throw row.error("profile", "must not be empty");
            break;
        case table::email_styles:
            if (row["profile"].empty()) throw row.error("profile", "must not be empty");
            (void)parse_style_(row);
            break;
        case table::providers:
            validate_provider_(row);
            break;
        case table::words:
            if (word_list_(row) == none_) throw row.error("list", "must be 'adj', 'animal' or 'jp'");
            if (row["word"].empty()) throw row.error("word", "must not be empty");
            require_chars_(row, "word", false, "must be lowercase letters and digits");
            break;
        }
    }

    static std::size_t word_list_(const core::tsv_row& row)
    {
        const auto l = row["list"];
        if (l == "adj") return 0;
        if (l == "animal") return 1;
        if (l == "jp") return 2;
        return none_;
    }

    // The code point of a one-character UTF-8 string; 0 when it is not exactly one.
    static std::uint32_t decode_one_(std::string_view text)
    {
        if (text.empty()) return 0;
        std::size_t used = 0;
        const auto cp = decode_(text, 0, used);
        return used == text.size() ? cp : 0;
    }

    // Decodes the character at @p i; @p used is its length (1 for a bad byte, with 0 returned).
    static std::uint32_t decode_(std::string_view text, std::size_t i, std::size_t& used)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        used = 1;
        if (c < 0x80) return c;
        std::size_t n = 0;
        std::uint32_t cp = 0;
        if ((c & 0xE0U) == 0xC0U)
        {
            n = 2;
            cp = c & 0x1FU;
        }
        else if ((c & 0xF0U) == 0xE0U)
        {
            n = 3;
            cp = c & 0x0FU;
        }
        else if ((c & 0xF8U) == 0xF0U)
        {
            n = 4;
            cp = c & 0x07U;
        }
        else
        {
            return 0;
        }
        if (i + n > text.size()) return 0;
        for (std::size_t k = 1; k < n; ++k)
        {
            const auto b = static_cast<unsigned char>(text[i + k]);
            if ((b & 0xC0U) != 0x80U) return 0;
            cp = (cp << 6U) | (b & 0x3FU);
        }
        used = n;
        return cp;
    }

    // The pattern part named @p name, or false when there is none; notes in @p st what it needs.
    static bool note_part_(std::string_view name, style_data& st, tok& kind)
    {
        using part = std::pair<std::string_view, tok>;
        static constexpr std::array<part, 12> parts{{{"f", tok::f}, {"l", tok::l}, {"fi", tok::fi}, {"li", tok::li},
                                                     {"n", tok::n}, {"y2", tok::y2}, {"y4", tok::y4}, {"r", tok::r},
                                                     {"r2", tok::r2}, {"adj", tok::adj}, {"animal", tok::animal},
                                                     {"jp", tok::jp}}};
        const auto it = std::ranges::find(parts, name, &part::first);
        if (it == parts.end()) return false;
        kind = it->second;
        switch (kind)
        {
        case tok::f:
        case tok::fi: st.needs_given = true; break;
        case tok::l:
        case tok::li: st.needs_family = true; break;
        case tok::n: st.needs_nickname = true; break;
        case tok::y2:
        case tok::y4: st.needs_year = true; break;
        case tok::adj: st.needs_words[0] = true; break;
        case tok::animal: st.needs_words[1] = true; break;
        case tok::jp: st.needs_words[2] = true; break;
        case tok::lit:
        case tok::r:
        case tok::r2: break;
        }
        return true;
    }

    static style_data parse_style_(const core::tsv_row& row)
    {
        style_data st;
        const auto text = row["pattern"];
        st.weight = row.get<double>("weight");
        if (!(st.weight >= 0.0)) throw row.error("weight", "must not be negative");
        std::string lit;
        const auto flush = [&] {
            if (!lit.empty()) st.tokens.push_back({.kind = tok::lit, .text = std::move(lit)});
            lit.clear();
        };
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c != '{')
            {
                const bool literal = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                if (!literal) throw row.error("pattern", std::string{"unexpected '"} + c + "' in '" + std::string{text} + "'");
                lit += c;
                continue;
            }
            const auto close = text.find('}', i);
            if (close == std::string_view::npos) throw row.error("pattern", "unclosed '{'");
            const auto name = text.substr(i + 1, close - i - 1);
            flush();
            tok kind{};
            if (!note_part_(name, st, kind)) throw row.error("pattern", "unknown part '{" + std::string{name} + "}'");
            st.tokens.push_back({.kind = kind, .text = {}});
            i = close;
        }
        flush();
        if (st.tokens.empty()) throw row.error("pattern", "must not be empty");
        return st;
    }

    static void read_countries_(model& m, const tables& tbls)
    {
        for (const auto& row : tbls.at(index_(table::countries)).rows())
        {
            country_data c;
            c.region = upper_(row["region"]);
            c.population = std::max(row.get_or<double>("population", 1.0), 1.0);
            m.by_region.emplace(c.region, m.countries.size());
            m.countries.push_back(std::move(c));
        }
        for (std::size_t i = 0; i < m.countries.size(); ++i)
        {
            m.all_population.add(i, m.countries[i].population);
            m.all_uniform.add(i, 1.0);
        }
    }

    static void read_text_tables_(model& m, const tables& tbls)
    {
        for (const auto& row : tbls.at(index_(table::fold)).rows())
        {
            m.fold[decode_one_(row["from"])] = std::string{row["to"]};
        }
        for (const auto& row : tbls.at(index_(table::particles)).rows()) m.particles.insert(std::string{row["particle"]});
        for (const auto& row : tbls.at(index_(table::words)).rows())
        {
            m.words.at(word_list_(row)).emplace_back(row["word"]);
        }
    }

    static void read_styles_(model& m, const tables& tbls)
    {
        for (const auto& row : tbls.at(index_(table::email_styles)).rows())
        {
            const std::string name{row["profile"]};
            auto it = m.profile_index.find(name);
            if (it == m.profile_index.end())
            {
                it = m.profile_index.emplace(name, m.profiles.size()).first;
                m.profiles.emplace_back();
            }
            m.profiles[it->second].push_back(parse_style_(row));
        }
        for (const auto& row : tbls.at(index_(table::profiles)).rows())
        {
            const auto it = m.by_region.find(upper_(row["region"]));
            if (it == m.by_region.end())
            {
                throw row.error("region", "no country '" + std::string{row["region"]} + "' in countries.tsv");
            }
            const auto pi = m.profile_index.find(std::string{row["profile"]});
            if (pi == m.profile_index.end())
            {
                throw row.error("profile", "no style for profile '" + std::string{row["profile"]} + "' in email_styles.tsv");
            }
            m.countries[it->second].profile = pi->second;
        }
    }

    // Each country's providers: its own rows plus the global ones it does not replace, without the
    // domains switched off by a weight of 0 (they are not drawn, weighted or not).
    static void read_providers_(model& m, const tables& tbls)
    {
        std::unordered_map<std::string, std::vector<provider_data>> by_country;
        for (const auto& row : tbls.at(index_(table::providers)).rows())
        {
            by_country[upper_(row["country"])].push_back(
                {.domain = std::string{row["domain"]}, .kind = std::string{row["kind"]}, .weight = row.get<double>("weight")});
        }
        const auto global = by_country.find("*");
        for (auto& c : m.countries)
        {
            const auto own = by_country.find(c.region);
            if (own != by_country.end()) c.providers = own->second;
            if (global != by_country.end())
            {
                for (const auto& g : global->second)
                {
                    const bool replaced = own != by_country.end() &&
                                          std::ranges::any_of(own->second, [&](const provider_data& d) { return d.domain == g.domain; });
                    if (!replaced) c.providers.push_back(g);
                }
            }
            std::erase_if(c.providers, [](const provider_data& d) { return !(d.weight > 0.0); });
            for (std::size_t k = 0; k < c.providers.size(); ++k) c.provider_weights.add(k, c.providers[k].weight);
        }
    }

    static model build_model_(const tables& tbls)
    {
        model m;
        read_countries_(m, tbls);
        read_text_tables_(m, tbls);
        read_styles_(m, tbls);
        read_providers_(m, tbls);
        return m;
    }

    void require_data_() const
    {
        if (_model.countries.empty())
        {
            throw std::runtime_error("No email data loaded. Call load() first.");
        }
    }
};

} // namespace dasmig

#endif // DASMIG_EMAILGEN_HPP
