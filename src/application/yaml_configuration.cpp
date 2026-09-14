module cnetmod.application.yaml_configuration;

import std;
import nlohmann.json;
import yaml_cpp;

namespace cnetmod::application {
namespace {

    constexpr std::size_t maximum_yaml_depth = 64;

    /**
     * @brief Converts a YAML scalar without losing explicit string tags.
     */
    auto convert_scalar(const yaml_cpp::Node& node) -> nlohmann::json
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
        const auto decimal_result = std::from_chars(
            value.data(), value.data() + value.size(), decimal);
        if (decimal_result.ec == std::errc{} &&
            decimal_result.ptr == value.data() + value.size() &&
            std::isfinite(decimal))
            return decimal;
        return value;
    }

    /**
     * @brief Recursively converts one bounded YAML node into JSON.
     */
    auto convert_node(const yaml_cpp::Node& node, std::size_t depth)
        -> std::expected<nlohmann::json, std::error_code>
    {
        if (depth > maximum_yaml_depth || !node ||
            node.Type() == yaml_cpp::NodeType::Undefined)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (node.Type() == yaml_cpp::NodeType::Null)
            return nlohmann::json{nullptr};
        if (node.Type() == yaml_cpp::NodeType::Scalar)
            return convert_scalar(node);
        if (node.Type() == yaml_cpp::NodeType::Sequence)
        {
            auto result = nlohmann::json::array();
            for (std::size_t index = 0; index < node.size(); ++index)
            {
                auto child = convert_node(node[index], depth + 1U);
                if (!child)
                    return std::unexpected(child.error());
                result.push_back(std::move(*child));
            }
            return result;
        }
        if (node.Type() != yaml_cpp::NodeType::Map)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        auto result = nlohmann::json::object();
        for (const auto& entry : node)
        {
            if (entry.first.Type() != yaml_cpp::NodeType::Scalar)
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
    -> std::expected<nlohmann::json, std::error_code>
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
