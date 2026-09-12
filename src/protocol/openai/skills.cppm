/// cnetmod.protocol.openai:skills — Progressive Agent Skills activation

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:skills;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :model;
import :tools;

namespace cnetmod::openai {

export struct agent_skill
{
    std::string name;
    std::string description;
    std::string instructions;
    std::map<std::string, std::string, std::less<>> resources;
    std::vector<executable_tool> tools;
    std::vector<tool_provider*> tool_providers;
};

/// Catalog and dynamic Tool Provider for progressively disclosed skills.
export class skill_catalog final : public tool_provider
{
public:
    [[nodiscard]] auto add(agent_skill skill)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto contains(std::string_view name) const -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto format_available_skills() const -> std::string;

    [[nodiscard]] auto is_dynamic() const noexcept -> bool override;
    auto provide(const tool_provider_request& request)
        -> task<std::expected<tool_provider_result, std::string>> override;

private:
    std::map<std::string, agent_skill, std::less<>> skills_;
};

export struct skill_loader_options
{
    std::string entry_file = "SKILL.md";
    std::size_t max_file_bytes = 1024 * 1024;
    std::size_t max_total_bytes = 16 * 1024 * 1024;
    bool include_hidden_files = false;
};

/// Loads one Agent Skills directory without blocking the I/O event loop.
export class filesystem_skill_loader
{
public:
    filesystem_skill_loader(io_context& context, thread_pool& pool,
        skill_loader_options options = {});

    auto load(std::filesystem::path directory,
        const run_config& config = {})
        -> task<std::expected<agent_skill, std::string>>;

private:
    io_context& context_;
    thread_pool& pool_;
    skill_loader_options options_;
};

} // namespace cnetmod::openai
