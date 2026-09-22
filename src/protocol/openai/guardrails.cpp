/// cnetmod.protocol.openai:guardrails — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :messages;
import :model;
import :prompt;
import :client;
import :moderation;
import :guardrails;
import cnetmod.json;

namespace cnetmod::openai {

namespace {
    auto lowercase(std::string_view value) -> std::string
    {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
        return result;
    }
} // namespace

void guardrail_pipeline::add(input_guardrail& guardrail)
{
    input_guardrails_.push_back(&guardrail);
}

void guardrail_pipeline::add(output_guardrail& guardrail)
{
    output_guardrails_.push_back(&guardrail);
}

auto guardrail_pipeline::apply_input(message input,
    const run_config& config) const
    -> task<std::expected<message, std::string>>
{
    for (auto* guardrail : input_guardrails_)
    {
        if (config.is_cancelled())
            co_return std::unexpected("input guardrail evaluation cancelled");
        auto result = co_await guardrail->validate(input, config);
        if (!result)
            co_return std::unexpected(result.error());
        if (result->action == guardrail_action::reject ||
            result->action == guardrail_action::retry)
            co_return std::unexpected(result->reason);
        if (result->action == guardrail_action::replace)
        {
            if (!result->replacement)
                co_return std::unexpected("input guardrail returned an empty replacement");
            input = std::move(*result->replacement);
        }
    }
    co_return input;
}

auto guardrail_pipeline::apply_output(message output,
    const run_config& config) const
    -> task<std::expected<guardrail_result, std::string>>
{
    for (auto* guardrail : output_guardrails_)
    {
        if (config.is_cancelled())
            co_return std::unexpected("output guardrail evaluation cancelled");
        auto result = co_await guardrail->validate(output, config);
        if (!result)
            co_return std::unexpected(result.error());
        if (result->action == guardrail_action::reject ||
            result->action == guardrail_action::retry)
            co_return *result;
        if (result->action == guardrail_action::replace)
        {
            if (!result->replacement)
                co_return std::unexpected("output guardrail returned an empty replacement");
            output = std::move(*result->replacement);
        }
    }
    co_return guardrail_result{.action = guardrail_action::accept,
        .replacement = std::move(output)};
}

pattern_input_guardrail::pattern_input_guardrail(
    std::vector<std::string> blocked_patterns)
{
    blocked_patterns_.reserve(blocked_patterns.size());
    for (auto& pattern : blocked_patterns)
        blocked_patterns_.push_back(lowercase(pattern));
}

auto pattern_input_guardrail::validate(const message& input,
    const run_config&) -> task<std::expected<guardrail_result, std::string>>
{
    const auto content = lowercase(input.content);
    for (const auto& pattern : blocked_patterns_)
    {
        if (!pattern.empty() && content.contains(pattern))
            co_return guardrail_result{.action = guardrail_action::reject,
                .reason = "input rejected by prompt-injection policy"};
    }
    co_return guardrail_result{};
}

moderation_input_guardrail::moderation_input_guardrail(client& api,
    std::string model)
    : owned_model_(std::make_unique<openai_moderation_model>(api)),
      model_(owned_model_.get()),
      model_name_(std::move(model))
{
}

moderation_input_guardrail::moderation_input_guardrail(
    moderation_model& model, std::string model_name)
    : model_(&model), model_name_(std::move(model_name))
{
}

auto moderation_input_guardrail::validate(const message& input,
    const run_config& config)
    -> task<std::expected<guardrail_result, std::string>>
{
    auto response = co_await model_->moderate(
        moderation_request{.model = model_name_, .input = {input.content}},
        config);
    if (!response)
        co_return std::unexpected("moderation request failed: " + response.error());
    if (std::ranges::any_of(response->results,
            [](const moderation_result& result)
            {
                return result.flagged;
            }))
        co_return guardrail_result{.action = guardrail_action::reject,
            .reason = "input rejected by content moderation policy"};
    co_return guardrail_result{};
}

json_schema_output_guardrail::json_schema_output_guardrail(json schema,
    bool retry_on_failure)
    : schema_(std::move(schema)), retry_on_failure_(retry_on_failure)
{
}

auto json_schema_output_guardrail::validate(const message& output,
    const run_config&) -> task<std::expected<guardrail_result, std::string>>
{
    const auto parsed = cnetmod::json::parse_document(output.content);
    if (!parsed)
        co_return guardrail_result{
            .action = retry_on_failure_ ? guardrail_action::retry
                                        : guardrail_action::reject,
            .reason = "model output is not valid JSON"};
    auto valid = validate_json_schema(*parsed, schema_);
    if (!valid)
        co_return guardrail_result{
            .action = retry_on_failure_ ? guardrail_action::retry
                                        : guardrail_action::reject,
            .reason = valid.error()};
    co_return guardrail_result{};
}

} // namespace cnetmod::openai
