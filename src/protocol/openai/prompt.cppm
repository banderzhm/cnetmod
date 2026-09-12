/// cnetmod.protocol.openai:prompt — Prompt templates and structured parsers

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:prompt;

import std;
import :foundation;
import :messages;
import nlohmann.json;

namespace cnetmod::openai {

export using prompt_variables = std::map<std::string, std::string>;

export class prompt_template
{
public:
    explicit prompt_template(std::string source = {});
    [[nodiscard]] auto format(const prompt_variables& variables) const
        -> std::expected<std::string, std::string>;
    [[nodiscard]] auto source() const noexcept -> std::string_view;
    [[nodiscard]] auto variables() const -> std::vector<std::string>;

private:
    std::string source_;
};

export struct message_prompt
{
    std::string role;
    prompt_template prompt;
};

export class chat_prompt_template
{
public:
    explicit chat_prompt_template(std::vector<message_prompt> prompts = {});
    void add(std::string role, prompt_template prompt);
    [[nodiscard]] auto format(const prompt_variables& variables) const
        -> std::expected<std::vector<message>, std::string>;

private:
    std::vector<message_prompt> prompts_;
};

export class output_parser
{
public:
    virtual ~output_parser() = default;
    [[nodiscard]] virtual auto parse(std::string_view text) const
        -> std::expected<json, std::string> = 0;
};

export class string_output_parser final : public output_parser
{
public:
    [[nodiscard]] auto parse(std::string_view text) const
        -> std::expected<json, std::string> override;
};

export class json_output_parser final : public output_parser
{
public:
    explicit json_output_parser(json schema = json::object());
    [[nodiscard]] auto parse(std::string_view text) const
        -> std::expected<json, std::string> override;

private:
    json schema_;
};

/// Validates the JSON Schema subset used by OpenAI structured output:
/// type, required, properties, items, enum and additionalProperties.
export [[nodiscard]] auto validate_json_schema(const json& value,
    const json& schema, std::string_view path = "$")
    -> std::expected<void, std::string>;

} // namespace cnetmod::openai
