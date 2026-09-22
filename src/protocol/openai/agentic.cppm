/// cnetmod.protocol.openai:agentic — Recoverable multi-agent workflow runtime

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:agentic;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :model;
import :prompt;
import :checkpoint;
import cnetmod.json;

namespace cnetmod::openai {

export class agentic_scope
{
public:
    auto read(std::string key) -> task<std::optional<json>>;
    auto write(std::string key, json value) -> task<void>;
    auto erase(std::string key) -> task<void>;
    auto contains(std::string key) -> task<bool>;
    auto snapshot() -> task<json>;
    auto restore(json state) -> task<std::expected<void, std::string>>;

private:
    async_mutex mutex_;
    json state_ = json::object();
};

export struct agent_invocation
{
    std::string agent;
    bool successful = false;
    std::string error;
};

export class workflow_agent
{
public:
    virtual ~workflow_agent() = default;
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
    virtual auto invoke(agentic_scope& scope, const run_config& config)
        -> task<std::expected<void, std::string>> = 0;
};

export using agent_handler = std::function<task<std::expected<void, std::string>>(
    agentic_scope&, const run_config&)>;

export class functional_agent final : public workflow_agent
{
public:
    functional_agent(std::string name, agent_handler handler);
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    auto invoke(agentic_scope& scope, const run_config& config)
        -> task<std::expected<void, std::string>> override;

private:
    std::string name_;
    agent_handler handler_;
};

export enum class planner_status
{
    execute,
    complete,
    suspend
};

export struct human_input_request
{
    std::string id;
    std::string prompt;
    std::string response_key = "human_input";
    json response_schema = json::object();
};

export struct human_input_response
{
    std::string request_id;
    json value;
};

export struct planner_directive
{
    planner_status status = planner_status::complete;
    std::vector<workflow_agent*> agents;
    std::string reason;
    std::optional<human_input_request> human_input;
};

export class workflow_planner
{
public:
    virtual ~workflow_planner() = default;
    virtual auto next(agentic_scope& scope, const run_config& config)
        -> task<std::expected<planner_directive, std::string>> = 0;
    [[nodiscard]] virtual auto save_state() const -> json;
    virtual auto restore_state(const json& state)
        -> std::expected<void, std::string>;
};

export class sequence_planner final : public workflow_planner
{
public:
    explicit sequence_planner(std::vector<workflow_agent*> agents);
    auto next(agentic_scope& scope, const run_config& config)
        -> task<std::expected<planner_directive, std::string>> override;
    [[nodiscard]] auto save_state() const -> json override;
    auto restore_state(const json& state)
        -> std::expected<void, std::string> override;

private:
    std::vector<workflow_agent*> agents_;
    std::size_t cursor_ = 0;
};

export struct agentic_checkpoint
{
    json scope = json::object();
    json planner = json::object();
    std::size_t completed_steps = 0;
    std::optional<human_input_request> pending_human_input;
};

export class agentic_scope_store
{
public:
    virtual ~agentic_scope_store() = default;
    virtual auto load(std::string workflow_id)
        -> task<std::expected<std::optional<agentic_checkpoint>, std::string>> = 0;
    virtual auto save(std::string workflow_id, agentic_checkpoint checkpoint)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto erase(std::string workflow_id)
        -> task<std::expected<void, std::string>> = 0;
};

export class in_memory_agentic_scope_store final : public agentic_scope_store
{
public:
    auto load(std::string workflow_id)
        -> task<std::expected<std::optional<agentic_checkpoint>, std::string>> override;
    auto save(std::string workflow_id, agentic_checkpoint checkpoint)
        -> task<std::expected<void, std::string>> override;
    auto erase(std::string workflow_id)
        -> task<std::expected<void, std::string>> override;

private:
    async_mutex mutex_;
    std::map<std::string, agentic_checkpoint, std::less<>> checkpoints_;
};

/**
 * Adapts the generic versioned checkpoint store to the Agent runtime.
 *
 * Every Agent save creates an immutable checkpoint version and uses optimistic
 * concurrency, so concurrent workflow runners cannot silently overwrite state.
 */
export class checkpoint_agentic_scope_store final : public agentic_scope_store
{
public:
    explicit checkpoint_agentic_scope_store(checkpoint_store& store,
        std::string branch = "main");

    auto load(std::string workflow_id)
        -> task<std::expected<std::optional<agentic_checkpoint>, std::string>> override;
    auto save(std::string workflow_id, agentic_checkpoint checkpoint)
        -> task<std::expected<void, std::string>> override;
    auto erase(std::string workflow_id)
        -> task<std::expected<void, std::string>> override;

private:
    checkpoint_store& store_;
    std::string branch_;
};

export struct file_agentic_store_options
{
    std::size_t max_checkpoint_bytes = 16 * 1024 * 1024;
};

/// Durable per-workflow checkpoint repository using atomic file replacement.
export class file_agentic_scope_store final : public agentic_scope_store
{
public:
    file_agentic_scope_store(io_context& context, thread_pool& pool,
        std::filesystem::path directory,
        file_agentic_store_options options = {});

    auto load(std::string workflow_id)
        -> task<std::expected<std::optional<agentic_checkpoint>, std::string>> override;
    auto save(std::string workflow_id, agentic_checkpoint checkpoint)
        -> task<std::expected<void, std::string>> override;
    auto erase(std::string workflow_id)
        -> task<std::expected<void, std::string>> override;

private:
    io_context& context_;
    thread_pool& pool_;
    std::filesystem::path directory_;
    file_agentic_store_options options_;
    async_mutex mutex_;
};

export enum class workflow_status
{
    completed,
    suspended
};

export struct workflow_result
{
    workflow_status status = workflow_status::completed;
    json state = json::object();
    std::vector<agent_invocation> invocations;
    std::string suspension_reason;
    std::optional<human_input_request> pending_human_input;
    std::size_t completed_steps = 0;
};

/// Template Method runtime with planner strategy and checkpoint repository.
export class agentic_runtime
{
public:
    agentic_runtime(io_context& context, agentic_scope_store& store,
        std::size_t max_steps = 64);

    auto execute(std::string workflow_id, workflow_planner& planner,
        json initial_state = json::object(), const run_config& config = {})
        -> task<std::expected<workflow_result, std::string>>;
    auto resume(std::string workflow_id, workflow_planner& planner,
        human_input_response response, const run_config& config = {})
        -> task<std::expected<workflow_result, std::string>>;
    auto discard(std::string workflow_id)
        -> task<std::expected<void, std::string>>;

private:
    io_context& context_;
    agentic_scope_store& store_;
    std::size_t max_steps_;
};

} // namespace cnetmod::openai
