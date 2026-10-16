module cnetmod.json;

import std;
import nlohmann.json;

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
            case errc::type_mismatch:
                return "JSON value has an incompatible type";
            case errc::missing_field:
                return "JSON document is missing a required field";
            case errc::unknown_field:
                return "JSON document contains an unknown field";
            }
            return "unknown JSON error";
        }
    };

    auto from_nlohmann(const nlohmann::json& source) -> document
    {
        if (source.is_null())
            return nullptr;
        if (source.is_boolean())
            return source.get<bool>();
        if (source.is_string())
            return source.get<std::string>();
        if (source.is_number_unsigned())
            return source.get<std::uint64_t>();
        if (source.is_number_integer())
            return source.get<std::int64_t>();
        if (source.is_number_float())
            return source.get<double>();
        if (source.is_array())
        {
            auto result = document::array();
            for (const auto& entry : source)
                result.push_back(from_nlohmann(entry));
            return result;
        }
        auto result = document::object();
        for (auto iterator = source.begin(); iterator != source.end(); ++iterator)
            result[iterator.key()] = from_nlohmann(iterator.value());
        return result;
    }

    auto to_nlohmann(const document& source) -> nlohmann::json
    {
        if (source.is_null())
            return nullptr;
        if (source.is_boolean())
            return source.get<bool>();
        if (source.is_string())
            return source.get<std::string>();
        if (source.is_number_unsigned())
            return source.get<std::uint64_t>();
        if (source.is_number_integer())
            return source.get<std::int64_t>();
        if (source.is_number_float())
            return source.get<double>();
        if (source.is_array())
        {
            auto result = nlohmann::json::array();
            auto& values = result.get_ref<nlohmann::json::array_t&>();
            values.reserve(source.size());
            for (const auto& entry : source)
                values.push_back(to_nlohmann(entry));
            return result;
        }
        auto result = nlohmann::json::object();
        for (const auto& [key, entry] :
            source.get_ref<const document::object_type&>())
            result[key] = to_nlohmann(entry);
        return result;
    }
} // namespace

auto make_error_code(errc value) noexcept -> std::error_code
{
    static const category instance;
    return {static_cast<int>(value), instance};
}

auto parse_document(std::string_view input) -> std::expected<document, std::error_code>
{
    return parse_document(input, {});
}

auto parse_document(std::string_view input, parse_options options)
    -> std::expected<document, std::error_code>
{
    try
    {
        bool violates_policy = false;
        std::unordered_map<int, std::unordered_set<std::string>> object_keys;
        const auto callback = [&](int depth, nlohmann::json::parse_event_t event,
                                  nlohmann::json& parsed) -> bool
        {
            using event_type = nlohmann::json::parse_event_t;
            if ((event == event_type::object_start || event == event_type::array_start) &&
                static_cast<std::size_t>(depth) >= options.max_depth)
                violates_policy = true;

            if (!options.reject_duplicate_keys)
                return true;
            if (event == event_type::object_start)
                object_keys[depth].clear();
            else if (event == event_type::key)
            {
                const auto& key = parsed.get_ref<const std::string&>();
                if (!object_keys[depth].emplace(key).second)
                    violates_policy = true;
            }
            else if (event == event_type::object_end)
                object_keys.erase(depth);
            return true;
        };

        auto parsed = nlohmann::json::parse(input, callback);
        if (violates_policy)
            return std::unexpected(make_error_code(errc::parse_failed));
        return from_nlohmann(parsed);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::unexpected(make_error_code(errc::parse_failed));
    }
}

auto write_document(const document& value, bool prettify)
    -> std::expected<std::string, std::error_code>
{
    try
    {
        return to_nlohmann(value).dump(prettify ? 2 : -1);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::unexpected(make_error_code(errc::serialization_failed));
    }
}

auto document::parse(std::string_view input, std::nullptr_t,
    bool allow_exceptions, bool) -> document
{
    auto parsed = parse_document(input);
    if (parsed)
        return std::move(*parsed);
    if (allow_exceptions)
        throw exception{"invalid JSON document"};
    document result;
    result.discarded_ = true;
    return result;
}

auto document::parse(std::istream& input, std::nullptr_t callback,
    bool allow_exceptions, bool ignore_comments) -> document
{
    return parse(std::string{std::istreambuf_iterator<char>{input},
                     std::istreambuf_iterator<char>{}},
        callback, allow_exceptions,
        ignore_comments);
}

auto document::dump(int) const -> std::string
{
    auto encoded = write_document(*this);
    if (!encoded)
        throw exception{"failed to serialize JSON document"};
    return std::move(*encoded);
}

} // namespace cnetmod::json
