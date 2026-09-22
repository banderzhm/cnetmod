/// cnetmod.protocol.openai:tools — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :foundation;
import :tool_contracts;
import :model;
import :prompt;
import :tools;
import cnetmod.json;

namespace cnetmod::openai {

auto tool_registry::add(executable_tool value)
    -> std::expected<void, std::string>
{
    const auto& name = value.definition.function_name;
    if (name.empty())
        return std::unexpected("tool name cannot be empty");
    if (!value.handler && !value.contextual_handler)
        return std::unexpected("tool handler cannot be empty: " + name);
    if (!value.definition.function_parameters.is_object())
        return std::unexpected("tool parameters must be a JSON Schema object: " + name);
    if (tools_.contains(name))
        return std::unexpected("duplicate tool: " + name);
    tools_.emplace(name, std::move(value));
    return {};
}

auto tool_registry::contains(std::string_view name) const -> bool
{
    return tools_.contains(name);
}

auto tool_registry::definitions() const -> std::vector<tool>
{
    std::vector<tool> result;
    result.reserve(tools_.size());
    for (const auto& [name, value] : tools_)
    {
        (void)name;
        result.push_back(value.definition);
    }
    return result;
}

auto tool_registry::commands() const -> std::vector<executable_tool>
{
    std::vector<executable_tool> result;
    result.reserve(tools_.size());
    for (const auto& [name, value] : tools_)
    {
        (void)name;
        result.push_back(value);
    }
    return result;
}

auto tool_registry::behavior(std::string_view name) const
    -> std::optional<tool_return_behavior>
{
    const auto found = tools_.find(name);
    if (found == tools_.end())
        return std::nullopt;
    return found->second.return_behavior;
}

auto tool_registry::size() const noexcept -> std::size_t
{
    return tools_.size();
}

auto tool_registry::invoke(const tool_call& call, const run_config& config) const
    -> task<std::expected<std::string, std::string>>
{
    auto result = co_await invoke_detailed(call, config);
    if (!result)
        co_return std::unexpected(result.error().message);
    co_return std::move(*result);
}

auto tool_registry::invoke_detailed(const tool_call& call,
    const run_config& config) const
    -> task<std::expected<std::string, tool_error>>
{
    if (config.is_cancelled())
        co_return std::unexpected(tool_error{tool_error_kind::cancelled,
            call.function.name, "tool invocation cancelled"});
    const auto found = tools_.find(call.function.name);
    if (found == tools_.end())
        co_return std::unexpected(tool_error{tool_error_kind::not_found,
            call.function.name, "unknown tool: " + call.function.name});
    auto parsed_arguments = cnetmod::json::parse_document(
        call.function.arguments);
    if (!parsed_arguments)
        co_return std::unexpected(tool_error{tool_error_kind::invalid_arguments,
            call.function.name,
            "invalid JSON arguments for tool: " + call.function.name});
    auto& arguments = *parsed_arguments;
    auto valid = validate_json_schema(arguments,
        found->second.definition.function_parameters);
    if (!valid)
        co_return std::unexpected(tool_error{tool_error_kind::invalid_arguments,
            call.function.name, std::format("invalid arguments for {}: {}", call.function.name, valid.error())});
    run_scope observation{config, run_event_type::tool_start,
        run_event_type::tool_end, run_event_type::tool_error,
        call.function.name, call.function.arguments,
        {{"tool_call_id", call.id}}};
    auto result = found->second.contextual_handler
        ? co_await found->second.contextual_handler(arguments, config)
        : co_await found->second.handler(arguments);
    if (config.is_cancelled())
    {
        observation.fail("tool invocation cancelled", 0,
            {{"tool_call_id", call.id}, {"cancelled", true}});
        co_return std::unexpected(tool_error{tool_error_kind::cancelled,
            call.function.name, "tool invocation cancelled"});
    }
    if (!result)
    {
        observation.fail(result.error(), 0,
            {{"tool_call_id", call.id}});
        co_return std::unexpected(tool_error{tool_error_kind::execution_failed,
            call.function.name, result.error()});
    }
    auto serialized = cnetmod::json::write_document(*result);
    if (!serialized)
        co_return std::unexpected(tool_error{tool_error_kind::execution_failed,
            call.function.name, "tool result serialization failed"});
    observation.succeed(*serialized, 0, {{"tool_call_id", call.id}});
    co_return std::move(*serialized);
}

auto tool_provider::is_dynamic() const noexcept -> bool
{
    return false;
}

functional_tool_provider::functional_tool_provider(tool_provider_handler handler,
    bool dynamic)
    : handler_(std::move(handler)), dynamic_(dynamic)
{
}

auto functional_tool_provider::is_dynamic() const noexcept -> bool
{
    return dynamic_;
}

auto functional_tool_provider::provide(const tool_provider_request& request)
    -> task<std::expected<tool_provider_result, std::string>>
{
    if (!handler_)
        co_return std::unexpected("tool provider handler is not configured");
    co_return co_await handler_(request);
}

} // namespace cnetmod::openai
