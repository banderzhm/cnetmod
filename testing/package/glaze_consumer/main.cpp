#include <cnetmod/json_codec.hpp>

import std;
import cnetmod.json;

namespace consumer {
struct payload
{
    std::string name;
    std::int32_t count{};
};
} // namespace consumer

auto glaze_header_is_available() -> bool;

auto main() -> int
{
    auto encoded = cnetmod::json::write(consumer::payload{"installed", 22});
    if (!encoded || !glaze_header_is_available())
        return 1;
    auto decoded = cnetmod::json::parse<consumer::payload>(*encoded);
    return decoded && decoded->name == "installed" && decoded->count == 22
        ? 0
        : 2;
}
