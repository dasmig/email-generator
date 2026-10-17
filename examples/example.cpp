#include "../dasmig/emailgen.hpp"
#include <cstdint>
#include <iostream>

int main()
{
    // The singleton loads the lite tier (compiled in, or from resources/).
    auto& gen = dasmig::emg::instance();
    if (!gen.has_data())
    {
        std::cerr << "Could not load the email data.\n";
        return 1;
    }
    std::cout << "Loaded " << gen.country_count() << " countries.\n\n";

    std::cout << "Random addresses (no names, so handles):\n";
    for (int i = 0; i < 5; ++i)
    {
        const auto e = gen.get_email(dasmig::email_request{});
        std::cout << "  " << e.country << "  " << e << '\n';
    }

    std::cout << "\nThe same name in five countries:\n";
    for (const char* c : {"US", "DE", "BR", "JP", "CN"})
    {
        const auto e = gen.get_email({.country = c, .given = "Zo\xC3\xAB", .family = "M\xC3\xBCller", .birth_year = 1988});
        std::cout << "  " << c << "  " << e << "  (" << e.kind << ")\n";
    }

    std::cout << "\nSafe addresses for tests:\n";
    for (int i = 0; i < 3; ++i)
    {
        std::cout << "  " << gen.get_email({.given = "Sam", .family = "Lee", .safe = true}) << '\n';
    }

    std::cout << "\nSeed 42, twice:\n";
    const dasmig::email_request request{.given = "Ana", .family = "Souza"};
    const auto a = gen.get_email(request, std::uint64_t{42});
    const auto b = gen.get_email(request, std::uint64_t{42});
    std::cout << "  " << a.to_json() << '\n'
              << "  " << b.to_json() << '\n';
    return 0;
}
