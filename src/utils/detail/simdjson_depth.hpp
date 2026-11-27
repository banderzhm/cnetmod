#pragma once

#include <simdjson.h>

namespace cnetmod::detail {

inline constexpr std::size_t max_container_depth = 256;

inline auto within_depth(simdjson::dom::element value, std::size_t depth) noexcept -> bool
{
    if (value.is_array())
    {
        if (depth == max_container_depth)
            return false;
        for (const auto child : value.get_array().value_unsafe())
        {
            if (!within_depth(child, depth + 1))
                return false;
        }
    }
    else if (value.is_object())
    {
        if (depth == max_container_depth)
            return false;
        for (const auto field : value.get_object().value_unsafe())
        {
            if (!within_depth(field.value, depth + 1))
                return false;
        }
    }
    return true;
}

inline auto parse_bounded_json(simdjson::dom::parser& parser,
    std::string_view input, simdjson::dom::element& root) noexcept -> bool
{
    if (input.starts_with("\xef\xbb\xbf"))
        input.remove_prefix(3);
    // Empty containers are not counted by the native tape depth limit.
    return !parser.allocate(input.size(), max_container_depth + 1) &&
        !parser.parse(input.data(), input.size()).get(root) && within_depth(root, 0);
}

inline auto find_last_json_field(simdjson::dom::element value,
    std::string_view key) noexcept -> simdjson::simdjson_result<simdjson::dom::element>
{
    simdjson::dom::object fields;
    if (const auto error = value.get_object().get(fields))
        return error;
    simdjson::dom::element result;
    bool found = false;
    for (const auto field : fields)
    {
        if (field.key == key)
        {
            result = field.value;
            found = true;
        }
    }
    if (!found)
        return simdjson::NO_SUCH_FIELD;
    return result;
}

} // namespace cnetmod::detail
