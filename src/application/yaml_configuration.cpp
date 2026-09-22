module cnetmod.application.yaml_configuration;

import std;
import cnetmod.json;
import yaml_cpp;
import cnetmod.utils.charconv;

namespace cnetmod::application {
namespace {

    constexpr std::size_t maximum_yaml_depth = 64;

    /**
     * @brief Converts a YAML scalar without losing explicit string tags.
     */
    auto convert_scalar(const yaml_cpp::Node& node) -> cnetmod::json::document
    {
        const auto value = node.Scalar();
        const auto tag = node.Tag();
        if (tag == "!" || tag == "!!str" ||
            tag == "tag:yaml.org,2002:str")
            return value;
        if (tag == "!!null" || tag == "tag:yaml.org,2002:null" ||
            value == "~" || value == "null" || value == "Null" || value == "NULL")
            return nullptr;
        if (tag == "!!bool" || tag == "tag:yaml.org,2002:bool" ||
            value == "true" || value == "True" || value == "TRUE")
            return true;
        if (value == "false" || value == "False" || value == "FALSE")
            return false;

        std::int64_t integer = 0;
        const auto integer_result = std::from_chars(
            value.data(), value.data() + value.size(), integer);
        if (integer_result.ec == std::errc{} &&
            integer_result.ptr == value.data() + value.size())
            return integer;

        double decimal = 0.0;
        if (cnetmod::from_chars_double(value, decimal) == std::errc{} &&
            std::isfinite(decimal))
            return decimal;
        return value;
    }

    /**
     * @brief Recursively converts one bounded YAML node into JSON.
     */
    auto convert_node(const yaml_cpp::Node& node, std::size_t depth)
        -> std::expected<cnetmod::json::document, std::error_code>
    {
        const auto kind = yaml_cpp::kind_of(node);
        if (depth > maximum_yaml_depth || !node ||
            kind == yaml_cpp::node_kind::undefined)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (kind == yaml_cpp::node_kind::null)
            return cnetmod::json::document{nullptr};
        if (kind == yaml_cpp::node_kind::scalar)
            return convert_scalar(node);
        if (kind == yaml_cpp::node_kind::sequence)
        {
            auto result = cnetmod::json::document::array();
            for (std::size_t index = 0; index < node.size(); ++index)
            {
                auto child = convert_node(node[index], depth + 1U);
                if (!child)
                    return std::unexpected(child.error());
                result.push_back(std::move(*child));
            }
            return result;
        }
        if (kind != yaml_cpp::node_kind::map)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        auto result = cnetmod::json::document::object();
        for (const auto& entry : node)
        {
            if (yaml_cpp::kind_of(entry.first) != yaml_cpp::node_kind::scalar)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            const auto key = entry.first.Scalar();
            if (result.contains(key))
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            auto child = convert_node(entry.second, depth + 1U);
            if (!child)
                return std::unexpected(child.error());
            result.emplace(key, std::move(*child));
        }
        return result;
    }
}

auto load_yaml_configuration_document(const std::filesystem::path& path)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    try
    {
        const auto document = yaml_cpp::LoadFile(path.string());
        auto converted = convert_node(document, 0U);
        if (!converted || !converted->is_object())
            return std::unexpected(converted
                ? std::make_error_code(std::errc::invalid_argument)
                : converted.error());
        return converted;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
}

} // namespace cnetmod::application
