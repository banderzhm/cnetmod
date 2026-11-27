export module cnetmod.application.reflected_options;

import std;
import boost.pfr;
import cnetmod.json;
import cnetmod.application.configuration;

namespace cnetmod::application {
namespace detail {

template <class T>
inline constexpr bool is_option_optional = false;

template <class T>
inline constexpr bool is_option_optional<std::optional<T>> = true;

template <class T>
concept OptionMap = requires(T& result, typename T::mapped_type value) {
    requires std::same_as<typename T::key_type, std::string>;
    result.emplace(std::string{}, std::move(value));
};

template <class T>
concept OptionSequence = std::ranges::input_range<T> &&
    !std::convertible_to<T, std::string_view> && requires(T& result, typename T::value_type value) {
        result.push_back(std::move(value));
    };

template <class T>
concept ReflectedOptionsObject = std::is_class_v<T> && std::is_aggregate_v<T> &&
    std::default_initializable<T> && !std::ranges::range<T>;

template <class T>
[[nodiscard]] auto assign_provided_option(T& target, const cnetmod::json::document& provided,
    std::string_view path) -> std::expected<void, configuration_error>;

template <ReflectedOptionsObject T>
[[nodiscard]] auto assign_reflected_object(T& target, const cnetmod::json::document& provided,
    std::string_view path) -> std::expected<void, configuration_error>
{
    if (!provided.is_object())
        return std::unexpected(configuration_error{.path = std::string{path},
            .message = "expected a mapping"});

    constexpr auto names = boost::pfr::names_as_array<T>();
    for (const auto& [name, value] : provided.get_object())
    {
        if (std::ranges::find(names, name) == names.end())
            return std::unexpected(configuration_error{
                .path = std::format("{}.{}", path, name), .message = "unknown key"});
    }

    std::expected<void, configuration_error> status;
    boost::pfr::for_each_field_with_name(target, [&](std::string_view name, auto& member)
    {
        if (status)
        {
            if (const auto* field = cnetmod::json::find(provided, name))
                status = assign_provided_option(member, *field, std::format("{}.{}", path, name));
        }
    });
    return status;
}

template <class T>
[[nodiscard]] auto assign_provided_option(T& target, const cnetmod::json::document& provided,
    std::string_view path) -> std::expected<void, configuration_error>
{
    if constexpr (is_option_optional<T>)
    {
        if (provided.is_null())
        {
            target.reset();
            return {};
        }
        if (!target)
            target.emplace();
        return assign_provided_option(*target, provided, path);
    }
    else if constexpr (ReflectedOptionsObject<T> && !cnetmod::json::DocumentMapped<T>)
        return assign_reflected_object(target, provided, path);
    else if constexpr (OptionMap<T> && !cnetmod::json::DocumentMapped<T>)
    {
        if (!provided.is_object())
            return std::unexpected(configuration_error{.path = std::string{path},
                .message = "expected a mapping"});
        T entries{};
        for (const auto& [key, field] : provided.get_object())
        {
            typename T::mapped_type entry{};
            if (auto result = assign_provided_option(entry, field, std::format("{}.{}", path, key));
                !result)
                return result;
            entries.emplace(key, std::move(entry));
        }
        target = std::move(entries);
        return {};
    }
    else if constexpr (OptionSequence<T> && !cnetmod::json::DocumentMapped<T>)
    {
        if (!provided.is_array())
            return std::unexpected(configuration_error{.path = std::string{path},
                .message = "expected an array"});
        T entries{};
        std::size_t index{};
        for (const auto& field : provided.get_array())
        {
            typename T::value_type entry{};
            if (auto result = assign_provided_option(entry, field, std::format("{}[{}]", path, index++));
                !result)
                return result;
            entries.push_back(std::move(entry));
        }
        target = std::move(entries);
        return {};
    }
    else
    {
        auto decoded = cnetmod::json::from_document_with_defaults<T>(provided);
        if (!decoded)
            return std::unexpected(configuration_error{.code = decoded.error(),
                .path = std::string{path}, .message = decoded.error().message()});
        target = std::move(*decoded);
        return {};
    }
}

} // namespace detail

/// Reads only supplied fields of a simple aggregate into its C++ defaults.
export template <class T>
    requires detail::ReflectedOptionsObject<T>
[[nodiscard]] auto decode_reflected_options(std::string_view path,
    const cnetmod::json::document* provided) -> std::expected<T, configuration_error>
{
    T result{};
    if (provided)
    {
        if (auto assigned = detail::assign_reflected_object(result, *provided, path); !assigned)
            return std::unexpected(assigned.error());
    }
    return result;
}

} // namespace cnetmod::application
