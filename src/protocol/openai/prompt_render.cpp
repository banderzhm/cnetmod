/// cnetmod.protocol.openai:prompt — template rendering implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import :messages;
import :prompt;

namespace cnetmod::openai {

namespace {
    class template_renderer
    {
    public:
        template_renderer(std::string_view source, const prompt_context& context)
            : source_(source), context_(context)
        {
        }

        auto render() const -> std::expected<std::string, std::string>
        {
            return render_range(0, source_.size(), nullptr);
        }

    private:
        struct section_range
        {
            std::size_t body_end = 0;
            std::size_t section_end = 0;
        };

        auto scalar(std::string_view name, const prompt_section* local) const
            -> const std::string*
        {
            if (local)
            {
                const auto found = local->find(name);
                if (found != local->end())
                    return &found->second;
            }
            const auto found = context_.variables.find(name);
            return found == context_.variables.end() ? nullptr : &found->second;
        }

        auto truthy(std::string_view name, const prompt_section* local) const -> bool
        {
            if (const auto* value = scalar(name, local))
                return !value->empty();
            const auto section = context_.sections.find(name);
            return section != context_.sections.end() && !section->second.empty();
        }

        auto find_section_end(std::size_t body_begin, std::string_view name) const
            -> std::expected<section_range, std::string>
        {
            std::vector<std::string_view> stack{name};
            auto index = body_begin;
            while (index < source_.size())
            {
                if (source_.substr(index, 2) == "{{")
                {
                    index += 2;
                    continue;
                }
                if (source_[index] != '{')
                {
                    ++index;
                    continue;
                }
                const auto end = source_.find('}', index + 1);
                if (end == std::string_view::npos)
                    return std::unexpected(std::format(
                        "unclosed prompt token at byte {}", index));
                const auto token = source_.substr(index + 1, end - index - 1);
                if (token.starts_with('?') || token.starts_with('#'))
                {
                    if (token.size() == 1)
                        return std::unexpected("empty prompt section name");
                    stack.push_back(token.substr(1));
                }
                else if (token.starts_with('/'))
                {
                    if (token.size() == 1 || stack.empty() ||
                        token.substr(1) != stack.back())
                        return std::unexpected("mismatched prompt section close: " +
                            std::string(token));
                    stack.pop_back();
                    if (stack.empty())
                        return section_range{.body_end = index,
                            .section_end = end + 1};
                }
                index = end + 1;
            }
            return std::unexpected("unclosed prompt section: " + std::string(name));
        }

        auto render_range(std::size_t begin, std::size_t end,
            const prompt_section* local) const
            -> std::expected<std::string, std::string>
        {
            std::string output;
            output.reserve(end - begin);
            for (auto index = begin; index < end;)
            {
                if (source_.substr(index, 2) == "{{")
                {
                    output.push_back('{');
                    index += 2;
                    continue;
                }
                if (source_.substr(index, 2) == "}}")
                {
                    output.push_back('}');
                    index += 2;
                    continue;
                }
                if (source_[index] != '{')
                {
                    output.push_back(source_[index++]);
                    continue;
                }

                const auto token_end = source_.find('}', index + 1);
                if (token_end == std::string_view::npos || token_end >= end)
                    return std::unexpected(std::format(
                        "unclosed prompt token at byte {}", index));
                const auto token = source_.substr(
                    index + 1, token_end - index - 1);
                if (token.empty())
                    return std::unexpected("empty prompt token");
                if (token.starts_with('/'))
                    return std::unexpected("unexpected prompt section close: " +
                        std::string(token));

                if (token.starts_with('?') || token.starts_with('#'))
                {
                    const auto name = token.substr(1);
                    if (name.empty())
                        return std::unexpected("empty prompt section name");
                    auto range = find_section_end(token_end + 1, name);
                    if (!range || range->section_end > end)
                        return range ? std::unexpected(
                                           "prompt section crosses parent boundary: " +
                                           std::string(name))
                                     : std::unexpected(range.error());

                    if (token.front() == '?')
                    {
                        if (truthy(name, local))
                        {
                            auto rendered = render_range(token_end + 1,
                                range->body_end, local);
                            if (!rendered)
                                return rendered;
                            output += *rendered;
                        }
                    }
                    else
                    {
                        const auto rows = context_.sections.find(name);
                        if (rows != context_.sections.end())
                        {
                            for (const auto& row : rows->second)
                            {
                                auto rendered = render_range(token_end + 1,
                                    range->body_end, &row);
                                if (!rendered)
                                    return rendered;
                                output += *rendered;
                            }
                        }
                    }
                    index = range->section_end;
                    continue;
                }

                const auto separator = token.find('|');
                const auto name = token.substr(0, separator);
                if (name.empty())
                    return std::unexpected("empty prompt variable");
                if (const auto* value = scalar(name, local))
                    output += *value;
                else if (separator != std::string_view::npos)
                    output += token.substr(separator + 1);
                else
                    return std::unexpected("missing prompt variable: " +
                        std::string(name));
                index = token_end + 1;
            }
            return output;
        }

        std::string_view source_;
        const prompt_context& context_;
    };
} // namespace

prompt_template::prompt_template(std::string source) : source_(std::move(source)) {}

auto prompt_template::format(const prompt_variables& variables) const
    -> std::expected<std::string, std::string>
{
    return format_context(prompt_context{.variables = variables});
}

auto prompt_template::format_context(const prompt_context& context) const
    -> std::expected<std::string, std::string>
{
    return template_renderer{source_, context}.render();
}

auto prompt_template::source() const noexcept -> std::string_view
{
    return source_;
}

auto prompt_template::variables() const -> std::vector<std::string>
{
    std::vector<std::string> result;
    for (std::size_t index = 0; index < source_.size();)
    {
        if (source_.substr(index, 2) == "{{")
        {
            index += 2;
            continue;
        }
        if (source_[index] != '{')
        {
            ++index;
            continue;
        }
        const auto end = source_.find('}', index + 1);
        if (end == std::string::npos)
            break;
        auto token = source_.substr(index + 1, end - index - 1);
        if (token.starts_with('/') || token.starts_with('?') ||
            token.starts_with('#'))
            token.erase(0, 1);
        if (const auto separator = token.find('|'); separator != std::string::npos)
            token.erase(separator);
        if (!token.empty() &&
            std::ranges::find(result, token) == result.end())
            result.push_back(std::move(token));
        index = end + 1;
    }
    return result;
}

chat_prompt_template::chat_prompt_template(std::vector<message_prompt> prompts)
    : prompts_(std::move(prompts))
{
}

void chat_prompt_template::add(std::string role, prompt_template prompt)
{
    prompts_.push_back({std::move(role), std::move(prompt)});
}

auto chat_prompt_template::format(const prompt_variables& variables) const
    -> std::expected<std::vector<message>, std::string>
{
    return format_context(prompt_context{.variables = variables});
}

auto chat_prompt_template::format_context(const prompt_context& context) const
    -> std::expected<std::vector<message>, std::string>
{
    std::vector<message> result;
    result.reserve(prompts_.size());
    for (const auto& item : prompts_)
    {
        auto content = item.prompt.format_context(context);
        if (!content)
            return std::unexpected(content.error());
        result.push_back(message{.role = item.role,
            .content = std::move(*content)});
    }
    return result;
}

} // namespace cnetmod::openai
