/// cnetmod.protocol.openai:skills — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :model;
import :tools;
import :skills;
import cnetmod.json;

namespace cnetmod::openai {

namespace {
    auto parse_activation(const message& value, std::string_view field)
        -> std::optional<std::string>
    {
        if (value.role != "tool")
            return std::nullopt;
        auto payload = cnetmod::json::parse_document(value.content);
        if (!payload || !payload->is_object() || !payload->contains(field) ||
            !(*payload)[field].is_string())
            return std::nullopt;
        return (*payload)[field].get<std::string>();
    }

    auto read_file(const std::filesystem::path& path, std::size_t limit)
        -> std::expected<std::string, std::string>
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::unexpected(std::format(
                "cannot inspect '{}': {}", path.string(), error.message()));
        if (size > limit)
            return std::unexpected(std::format(
                "skill file '{}' exceeds {} bytes", path.string(), limit));
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return std::unexpected("cannot open skill file: " + path.string());
        std::string content(static_cast<std::size_t>(size), '\0');
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (!input && !input.eof())
            return std::unexpected("cannot read skill file: " + path.string());
        content.resize(static_cast<std::size_t>(input.gcount()));
        return content;
    }

    auto unquote(std::string value) -> std::string
    {
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\'')))
            return value.substr(1, value.size() - 2);
        return value;
    }

    void parse_skill_document(std::string content, agent_skill& skill)
    {
        if (!content.starts_with("---\n") && !content.starts_with("---\r\n"))
        {
            skill.instructions = std::move(content);
            return;
        }
        const auto header_start = content.find('\n') + 1;
        const auto header_end = content.find("\n---", header_start);
        if (header_end == std::string::npos)
        {
            skill.instructions = std::move(content);
            return;
        }
        auto header = std::string_view(content).substr(
            header_start, header_end - header_start);
        std::size_t offset = 0;
        while (offset < header.size())
        {
            const auto end = header.find('\n', offset);
            auto line = header.substr(offset,
                end == std::string_view::npos ? header.size() - offset
                                              : end - offset);
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            if (const auto separator = line.find(':');
                separator != std::string_view::npos)
            {
                auto key = std::string(line.substr(0, separator));
                auto value = std::string(line.substr(separator + 1));
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                    value.erase(value.begin());
                if (key == "name")
                    skill.name = unquote(std::move(value));
                else if (key == "description")
                    skill.description = unquote(std::move(value));
            }
            if (end == std::string_view::npos)
                break;
            offset = end + 1;
        }
        auto body_start = header_end + 4;
        if (body_start < content.size() && content[body_start] == '\r')
            ++body_start;
        if (body_start < content.size() && content[body_start] == '\n')
            ++body_start;
        skill.instructions = content.substr(body_start);
    }

    auto load_skill_directory(std::filesystem::path directory,
        const skill_loader_options& options)
        -> std::expected<agent_skill, std::string>
    {
        std::error_code error;
        auto root = std::filesystem::weakly_canonical(directory, error);
        if (error || !std::filesystem::is_directory(root))
            return std::unexpected("skill directory does not exist: " +
                directory.string());
        const auto entry = root / options.entry_file;
        auto instructions = read_file(entry, options.max_file_bytes);
        if (!instructions)
            return std::unexpected(instructions.error());

        agent_skill skill{.name = root.filename().string()};
        parse_skill_document(std::move(*instructions), skill);
        if (skill.description.empty())
            skill.description = "Agent skill " + skill.name;

        std::size_t total_bytes = skill.instructions.size();
        for (std::filesystem::recursive_directory_iterator iterator{
                 root, std::filesystem::directory_options::skip_permission_denied,
                 error};
            iterator != std::default_sentinel; iterator.increment(error))
        {
            if (error)
                return std::unexpected("cannot enumerate skill directory: " +
                    error.message());
            const auto& item = *iterator;
            if (item.is_symlink(error))
            {
                if (item.is_directory(error))
                    iterator.disable_recursion_pending();
                continue;
            }
            if (!item.is_regular_file(error) || item.path() == entry)
                continue;
            auto relative = std::filesystem::relative(item.path(), root, error);
            if (error || relative.empty() || *relative.begin() == "..")
                return std::unexpected("skill resource escapes its directory");
            const auto name = relative.generic_string();
            if (!options.include_hidden_files &&
                std::ranges::any_of(relative, [](const auto& component)
                    {
                        return component.string().starts_with('.');
                    }))
                continue;
            auto resource = read_file(item.path(), options.max_file_bytes);
            if (!resource)
                return std::unexpected(resource.error());
            total_bytes += resource->size();
            if (total_bytes > options.max_total_bytes)
                return std::unexpected(std::format(
                    "skill '{}' exceeds {} total bytes",
                    skill.name, options.max_total_bytes));
            skill.resources.emplace(name, std::move(*resource));
        }
        return skill;
    }
} // namespace

auto skill_catalog::add(agent_skill skill) -> std::expected<void, std::string>
{
    if (skill.name.empty())
        return std::unexpected("skill name cannot be empty");
    if (skill.description.empty())
        return std::unexpected("skill description cannot be empty: " + skill.name);
    if (skill.instructions.empty())
        return std::unexpected("skill instructions cannot be empty: " + skill.name);
    if (skills_.contains(skill.name))
        return std::unexpected("duplicate skill: " + skill.name);
    skills_.emplace(skill.name, std::move(skill));
    return {};
}

auto skill_catalog::contains(std::string_view name) const -> bool
{
    return skills_.contains(name);
}

auto skill_catalog::size() const noexcept -> std::size_t
{
    return skills_.size();
}

auto skill_catalog::format_available_skills() const -> std::string
{
    std::string result;
    for (const auto& [name, skill] : skills_)
        result += std::format("- {}: {}\n", name, skill.description);
    return result;
}

auto skill_catalog::is_dynamic() const noexcept -> bool
{
    return true;
}

auto skill_catalog::provide(const tool_provider_request& request)
    -> task<std::expected<tool_provider_result, std::string>>
{
    std::set<std::string, std::less<>> active;
    for (const auto& value : request.conversation)
    {
        if (auto activated = parse_activation(value, "activated_skill"))
            active.insert(std::move(*activated));
        if (auto deactivated = parse_activation(value, "deactivated_skill"))
            active.erase(*deactivated);
    }

    tool_provider_result result;
    result.tools.push_back({.definition = {.function_name = "activate_skill",
                                .function_description =
                                    "Activate a skill and load its instructions",
                                .function_parameters = {
                                    {"type", "object"},
                                    {"properties", {{"name", {{"type", "string"}}}}},
                                    {"required", {"name"}},
                                    {"additionalProperties", false}}},
        .handler = [this](const json& arguments) -> task<std::expected<json, std::string>>
        {
            const auto name = cnetmod::json::value_or(
                arguments, "name", std::string{});
            const auto found = skills_.find(name);
            if (found == skills_.end())
                co_return std::unexpected("unknown skill: " + name);
            std::vector<std::string> resources;
            for (const auto& [resource_name, content] : found->second.resources)
            {
                (void)content;
                resources.push_back(resource_name);
            }
            auto wire_resources = cnetmod::json::array();
            for (const auto& resource : resources)
                wire_resources.get_array().emplace_back(resource);
            co_return cnetmod::json::object({{"activated_skill", name},
                {"instructions", found->second.instructions},
                {"resources", std::move(wire_resources)}});
        },
        .visibility = tool_visibility::always_visible});
    result.tools.push_back({.definition = {.function_name = "deactivate_skill",
                                .function_description = "Deactivate a previously activated skill",
                                .function_parameters = {
                                    {"type", "object"},
                                    {"properties", {{"name", {{"type", "string"}}}}},
                                    {"required", {"name"}},
                                    {"additionalProperties", false}}},
        .handler = [this](const json& arguments) -> task<std::expected<json, std::string>>
        {
            const auto name = cnetmod::json::value_or(
                arguments, "name", std::string{});
            if (!skills_.contains(name))
                co_return std::unexpected("unknown skill: " + name);
            co_return json{{"deactivated_skill", name}};
        },
        .visibility = tool_visibility::always_visible});
    result.tools.push_back({.definition = {.function_name = "read_skill_resource",
                                .function_description = "Read a resource from an active skill",
                                .function_parameters = {
                                    {"type", "object"},
                                    {"properties",
                                        {{"skill", {{"type", "string"}}},
                                            {"resource", {{"type", "string"}}}}},
                                    {"required", {"skill", "resource"}},
                                    {"additionalProperties", false}}},
        .handler = [this, active](const json& arguments) -> task<std::expected<json, std::string>>
        {
            const auto skill_name = cnetmod::json::value_or(
                arguments, "skill", std::string{});
            const auto resource_name = cnetmod::json::value_or(
                arguments, "resource", std::string{});
            if (!active.contains(skill_name))
                co_return std::unexpected(
                    "skill is not active: " + skill_name);
            const auto skill = skills_.find(skill_name);
            if (skill == skills_.end())
                co_return std::unexpected("unknown skill: " + skill_name);
            const auto resource = skill->second.resources.find(resource_name);
            if (resource == skill->second.resources.end())
                co_return std::unexpected("unknown skill resource: " +
                    resource_name);
            co_return json{{"skill", skill_name},
                {"resource", resource_name}, {"content", resource->second}};
        },
        .visibility = tool_visibility::always_visible});

    for (const auto& name : active)
    {
        const auto found = skills_.find(name);
        if (found == skills_.end())
            continue;
        result.tools.insert(result.tools.end(), found->second.tools.begin(),
            found->second.tools.end());
        for (auto* provider : found->second.tool_providers)
        {
            if (!provider)
                continue;
            auto provided = co_await provider->provide(request);
            if (!provided)
                co_return std::unexpected(std::format(
                    "skill '{}' tool provider failed: {}", name,
                    provided.error()));
            result.tools.insert(result.tools.end(),
                std::make_move_iterator(provided->tools.begin()),
                std::make_move_iterator(provided->tools.end()));
        }
    }
    co_return result;
}

filesystem_skill_loader::filesystem_skill_loader(io_context& context,
    thread_pool& pool, skill_loader_options options)
    : context_(context), pool_(pool), options_(std::move(options))
{
}

auto filesystem_skill_loader::load(std::filesystem::path directory,
    const run_config& config)
    -> task<std::expected<agent_skill, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("skill loading cancelled");
    auto loaded = co_await blocking_invoke(pool_, context_,
        [directory = std::move(directory), options = options_]
        {
            return load_skill_directory(directory, options);
        });
    if (config.is_cancelled())
        co_return std::unexpected("skill loading cancelled");
    co_return loaded;
}

} // namespace cnetmod::openai
