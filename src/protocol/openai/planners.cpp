/// cnetmod.protocol.openai:planners — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :model;
import :agentic;
import :planners;

namespace cnetmod::openai {

parallel_planner::parallel_planner(std::vector<workflow_agent*> agents)
    : agents_(std::move(agents))
{
    if (agents_.empty() || std::ranges::contains(agents_, nullptr))
        throw std::invalid_argument(
            "parallel planner requires non-null agents");
}

auto parallel_planner::next(agentic_scope&, const run_config&)
    -> task<std::expected<planner_directive, std::string>>
{
    if (dispatched_)
        co_return planner_directive{.status = planner_status::complete};
    dispatched_ = true;
    co_return planner_directive{
        .status = planner_status::execute,
        .agents = agents_};
}

auto parallel_planner::save_state() const -> json
{
    return {{"dispatched", dispatched_}};
}

auto parallel_planner::restore_state(const json& state)
    -> std::expected<void, std::string>
{
    if (!state.is_object())
        return std::unexpected("parallel planner state must be an object");
    dispatched_ = state.value("dispatched", false);
    return {};
}

conditional_planner::conditional_planner(agent_selector selector)
    : selector_(std::move(selector))
{
    if (!selector_)
        throw std::invalid_argument(
            "conditional planner selector cannot be empty");
}

auto conditional_planner::next(agentic_scope& scope,
    const run_config& config)
    -> task<std::expected<planner_directive, std::string>>
{
    if (dispatched_)
        co_return planner_directive{.status = planner_status::complete};
    auto selected = co_await selector_(scope, config);
    if (!selected)
        co_return std::unexpected(selected.error());
    if (!*selected)
        co_return std::unexpected(
            "conditional planner selected a null agent");
    dispatched_ = true;
    co_return planner_directive{
        .status = planner_status::execute,
        .agents = {*selected}};
}

auto conditional_planner::save_state() const -> json
{
    return {{"dispatched", dispatched_}};
}

auto conditional_planner::restore_state(const json& state)
    -> std::expected<void, std::string>
{
    if (!state.is_object())
        return std::unexpected(
            "conditional planner state must be an object");
    dispatched_ = state.value("dispatched", false);
    return {};
}

loop_planner::loop_planner(workflow_agent& agent,
    loop_condition condition, std::size_t max_iterations)
    : agent_(agent), condition_(std::move(condition)), max_iterations_(std::max<std::size_t>(1, max_iterations))
{
    if (!condition_)
        throw std::invalid_argument("loop planner condition cannot be empty");
}

auto loop_planner::next(agentic_scope& scope, const run_config& config)
    -> task<std::expected<planner_directive, std::string>>
{
    if (iterations_ >= max_iterations_)
        co_return std::unexpected(std::format(
            "loop planner exceeded max_iterations={}", max_iterations_));
    auto proceed = co_await condition_(scope, config);
    if (!proceed)
        co_return std::unexpected(proceed.error());
    if (!*proceed)
        co_return planner_directive{.status = planner_status::complete};
    ++iterations_;
    co_return planner_directive{
        .status = planner_status::execute,
        .agents = {&agent_}};
}

auto loop_planner::save_state() const -> json
{
    return {{"iterations", iterations_}};
}

auto loop_planner::restore_state(const json& state)
    -> std::expected<void, std::string>
{
    if (!state.is_object())
        return std::unexpected("loop planner state must be an object");
    const auto iterations = state.value("iterations", std::size_t{0});
    if (iterations > max_iterations_)
        return std::unexpected("loop planner iteration is out of range");
    iterations_ = iterations;
    return {};
}

} // namespace cnetmod::openai
