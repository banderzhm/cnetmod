module cnetmod.observability.export_response;

import std;
import cnetmod.json;

namespace cnetmod::observability::detail {

auto parse_export_response(std::string_view body, std::string_view rejected_field,
    std::uint64_t sent) noexcept -> std::optional<export_acknowledgement>
{
    if (body.empty() || body.size() > 64U * 1024U)
        return std::nullopt;
    try
    {
        const auto parsed = cnetmod::json::parse_document(body,
            {.max_depth = 16, .reject_duplicate_keys = true});
        if (!parsed || !parsed->is_object())
            return std::nullopt;
        export_acknowledgement result;
        const auto partial = parsed->find("partialSuccess");
        if (partial == parsed->end())
            return result;
        if (!partial->is_object())
            return std::nullopt;
        result.partial = true;
        if (const auto rejected = partial->find(rejected_field);
            rejected != partial->end())
        {
            if (rejected->is_number_integer())
                result.rejected = rejected->get<std::uint64_t>();
            else if (rejected->is_string())
            {
                const auto value = rejected->get<std::string>();
                const auto converted = std::from_chars(value.data(),
                    value.data() + value.size(), result.rejected);
                if (converted.ec != std::errc{} ||
                    converted.ptr != value.data() + value.size())
                    return std::nullopt;
            }
            else
                return std::nullopt;
        }
        if (const auto message = partial->find("errorMessage");
            message != partial->end())
        {
            if (!message->is_string())
                return std::nullopt;
            result.warning = !message->get<std::string>().empty();
        }
        if (result.rejected > sent)
            return std::nullopt;
        return result;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

} // namespace cnetmod::observability::detail
