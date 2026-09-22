/// cnetmod.protocol.openai:tool_contracts — Function calling contracts

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:tool_contracts;

import std;
import :foundation;

namespace cnetmod::openai {

export struct function_call
{
    std::string name;
    std::string arguments;
};

export struct tool_call
{
    std::string id;
    std::string type = "function";
    function_call function;
};

export struct tool
{
    std::string type = "function";
    std::string function_name;
    std::string function_description;
    json function_parameters = cnetmod::json::object();
    bool strict = true;
};

} // namespace cnetmod::openai
