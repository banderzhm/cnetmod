/// cnetmod.protocol.openai:bindings — Strongly typed tool bindings

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:bindings;

import std;
import cnetmod.coro.task;
import :foundation;
import :tool_contracts;
import :tools;

namespace cnetmod::openai {

export template <typename Arguments, typename Result>
struct tool_binding
{
    tool definition;
    std::function<std::expected<Arguments, std::string>(const json&)> decode;
    std::function<task<std::expected<Result, std::string>>(Arguments)> execute;
    std::function<std::expected<json, std::string>(Result)> encode;
};

/// Adapts a domain-level asynchronous command to the JSON tool protocol.
export template <typename Arguments, typename Result>
auto bind_tool(tool_binding<Arguments, Result> binding)
    -> std::expected<executable_tool, std::string>
{
    if (binding.definition.function_name.empty())
        return std::unexpected("tool binding name cannot be empty");
    if (!binding.decode)
        return std::unexpected("tool argument decoder is not configured");
    if (!binding.execute)
        return std::unexpected("tool command is not configured");
    if (!binding.encode)
        return std::unexpected("tool result encoder is not configured");

    auto definition = std::move(binding.definition);
    auto handler = [decode = std::move(binding.decode),
                       execute = std::move(binding.execute),
                       encode = std::move(binding.encode)](const json& wire)
        -> task<std::expected<json, std::string>>
    {
        auto arguments = decode(wire);
        if (!arguments)
            co_return std::unexpected("tool argument decoding failed: " +
                arguments.error());
        auto result = co_await execute(std::move(*arguments));
        if (!result)
            co_return std::unexpected(result.error());
        auto encoded = encode(std::move(*result));
        if (!encoded)
            co_return std::unexpected("tool result encoding failed: " +
                encoded.error());
        co_return std::move(*encoded);
    };
    return executable_tool{
        .definition = std::move(definition),
        .handler = std::move(handler)};
}

} // namespace cnetmod::openai
