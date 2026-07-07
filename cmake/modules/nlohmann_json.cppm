module;

#include <nlohmann/json.hpp>

export module nlohmann.json;

export
NLOHMANN_JSON_NAMESPACE_BEGIN

using NLOHMANN_JSON_NAMESPACE::adl_serializer;
using NLOHMANN_JSON_NAMESPACE::basic_json;
using NLOHMANN_JSON_NAMESPACE::json;
using NLOHMANN_JSON_NAMESPACE::json_pointer;
using NLOHMANN_JSON_NAMESPACE::ordered_json;
using NLOHMANN_JSON_NAMESPACE::ordered_map;
using NLOHMANN_JSON_NAMESPACE::to_string;

inline namespace literals
{
inline namespace json_literals
{
    using NLOHMANN_JSON_NAMESPACE::literals::json_literals::operator""_json;
    using NLOHMANN_JSON_NAMESPACE::literals::json_literals::operator""_json_pointer;
}
}

namespace detail
{
    using NLOHMANN_JSON_NAMESPACE::detail::json_sax_dom_callback_parser;
    using NLOHMANN_JSON_NAMESPACE::detail::unknown_size;
}

NLOHMANN_JSON_NAMESPACE_END
