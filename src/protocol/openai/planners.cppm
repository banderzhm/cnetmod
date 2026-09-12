/// cnetmod.protocol.openai:planners — Reusable agentic planning strategies

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:planners;

import std;
import cnetmod.coro.task;
import :model;
import :agentic;

namespace cnetmod::openai {

/// Executes a fixed group of agents concurrently exactly once.
export class parallel_planner final : public workflow_planner
{
public:
    explicit parallel_planner(std::vector<workflow_agent*> agents);
    auto next(agentic_scope& scope, const run_config& config)
        -> task<std::expected<planner_directive, std::string>> override;
    [[nodiscard]] auto save_state() const -> json override;
    auto restore_state(const json& state)
        -> std::expected<void, std::string> override;

private:
    std::vector<workflow_agent*> agents_;
    bool dispatched_ = false;
};

export using agent_selector = std::function<task<
    std::expected<workflow_agent*, std::string>>(
    agentic_scope&, const run_config&)>;

/// Selects one branch from workflow state, then completes.
export class conditional_planner final : public workflow_planner
{
public:
    explicit conditional_planner(agent_selector selector);
    auto next(agentic_scope& scope, const run_config& config)
        -> task<std::expected<planner_directive, std::string>> override;
    [[nodiscard]] auto save_state() const -> json override;
    auto restore_state(const json& state)
        -> std::expected<void, std::string> override;

private:
    agent_selector selector_;
    bool dispatched_ = false;
};

export using loop_condition = std::function<task<
    std::expected<bool, std::string>>(
    agentic_scope&, const run_config&)>;

/// Repeats one agent while an asynchronous condition is true.
export class loop_planner final : public workflow_planner
{
public:
    loop_planner(workflow_agent& agent, loop_condition condition,
        std::size_t max_iterations = 32);
    auto next(agentic_scope& scope, const run_config& config)
        -> task<std::expected<planner_directive, std::string>> override;
    [[nodiscard]] auto save_state() const -> json override;
    auto restore_state(const json& state)
        -> std::expected<void, std::string> override;

private:
    workflow_agent& agent_;
    loop_condition condition_;
    std::size_t max_iterations_;
    std::size_t iterations_ = 0;
};

} // namespace cnetmod::openai
