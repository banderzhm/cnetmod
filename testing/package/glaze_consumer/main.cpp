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

CNETMOD_JSON(consumer::payload,
    CNETMOD_JSON_FIELD(name),
    CNETMOD_JSON_FIELD(count))

auto main() -> int
{
    auto encoded = cnetmod::json::write(consumer::payload{"installed", 22});
    if (!encoded)
        return 1;
    auto decoded = cnetmod::json::parse<consumer::payload>(*encoded);
    return decoded && decoded->name == "installed" && decoded->count == 22
        ? 0
        : 2;
}
