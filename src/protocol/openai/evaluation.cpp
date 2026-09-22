/// cnetmod.protocol.openai:evaluation — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :chat;
import :messages;
import :model;
import :prompt;
import :evaluation;
import cnetmod.json;

namespace cnetmod::openai {

namespace {
    auto normalize_text(std::string value, exact_match_options options)
        -> std::string
    {
        if (options.trim_whitespace)
        {
            const auto content = std::string_view{value};
            const auto first = content.find_first_not_of(" \t\r\n");
            const auto last = content.find_last_not_of(" \t\r\n");
            value = first == std::string_view::npos
                ? std::string{}
                : std::string{content.substr(first, last - first + 1)};
        }
        if (!options.case_sensitive)
            std::ranges::transform(value, value.begin(), [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
        return value;
    }

    auto cosine_similarity(std::span<const float> left,
        std::span<const float> right) -> std::expected<double, std::string>
    {
        if (left.empty() || left.size() != right.size())
            return std::unexpected("evaluation embeddings have incompatible dimensions");
        double dot = 0.0;
        double left_norm = 0.0;
        double right_norm = 0.0;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            dot += static_cast<double>(left[index]) * right[index];
            left_norm += static_cast<double>(left[index]) * left[index];
            right_norm += static_cast<double>(right[index]) * right[index];
        }
        if (left_norm == 0.0 || right_norm == 0.0)
            return std::unexpected("evaluation embedding norm cannot be zero");
        return std::clamp(dot / std::sqrt(left_norm * right_norm), -1.0, 1.0);
    }
} // namespace

exact_match_evaluator::exact_match_evaluator(exact_match_options options) noexcept
    : options_(options)
{
}

auto exact_match_evaluator::name() const -> std::string_view
{
    return "exact_match";
}

auto exact_match_evaluator::evaluate(const evaluation_case& sample,
    const run_config& config)
    -> task<std::expected<evaluation_score, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("evaluation cancelled");
    const auto passed = normalize_text(sample.expected_output, options_) ==
        normalize_text(sample.actual_output, options_);
    co_return evaluation_score{.evaluator = std::string{name()},
        .value = passed ? 1.0 : 0.0,
        .passed = passed,
        .explanation = passed ? "outputs match" : "outputs differ"};
}

semantic_similarity_evaluator::semantic_similarity_evaluator(
    embedding_model& model, double passing_score)
    : model_(model), passing_score_(passing_score)
{
    if (passing_score < -1.0 || passing_score > 1.0)
        throw std::invalid_argument("semantic passing score must be in [-1, 1]");
}

auto semantic_similarity_evaluator::name() const -> std::string_view
{
    return "semantic_similarity";
}

auto semantic_similarity_evaluator::evaluate(const evaluation_case& sample,
    const run_config& config)
    -> task<std::expected<evaluation_score, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("evaluation cancelled");
    auto vectors = co_await model_.embed_documents(
        {sample.expected_output, sample.actual_output});
    if (!vectors)
        co_return std::unexpected("evaluation embedding failed: " + vectors.error());
    if (vectors->size() != 2)
        co_return std::unexpected("evaluation embedding model returned an invalid count");
    auto similarity = cosine_similarity((*vectors)[0], (*vectors)[1]);
    if (!similarity)
        co_return std::unexpected(similarity.error());
    co_return evaluation_score{.evaluator = std::string{name()},
        .value = *similarity,
        .passed = *similarity >= passing_score_,
        .explanation = std::format("cosine similarity {:.6f}, threshold {:.6f}",
            *similarity, passing_score_)};
}

model_judge_evaluator::model_judge_evaluator(chat_model& model,
    model_judge_options options)
    : model_(model), options_(std::move(options))
{
    if (options_.criterion.empty())
        throw std::invalid_argument("model judge criterion cannot be empty");
    if (options_.passing_score < 0.0 || options_.passing_score > 1.0)
        throw std::invalid_argument("model judge passing score must be in [0, 1]");
}

auto model_judge_evaluator::name() const -> std::string_view
{
    return "model_judge";
}

auto model_judge_evaluator::evaluate(const evaluation_case& sample,
    const run_config& config)
    -> task<std::expected<evaluation_score, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("evaluation cancelled");
    const json schema{{"type", "object"},
        {"properties",
            {{"score", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                {"explanation", {{"type", "string"}}}}},
        {"required", {"score", "explanation"}},
        {"additionalProperties", false}};
    chat_request request;
    if (!options_.model.empty())
        request.model = options_.model;
    request.temperature = 0.0;
    request.response_format = "json_schema";
    request.response_schema_name = "evaluation_score";
    request.response_schema = schema;
    request.response_schema_strict = true;
    request.messages = {message::system(
                            "You are a deterministic response quality evaluator. " "Apply the supplied criterion and return only the requested JSON."),
        message::user(json{{"criterion", options_.criterion},
            {"input", sample.input}, {"expected_output", sample.expected_output},
            {"actual_output", sample.actual_output}, {"metadata", sample.metadata}}
                .dump())};
    auto response = co_await model_.invoke(std::move(request), config);
    if (!response)
        co_return std::unexpected("model judge failed: " + response.error());
    if (response->choices.empty())
        co_return std::unexpected("model judge returned no choices");
    auto verdict = json::parse(
        response->choices.front().msg.content, nullptr, false);
    if (verdict.is_discarded())
        co_return std::unexpected("model judge returned invalid JSON");
    auto valid = validate_json_schema(verdict, schema);
    if (!valid)
        co_return std::unexpected(
            "model judge response validation failed: " + valid.error());
    const auto score = verdict["score"].get<double>();
    co_return evaluation_score{.evaluator = std::string{name()},
        .value = score,
        .passed = score >= options_.passing_score,
        .explanation = verdict["explanation"].get<std::string>()};
}

auto evaluation_suite::add(response_evaluator& evaluator)
    -> std::expected<void, std::string>
{
    if (std::ranges::any_of(evaluators_, [&](const auto* existing)
            {
                return existing == &evaluator || existing->name() == evaluator.name();
            }))
        return std::unexpected("duplicate evaluator: " + std::string{evaluator.name()});
    evaluators_.push_back(&evaluator);
    return {};
}

auto evaluation_suite::run(std::span<const evaluation_case> cases,
    const run_config& config)
    -> task<std::expected<evaluation_report, std::string>>
{
    if (evaluators_.empty())
        co_return std::unexpected("evaluation suite has no evaluators");
    evaluation_report report;
    report.entries.reserve(cases.size() * evaluators_.size());
    double total = 0.0;
    for (const auto& sample : cases)
    {
        for (auto* evaluator : evaluators_)
        {
            auto score = co_await evaluator->evaluate(sample, config);
            if (!score)
                co_return std::unexpected(std::format(
                    "evaluation case '{}' failed in '{}': {}", sample.id,
                    evaluator->name(), score.error()));
            total += score->value;
            score->passed ? ++report.passed : ++report.failed;
            report.entries.push_back({sample.id, std::move(*score)});
        }
    }
    if (!report.entries.empty())
        report.average_score = total / static_cast<double>(report.entries.size());
    co_return report;
}

} // namespace cnetmod::openai
