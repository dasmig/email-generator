// Benchmarks for email-generator; see bench.hpp for the options.

#include "../dasmig/emailgen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"email-generator", argc, argv};

    run.load("load", [&] {
        dasmig::emg gen;
        return gen.load(run.tier());
    });

    dasmig::emg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    const dasmig::email_request request{.given = "Ana", .family = "Souza", .birth_year = 1990};
    run.calls("get_email", [&](std::uint64_t seed) { return gen.get_email(request, seed); });
    run.calls("get_email to_json", [&](std::uint64_t seed) {
        return gen.get_email(request, seed).to_json();
    });

    run.batches("get_email_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_email_batch(request, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}
