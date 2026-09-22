/// cnetmod.protocol.openai:guardrails — Composable input and output policy pipeline

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:guardrails;

import std;
import cnetmod.coro.task;
import :messages;
import :model;
import :prompt;
import :client;
import :moderation;
import cnetmod.json;

namespace cnetmod::openai {

export enum class guardrail_action
{
    accept,
    replace,
    reject,
    retry
};

export struct guardrail_result
{
    guardrail_action action = guardrail_action::accept;
    std::optional<message> replacement;
    std::string reason;
};

export class input_guardrail
{
public:
    virtual ~input_guardrail() = default;
    virtual auto validate(const message& input, const run_config& config)
        -> task<std::expected<guardrail_result, std::string>> = 0;
};

export class output_guardrail
{
public:
    virtual ~output_guardrail() = default;
    virtual auto validate(const message& output, const run_config& config)
        -> task<std::expected<guardrail_result, std::string>> = 0;
};

/// Chain of Responsibility for policy checks and controlled rewrites.
export class guardrail_pipeline
{
public:
    void add(input_guardrail& guardrail);
    void add(output_guardrail& guardrail);

    auto apply_input(message input, const run_config& config = {}) const
        -> task<std::expected<message, std::string>>;
    auto apply_output(message output, const run_config& config = {}) const
        -> task<std::expected<guardrail_result, std::string>>;

private:
    std::vector<input_guardrail*> input_guardrails_;
    std::vector<output_guardrail*> output_guardrails_;
};

/// Fast deterministic first-line defense for known prompt-injection phrases.
export class pattern_input_guardrail final : public input_guardrail
{
public:
    explicit pattern_input_guardrail(std::vector<std::string> blocked_patterns);
    auto validate(const message& input, const run_config& config)
        -> task<std::expected<guardrail_result, std::string>> override;

private:
    std::vector<std::string> blocked_patterns_;
};

export class moderation_input_guardrail final : public input_guardrail
{
public:
    explicit moderation_input_guardrail(client& api,
        std::string model = "omni-moderation-latest");
    explicit moderation_input_guardrail(moderation_model& model,
        std::string model_name = "omni-moderation-latest");
    auto validate(const message& input, const run_config& config)
        -> task<std::expected<guardrail_result, std::string>> override;

private:
    std::unique_ptr<openai_moderation_model> owned_model_;
    moderation_model* model_ = nullptr;
    std::string model_name_;
};

/// Validates model content against JSON Schema and optionally requests a retry.
export class json_schema_output_guardrail final : public output_guardrail
{
public:
    explicit json_schema_output_guardrail(json schema,
        bool retry_on_failure = true);
    auto validate(const message& output, const run_config& config)
        -> task<std::expected<guardrail_result, std::string>> override;

private:
    json schema_;
    bool retry_on_failure_;
};

} // namespace cnetmod::openai
