/// cnetmod.protocol.openai:prompt — Prompt templates and structured parsers

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:prompt;

import std;
import :foundation;
import :messages;
import cnetmod.json;

namespace cnetmod::openai {

export using prompt_variables =
    std::map<std::string, std::string, std::less<>>;

export struct prompt_section;
export using prompt_sections =
    std::map<std::string, std::vector<prompt_section>, std::less<>>;

/**
 * Defines one repeated row with local variables and nested sections.
 */
export struct prompt_section
{
    prompt_section();
    prompt_section(prompt_variables variables);
    auto with_sections(prompt_sections nested) && -> prompt_section;

    prompt_variables variables;
    prompt_sections sections;
};

/**
 * Supplies global scalar variables and recursively repeated sections.
 *
 * Section rows form a local scope whose values take precedence over global
 * variables while that row is rendered. Nested sections resolve from the
 * current row before falling back to the root context.
 */
export struct prompt_context
{
    prompt_variables variables;
    prompt_sections sections;
};

/**
 * Renders strict variables, defaults, conditions, and repeated sections.
 *
 * Supported forms are `{name}`, `{name|default}`,
 * `{?name}...{/name}`, and `{#items}...{/items}`. Double braces emit literal
 * braces. Missing strict variables remain errors for backward compatibility.
 */
export class prompt_template
{
public:
    explicit prompt_template(std::string source = {});
    [[nodiscard]] auto format(const prompt_variables& variables) const
        -> std::expected<std::string, std::string>;
    [[nodiscard]] auto format_context(const prompt_context& context) const
        -> std::expected<std::string, std::string>;
    [[nodiscard]] auto source() const noexcept -> std::string_view;
    [[nodiscard]] auto variables() const -> std::vector<std::string>;

private:
    std::string source_;
};

/**
 * Associates a protocol role with a prompt template.
 */
export struct message_prompt
{
    std::string role;
    prompt_template prompt;
};

/**
 * Renders an ordered group of role-specific message templates.
 */
export class chat_prompt_template
{
public:
    explicit chat_prompt_template(std::vector<message_prompt> prompts = {});
    void add(std::string role, prompt_template prompt);
    [[nodiscard]] auto format(const prompt_variables& variables) const
        -> std::expected<std::vector<message>, std::string>;
    [[nodiscard]] auto format_context(const prompt_context& context) const
        -> std::expected<std::vector<message>, std::string>;

private:
    std::vector<message_prompt> prompts_;
};

/**
 * Parses model output when an application explicitly requires structured data.
 */
export class output_parser
{
public:
    virtual ~output_parser() = default;
    [[nodiscard]] virtual auto parse(std::string_view text) const
        -> std::expected<json, std::string> = 0;
};

/**
 * Returns model output as a JSON string value without structural validation.
 */
export class string_output_parser final : public output_parser
{
public:
    [[nodiscard]] auto parse(std::string_view text) const
        -> std::expected<json, std::string> override;
};

/**
 * Parses JSON model output and optionally validates a JSON Schema subset.
 */
export class json_output_parser final : public output_parser
{
public:
    explicit json_output_parser(json schema = cnetmod::json::object());
    [[nodiscard]] auto parse(std::string_view text) const
        -> std::expected<json, std::string> override;

private:
    json schema_;
};

/**
 * Validates the JSON Schema subset used by OpenAI structured output.
 *
 * Supported keywords are type, required, properties, items, enum, and
 * additionalProperties.
 */
export [[nodiscard]] auto validate_json_schema(const json& value,
    const json& schema, std::string_view path = "$")
    -> std::expected<void, std::string>;

} // namespace cnetmod::openai
