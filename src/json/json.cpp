module cnetmod.json;

import std;

namespace cnetmod::json {
namespace {
    class category final : public std::error_category
    {
    public:
        [[nodiscard]] auto name() const noexcept -> const char* override
        {
            return "cnetmod.json";
        }

        [[nodiscard]] auto message(int value) const -> std::string override
        {
            switch (static_cast<errc>(value))
            {
            case errc::parse_failed:
                return "JSON parsing failed";
            case errc::serialization_failed:
                return "JSON serialization failed";
            }
            return "unknown JSON error";
        }
    };
} // namespace

auto make_error_code(errc value) noexcept -> std::error_code
{
    static const category instance;
    return {static_cast<int>(value), instance};
}
} // namespace cnetmod::json
