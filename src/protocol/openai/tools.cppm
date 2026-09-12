/// cnetmod.protocol.openai:tools — Schema-validated asynchronous tools

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:tools;

import std;
import cnetmod.coro.task;
import :foundation;
import :tool_contracts;
import :messages;
import :model;
import nlohmann.json;

namespace cnetmod::openai {

export using tool_handler = std::function<task<
    std::expected<json, std::string>>(const json& arguments)>;
export using contextual_tool_handler = std::function<task<
    std::expected<json, std::string>>(const json& arguments,
    const run_config& config)>;

export enum class tool_return_behavior
{
    to_model,
    immediate,
    immediate_if_last
};

export enum class tool_visibility
{
    searchable,
    always_visible
};

export struct executable_tool
{
    tool definition;
    tool_handler handler;
    contextual_tool_handler contextual_handler;
    tool_return_behavior return_behavior = tool_return_behavior::to_model;
    tool_visibility visibility = tool_visibility::searchable;
};

export enum class tool_error_kind
{
    cancelled,
    not_found,
    invalid_arguments,
    execution_failed
};

export struct tool_error
{
    tool_error_kind kind = tool_error_kind::execution_failed;
    std::string tool_name;
    std::string message;
};

/// Command registry. Configure before serving concurrent agent invocations.
export class tool_registry
{
public:
    [[nodiscard]] auto add(executable_tool value)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto contains(std::string_view name) const -> bool;
    [[nodiscard]] auto definitions() const -> std::vector<tool>;
    [[nodiscard]] auto commands() const -> std::vector<executable_tool>;
    [[nodiscard]] auto behavior(std::string_view name) const
        -> std::optional<tool_return_behavior>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    auto invoke(const tool_call& call, const run_config& config = {}) const
        -> task<std::expected<std::string, std::string>>;
    auto invoke_detailed(const tool_call& call,
        const run_config& config = {}) const
        -> task<std::expected<std::string, tool_error>>;

private:
    std::map<std::string, executable_tool, std::less<>> tools_;
};

export struct tool_provider_request
{
    std::vector<message> conversation;
    std::string session_id;
    std::map<std::string, std::string> invocation_parameters;
    std::size_t iteration = 0;
    /// Non-owning invocation context; providers must not retain this pointer.
    const run_config* config = nullptr;
};

export struct tool_provider_result
{
    std::vector<executable_tool> tools;
};

/// Strategy that resolves the tools visible to one agent invocation.
export class tool_provider
{
public:
    virtual ~tool_provider() = default;
    [[nodiscard]] virtual auto is_dynamic() const noexcept -> bool;
    virtual auto provide(const tool_provider_request& request)
        -> task<std::expected<tool_provider_result, std::string>> = 0;
};

export using tool_provider_handler = std::function<task<
    std::expected<tool_provider_result, std::string>>(
    const tool_provider_request& request)>;

/// Adapter for supplying tools with a coroutine callback.
export class functional_tool_provider final : public tool_provider
{
public:
    explicit functional_tool_provider(tool_provider_handler handler,
        bool dynamic = false);

    [[nodiscard]] auto is_dynamic() const noexcept -> bool override;
    auto provide(const tool_provider_request& request)
        -> task<std::expected<tool_provider_result, std::string>> override;

private:
    tool_provider_handler handler_;
    bool dynamic_ = false;
};

} // namespace cnetmod::openai
