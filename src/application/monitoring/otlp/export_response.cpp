module;

#include "../../../utils/detail/simdjson_depth.hpp"

module cnetmod.observability.export_response;

import std;

namespace cnetmod::observability::detail {
auto parse_export_response(std::string_view body, std::string_view rejected_field,
    std::uint64_t sent) noexcept -> std::optional<export_acknowledgement>
{
    if (body.empty() || body.size() > 64U * 1024U)
        return std::nullopt;

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (!cnetmod::detail::parse_bounded_json(parser, body, root) ||
        !root.is_object())
        return std::nullopt;

    using cnetmod::detail::find_last_json_field;
    export_acknowledgement result;
    const auto partial = find_last_json_field(root, "partialSuccess");
    if (partial.error() == simdjson::NO_SUCH_FIELD)
        return result;
    if (partial.error() || !partial.value_unsafe().is_object())
        return std::nullopt;

    result.partial = true;
    const auto fields = partial.value_unsafe();
    if (const auto rejected = find_last_json_field(fields, rejected_field); !rejected.error())
    {
        std::string_view text;
        if (!rejected.get_string().get(text))
        {
            const auto converted = std::from_chars(text.data(),
                text.data() + text.size(), result.rejected);
            if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size())
                return std::nullopt;
        }
        else if (rejected.get_uint64().get(result.rejected))
        {
            return std::nullopt;
        }
    }

    if (const auto message = find_last_json_field(fields, "errorMessage"); !message.error())
    {
        std::string_view text;
        if (message.get_string().get(text))
            return std::nullopt;
        result.warning = !text.empty();
    }
    if (result.rejected > sent)
        return std::nullopt;
    return result;
}

} // namespace cnetmod::observability::detail
