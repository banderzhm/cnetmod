/// cnetmod.protocol.openai:evaluation — Repeatable response quality evaluation

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:evaluation;

import std;
import cnetmod.coro.task;
import :model;

namespace cnetmod::openai {

export struct evaluation_case
{
    std::string id;
    std::string input;
    std::string expected_output;
    std::string actual_output;
    std::map<std::string, std::string> metadata;
};

export struct evaluation_score
{
    std::string evaluator;
    double value = 0.0;
    bool passed = false;
    std::string explanation;
};

export class response_evaluator
{
public:
    virtual ~response_evaluator() = default;
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;
    virtual auto evaluate(const evaluation_case& sample,
        const run_config& config = {})
        -> task<std::expected<evaluation_score, std::string>> = 0;
};

export struct exact_match_options
{
    bool case_sensitive = true;
    bool trim_whitespace = true;
};

export class exact_match_evaluator final : public response_evaluator
{
public:
    explicit exact_match_evaluator(exact_match_options options = {}) noexcept;
    [[nodiscard]] auto name() const -> std::string_view override;
    auto evaluate(const evaluation_case& sample,
        const run_config& config = {})
        -> task<std::expected<evaluation_score, std::string>> override;

private:
    exact_match_options options_;
};

export class semantic_similarity_evaluator final : public response_evaluator
{
public:
    semantic_similarity_evaluator(embedding_model& model,
        double passing_score = 0.8);
    [[nodiscard]] auto name() const -> std::string_view override;
    auto evaluate(const evaluation_case& sample,
        const run_config& config = {})
        -> task<std::expected<evaluation_score, std::string>> override;

private:
    embedding_model& model_;
    double passing_score_;
};

export struct model_judge_options
{
    std::string criterion =
        "Score whether the actual output correctly satisfies the input and expected output.";
    double passing_score = 0.7;
    std::string model;
};

/// Uses a chat model as a schema-constrained quality judge.
export class model_judge_evaluator final : public response_evaluator
{
public:
    model_judge_evaluator(chat_model& model,
        model_judge_options options = {});
    [[nodiscard]] auto name() const -> std::string_view override;
    auto evaluate(const evaluation_case& sample,
        const run_config& config = {})
        -> task<std::expected<evaluation_score, std::string>> override;

private:
    chat_model& model_;
    model_judge_options options_;
};

export struct evaluation_entry
{
    std::string case_id;
    evaluation_score score;
};

export struct evaluation_report
{
    std::vector<evaluation_entry> entries;
    double average_score = 0.0;
    std::size_t passed = 0;
    std::size_t failed = 0;
};

/// Composite evaluator that runs every configured policy for every case.
export class evaluation_suite
{
public:
    [[nodiscard]] auto add(response_evaluator& evaluator)
        -> std::expected<void, std::string>;
    auto run(std::span<const evaluation_case> cases,
        const run_config& config = {})
        -> task<std::expected<evaluation_report, std::string>>;

private:
    std::vector<response_evaluator*> evaluators_;
};

} // namespace cnetmod::openai
