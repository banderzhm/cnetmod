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
        const auto parsed = cnetmod::json::parse_document(body);
        if (!parsed || !parsed->is_object())
            return std::nullopt;
        export_acknowledgement result;
        const auto& root = parsed->get_object();
        const auto partial = root.find("partialSuccess");
        if (partial == root.end())
            return result;
        const auto& partial_value = partial->second;
        if (!partial_value.is_object())
            return std::nullopt;
        const auto& partial_object = partial_value.get_object();
        result.partial = true;
        if (const auto rejected = partial_object.find(rejected_field);
            rejected != partial_object.end())
        {
            const auto& rejected_value = rejected->second;
            if (rejected_value.is_uint64() || rejected_value.is_int64())
                result.rejected = rejected_value.as<std::uint64_t>();
            else if (rejected_value.is_string())
            {
                const auto value = rejected_value.get<std::string>();
                const auto converted = std::from_chars(value.data(),
                    value.data() + value.size(), result.rejected);
                if (converted.ec != std::errc{} ||
                    converted.ptr != value.data() + value.size())
                    return std::nullopt;
            }
            else
                return std::nullopt;
        }
        if (const auto message = partial_object.find("errorMessage");
            message != partial_object.end())
        {
            if (!message->second.is_string())
                return std::nullopt;
            result.warning = !message->second.get<std::string>().empty();
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
