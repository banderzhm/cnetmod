#include "test_framework.hpp"

import std;
import cnetmod.core.error;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.tracing;

namespace observation = cnetmod::instrumentation;

TEST(disabled_scope_does_not_evaluate_metadata)
{
    bool evaluated = false;
    auto scope = observation::operation_scope::start({}, [&]
        {
            evaluated = true;
            throw std::runtime_error("must not be called");
            return observation::active_span{};
        });
    ASSERT_FALSE(evaluated);
    ASSERT_TRUE(scope.context() == nullptr);
    scope.annotate([&]
        {
            evaluated = true;
            return std::vector<std::pair<std::string, std::string>>{};
        });
    ASSERT_FALSE(evaluated);
    scope.complete();
}

TEST(sampler_without_exporter_keeps_scope_disabled)
{
    unsigned samples = 0;
    unsigned factories = 0;
    observation::span_exporter sink{{}, [&](const auto&)
        {
            ++samples;
            return true;
        }};
    ASSERT_FALSE(static_cast<bool>(sink));
    auto scope = observation::operation_scope::start(sink, [&]
        {
            ++factories;
            return observation::start_client_span({}, "disabled");
        });
    scope.annotate([&]
        {
            ++factories;
            return std::vector<std::pair<std::string, std::string>>{};
        });
    scope.complete();
    ASSERT_TRUE(scope.context() == nullptr);
    ASSERT_EQ(samples, 0U);
    ASSERT_EQ(factories, 0U);
}

TEST(completion_reentry_cannot_export_twice_or_evaluate_metadata)
{
    unsigned exports = 0;
    unsigned annotations = 0;
    observation::operation_scope scope;
    observation::span_exporter sink = [&](const auto&)
    {
        ++exports;
        ASSERT_TRUE(scope.context() == nullptr);
        if (exports != 1)
            return;
        scope.annotate([&]
            {
                ++annotations;
                return std::vector<std::pair<std::string, std::string>>{};
            });
        scope.complete();
    };
    scope = observation::operation_scope::start(sink, []
        {
            return observation::start_client_span({}, "reentrant-completion");
        });
    scope.complete();
    ASSERT_EQ(exports, 1U);
    ASSERT_EQ(annotations, 0U);
}

TEST(operation_moves_and_completes_exactly_once)
{
    unsigned calls = 0;
    observation::completed_span result;
    observation::span_exporter sink = [&](const auto& span)
    {
        ++calls;
        result = span;
    };
    const auto parent = observation::new_root_context();
    {
        auto first = observation::operation_scope::start(sink, [&]
            {
                return observation::start_client_span(parent, "query");
            });
        auto second = std::move(first);
        ASSERT_TRUE(first.context() == nullptr);
        ASSERT_EQ(second.context()->trace_id, parent.trace_id);
        second.complete({observation::operation_status::timeout,
            std::make_error_code(std::errc::timed_out)});
        second.complete();
    }
    ASSERT_EQ(calls, 1U);
    ASSERT_TRUE(result.failed);
    ASSERT_TRUE(result.result.status == observation::operation_status::timeout);
    ASSERT_TRUE(result.result.error == std::errc::timed_out);
    ASSERT_EQ(result.parent_span_id, parent.span_id);
}

TEST(abandonment_and_throwing_exporters_are_contained)
{
    unsigned calls = 0;
    bool abandoned = false;
    observation::span_exporter sink = [&](const auto& span)
    {
        ++calls;
        abandoned = span.result.status == observation::operation_status::abandoned;
        throw std::runtime_error("export failed");
    };
    {
        auto scope = observation::operation_scope::start(sink, []
            {
                return observation::start_client_span({}, "unfinished");
            });
    }
    ASSERT_EQ(calls, 1U);
    ASSERT_TRUE(abandoned);
    auto scope = observation::operation_scope::start(sink, []() -> observation::active_span
        {
            throw std::runtime_error("metadata failed");
        });
    ASSERT_TRUE(scope.context() == nullptr);
    ASSERT_EQ(calls, 1U);
}

TEST(move_assignment_closes_replaced_operation_and_preserves_cancellation)
{
    std::vector<observation::operation_status> outcomes;
    observation::span_exporter sink = [&](const auto& span)
    {
        outcomes.push_back(span.result.status);
        ASSERT_FALSE(span.failed &&
            span.result.status == observation::operation_status::cancelled);
    };
    auto create = []
    {
        return observation::start_client_span({}, "operation");
    };
    {
        auto first = observation::operation_scope::start(sink, create);
        auto second = observation::operation_scope::start(sink, create);
        first = std::move(second);
        ASSERT_TRUE(second.context() == nullptr);
        first.complete({observation::operation_status::cancelled,
            std::make_error_code(std::errc::operation_canceled)});
    }
    ASSERT_EQ(outcomes.size(), std::size_t{2});
    ASSERT_TRUE(outcomes[0] == observation::operation_status::abandoned);
    ASSERT_TRUE(outcomes[1] == observation::operation_status::cancelled);
}

TEST(invalid_parent_does_not_create_a_dangling_parent_span)
{
    observation::trace_context invalid{.span_id = "invalid-parent"};
    const auto span = observation::start_client_span(invalid, "root");
    ASSERT_TRUE(span.parent_span_id.empty());
    ASSERT_TRUE(observation::valid_trace_context(span.context));
}

TEST(error_classification_preserves_framework_and_standard_codes)
{
    using observation::operation_status;
    const std::array cases{
        std::pair{std::error_code{}, operation_status::success},
        std::pair{std::error_code{0, std::system_category()}, operation_status::success},
        std::pair{cnetmod::make_error_code(cnetmod::errc::success), operation_status::success},
        std::pair{std::make_error_code(std::errc::operation_canceled), operation_status::cancelled},
        std::pair{cnetmod::make_error_code(cnetmod::errc::operation_aborted), operation_status::cancelled},
        std::pair{std::make_error_code(std::errc::timed_out), operation_status::timeout},
        std::pair{cnetmod::make_error_code(cnetmod::errc::connection_timed_out), operation_status::timeout},
        std::pair{std::make_error_code(std::errc::connection_reset), operation_status::error},
        std::pair{cnetmod::make_error_code(cnetmod::errc::connection_aborted), operation_status::error},
        std::pair{std::error_code{10, std::generic_category()}, operation_status::error}};
    for (const auto& [code, expected] : cases)
    {
        const auto result = observation::classify_error(code);
        ASSERT_TRUE(result.status == expected);
        ASSERT_TRUE(result.error == code);
        ASSERT_TRUE(&result.error.category() == &code.category());
    }
}

TEST(classified_completion_preserves_cancellation_and_timeout_semantics)
{
    for (const auto error : {std::error_code{},
             std::make_error_code(std::errc::operation_canceled),
             cnetmod::make_error_code(cnetmod::errc::operation_aborted),
             std::make_error_code(std::errc::timed_out),
             std::make_error_code(std::errc::connection_refused)})
    {
        const auto expected = observation::classify_error(error);
        unsigned exports = 0;
        observation::span_exporter sink = [&](const auto& span)
        {
            ++exports;
            ASSERT_TRUE(span.result.status == expected.status);
            ASSERT_EQ(span.result.error, error);
            ASSERT_EQ(span.failed, expected.status == observation::operation_status::error || expected.status == observation::operation_status::timeout);
        };
        {
            auto scope = observation::operation_scope::start(sink, []
                {
                    return observation::start_client_span({}, "classified-operation");
                });
            scope.complete(expected);
        }
        ASSERT_EQ(exports, 1U);
    }
}

TEST(head_sampling_preserves_parent_decisions_and_skips_recording)
{
    unsigned samples = 0;
    unsigned exports = 0;
    unsigned annotations = 0;
    bool accept = false;
    observation::span_exporter sink{
        [&](const auto&)
        {
            ++exports;
        },
        [&](const auto&)
        {
            ++samples;
            return accept;
        }};
    auto root = observation::operation_scope::start(sink, []
        {
            return observation::start_client_span({}, "root");
        });
    ASSERT_TRUE(root.context() != nullptr);
    ASSERT_TRUE(observation::valid_trace_context(*root.context()));
    ASSERT_EQ(root.context()->flags & 1U, 0U);
    accept = true;
    auto child = observation::operation_scope::start(sink, [&]
        {
            return observation::start_client_span(*root.context(), "child");
        });
    ASSERT_EQ(child.context()->trace_id, root.context()->trace_id);
    ASSERT_EQ(child.context()->flags & 1U, 0U);
    child.annotate([&]
        {
            ++annotations;
            return std::vector<std::pair<std::string, std::string>>{};
        });
    child.complete();
    root.complete();
    ASSERT_EQ(samples, 1U);
    ASSERT_EQ(exports, 0U);
    ASSERT_EQ(annotations, 0U);
    auto parent = observation::new_root_context();
    accept = false;
    auto sampled = observation::operation_scope::start(sink, [&]
        {
            return observation::start_client_span(parent, "sampled-child");
        });
    ASSERT_EQ(sampled.context()->flags & 1U, 1U);
    sampled.complete();
    ASSERT_EQ(samples, 1U);
    ASSERT_EQ(exports, 1U);
}

TEST(throwing_root_sampler_does_not_escape_or_export)
{
    unsigned exports = 0;
    observation::span_exporter sink{
        [&](const auto&)
        {
            ++exports;
        },
        [](const auto&) -> bool
        {
            throw std::runtime_error("sampler failed");
        }};
    auto scope = observation::operation_scope::start(sink, []
        {
            return observation::start_client_span({}, "root");
        });
    ASSERT_TRUE(scope.context() != nullptr);
    ASSERT_EQ(scope.context()->flags & 1U, 0U);
    scope.complete();
    ASSERT_EQ(exports, 0U);
}

RUN_TESTS();
