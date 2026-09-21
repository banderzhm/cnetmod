#include "test_framework.hpp"

#include <cnetmod/config.hpp>

import std;
import nlohmann.json;
import cnetmod.application;
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import cnetmod.observability.grpc;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import cnetmod.application.amqp091;
import cnetmod.application.service_lifecycle;
import cnetmod.application.service_registry;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.observability;
import cnetmod.core;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.protocol.amqp091;
import cnetmod.core.error;
import cnetmod.executor.async_op;

    #include "amqp091_confirmation_cases.inc"
    #include "amqp091_generation_cases.inc"
    #include "amqp091_recovery_cancellation_cases.inc"
    #include "amqp091_registration_rollback_cases.inc"
    #include "amqp091_service_recovery_cases.inc"
    #include "amqp091_start_admission_cases.inc"
    #include "amqp091_start_cleanup_cases.inc"
    #include "amqp091_start_timeout_cases.inc"
#endif
#ifdef CNETMOD_HAS_SSL
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;

TEST(application_transport_shutdown_honors_precancellation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto socket = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    auto ssl = cnetmod::ssl_context::client();
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(ssl.has_value());
    cnetmod::ssl_stream stream{*ssl, *io, *socket};
    stream.set_connect_state();
    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    auto shutdown = stream.async_shutdown(cancellation);
    shutdown.handle().resume();
    ASSERT_TRUE(shutdown.handle().done());
    auto result = shutdown.handle().promise().result();
    ASSERT_FALSE(result.has_value());
    if (!result)
        ASSERT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
}
#endif
#if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL) || defined(CNETMOD_HAS_PROTOCOL_MONGODB) || defined(CNETMOD_HAS_PROTOCOL_MYSQL) || defined(CNETMOD_HAS_PROTOCOL_REDIS)
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.observability;
import cnetmod.core;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import cnetmod.application.mysql;
    #ifdef CNETMOD_HAS_ORM
import cnetmod.application.mysql_orm;
    #endif
import cnetmod.protocol.mysql;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import cnetmod.ai;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.openai;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import cnetmod.application.redis;
import cnetmod.protocol.redis;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import cnetmod.application.service_lifecycle;
import cnetmod.application.service_registry;
import cnetmod.application.health_registry;
import cnetmod.application.postgresql;
import cnetmod.protocol.postgresql;
import cnetmod.executor.async_op;

    #include "postgresql_lifecycle_cases.inc"
    #include "postgresql_pool_shutdown_cases.inc"
    #include "postgresql_warmup_cases.inc"
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import cnetmod.application.mongodb;
import cnetmod.protocol.mongodb;
import cnetmod.executor.async_op;

    #include "mongodb_health_probe_cases.inc"
    #include "mongodb_service_shutdown_cases.inc"
    #include "mongodb_startup_timeout_cases.inc"
#endif
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import cnetmod.protocol.openai;
import cnetmod.application.openai;
import cnetmod.coro.cancel;
#endif

namespace application = cnetmod::application;

#if defined(CNETMOD_HAS_PROTOCOL_MYSQL) || defined(CNETMOD_HAS_PROTOCOL_REDIS)
import cnetmod.application.service_lifecycle;
import cnetmod.application.service_registry;
import cnetmod.application.health_registry;

    #include "application_recoverable_pool_ownership_cases.inc"
#endif

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import cnetmod.application.kafka;
import cnetmod.application.recovery_policy;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.observability;
import cnetmod.core;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.task_group;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.kafka;
import cnetmod.protocol.kafka.protocol_value_codec;
import cnetmod.observability.kafka_producer;
import cnetmod.observability.kafka_consumer;
import cnetmod.instrumentation.tracing;
import cnetmod.observability.messaging;

    #include "kafka_consumer_lifecycle_cases.inc"

namespace {
namespace kafka = cnetmod::kafka;

struct kafka_observation_backend final : kafka::producer_backend
{
    kafka::record received;
    kafka::error_code outcome = kafka::error_code::none;
    bool throws = false;
    std::function<void()> during_send;
    std::function<void()> during_initialize;
    unsigned batches = 0;
    std::vector<bool> transaction_completions;
    cnetmod::cancel_token* transaction_token = nullptr;
    std::string transaction_group;
    std::map<kafka::topic_partition, kafka::offset_and_metadata> transaction_offsets;
    kafka::error_code transaction_error = kafka::error_code::none;

    auto partitions(std::string_view) -> kafka::result<std::vector<std::int32_t>> override
    {
        return std::vector<std::int32_t>{0};
    }

    auto initialize_idempotent(std::optional<std::string_view>, std::chrono::milliseconds, cnetmod::cancel_token*)
        -> cnetmod::task<kafka::result<std::pair<std::int64_t, std::int16_t>>> override
    {
        if (during_initialize)
            during_initialize();
        co_return std::pair<std::int64_t, std::int16_t>{1, 0};
    }

    auto wait_for_linger(std::chrono::milliseconds, cnetmod::cancel_token*) -> cnetmod::task<kafka::result<void>> override
    {
        co_return kafka::result<void>{};
    }

    auto send_batch(const kafka::topic_partition& destination, std::span<const kafka::record> records,
        const kafka::record_batch_options&, kafka::acknowledgement,
        std::chrono::steady_clock::time_point, cnetmod::cancel_token*)
        -> cnetmod::task<kafka::result<std::vector<kafka::record_metadata>>> override
    {
        if (batches++ == 0)
            received = records.front();
        if (during_send)
            during_send();
        if (throws)
            throw std::runtime_error("backend exception");
        if (outcome != kafka::error_code::none)
            co_return std::unexpected(kafka::error{.code = outcome, .message = "backend diagnostic", .retriable = false});
        co_return std::vector<kafka::record_metadata>{{destination, 17, 123}};
    }

    auto add_transaction_partitions(std::string_view, std::int64_t, std::int16_t,
        std::span<const kafka::topic_partition>, cnetmod::cancel_token*) -> cnetmod::task<kafka::result<void>> override
    {
        co_return kafka::result<void>{};
    }

    auto add_transaction_offsets(std::string_view, std::int64_t, std::int16_t, std::string_view group,
        const std::map<kafka::topic_partition, kafka::offset_and_metadata>& offsets, cnetmod::cancel_token* token)
        -> cnetmod::task<kafka::result<void>> override
    {
        transaction_group = group;
        transaction_offsets = offsets;
        transaction_token = token;
        co_return kafka::result<void>{};
    }

    auto finish_transaction(std::string_view, std::int64_t, std::int16_t, bool commit, cnetmod::cancel_token* token)
        -> cnetmod::task<kafka::result<void>> override
    {
        transaction_completions.push_back(commit);
        transaction_token = token;
        if (transaction_error != kafka::error_code::none)
            co_return std::unexpected(kafka::error{.code = transaction_error, .message = "transaction diagnostic"});
        co_return kafka::result<void>{};
    }
};
} // namespace

TEST(kafka_observed_send_propagates_context_without_changing_payload)
{
    namespace kafka = cnetmod::kafka;
    using backend = kafka_observation_backend;

    for (const bool idempotent : {false, true})
        for (const bool throws : {false, true})
            for (const bool enabled : {false, true})
                for (const auto outcome : {kafka::error_code::none, kafka::error_code::cancelled,
                         kafka::error_code::request_timed_out, kafka::error_code::transport})
                {
                    auto transport = std::make_shared<backend>();
                    transport->outcome = outcome;
                    transport->throws = throws;
                    kafka::producer producer{transport, {.linger = std::chrono::milliseconds{0}, .idempotent = idempotent}};
                    std::optional<cnetmod::task<kafka::result<kafka::record_metadata>>> queued;
                    std::optional<cnetmod::task<kafka::result<void>>> flushing;
                    std::optional<cnetmod::task<kafka::result<void>>> second_flush;
                    transport->during_send = [&]
                    {
                        if (queued)
                            return;
                        queued.emplace(producer.send("orders", {}));
                        queued->handle().resume();
                        ASSERT_FALSE(queued->handle().done());
                        flushing.emplace(producer.flush());
                        flushing->handle().resume();
                        ASSERT_FALSE(flushing->handle().done());
                        {
                            auto withdrawn = producer.flush();
                            withdrawn.handle().resume();
                            ASSERT_FALSE(withdrawn.handle().done());
                            second_flush.emplace(producer.flush());
                            second_flush->handle().resume();
                            ASSERT_FALSE(second_flush->handle().done());
                        }
                    };
                    const auto parent = cnetmod::instrumentation::new_root_context();
                    std::vector<cnetmod::instrumentation::completed_span> spans;
                    cnetmod::instrumentation::span_exporter sink;
                    if (enabled)
                        sink = cnetmod::instrumentation::span_exporter{[&](const auto& span)
                            {
                                spans.push_back(span);
                            }};
                    kafka::record record;
                    record.value = kafka::bytes{std::byte{0x42}, std::byte{0xff}};
                    record.headers.push_back({"business", {std::byte{1}}});
                    kafka::result<kafka::record_metadata> result;
                    bool caught = false;
                    try
                    {
                        result = cnetmod::sync_wait(cnetmod::observability::send_kafka_record(
                            producer, "orders", std::move(record), parent, sink));
                    }
                    catch (const std::runtime_error& error)
                    {
                        caught = true;
                        ASSERT_EQ(std::string_view{error.what()}, std::string_view{"backend exception"});
                    }
                    ASSERT_EQ(caught, throws);
                    if (queued)
                    {
                        const bool settled = queued->handle().done();
                        if (!settled)
                            producer.close();
                        ASSERT_TRUE(settled);
                        bool queued_exception = false;
                        try
                        {
                            const auto queued_result = queued->handle().promise().result();
                            ASSERT_EQ(queued_result.has_value(), outcome == kafka::error_code::none);
                            if (queued_result)
                            {
                                ASSERT_EQ(queued_result->target.topic, std::string{"orders"});
                                ASSERT_EQ(queued_result->offset, std::int64_t{17});
                            }
                            else
                            {
                                ASSERT_TRUE(queued_result.error().code == outcome);
                                ASSERT_EQ(queued_result.error().message, std::string{"backend diagnostic"});
                            }
                        }
                        catch (const std::runtime_error& error)
                        {
                            queued_exception = true;
                            ASSERT_EQ(std::string_view{error.what()}, std::string_view{"backend exception"});
                        }
                        ASSERT_EQ(queued_exception, throws);
                        for (auto* flush_task : {&*flushing, &*second_flush})
                        {
                            ASSERT_TRUE(flush_task->handle().done());
                            bool flush_exception = false;
                            try
                            {
                                const auto flush_result = flush_task->handle().promise().result();
                                ASSERT_EQ(flush_result.has_value(), outcome == kafka::error_code::none);
                                if (!flush_result)
                                {
                                    ASSERT_TRUE(flush_result.error().code == outcome);
                                    ASSERT_EQ(flush_result.error().message, std::string{"backend diagnostic"});
                                }
                            }
                            catch (const std::runtime_error& error)
                            {
                                flush_exception = true;
                                ASSERT_EQ(std::string_view{error.what()}, std::string_view{"backend exception"});
                            }
                            ASSERT_EQ(flush_exception, throws);
                        }
                    }
                    if (!throws)
                        ASSERT_EQ(result.has_value(), outcome == kafka::error_code::none);
                    if (!throws && result)
                    {
                        ASSERT_EQ(result->offset, std::int64_t{17});
                        ASSERT_EQ(result->target.topic, std::string{"orders"});
                    }
                    else if (!throws)
                    {
                        ASSERT_TRUE(result.error().code == outcome);
                        ASSERT_EQ(result.error().message, std::string{"backend diagnostic"});
                        ASSERT_FALSE(result.error().retriable);
                    }
                    ASSERT_TRUE((transport->received.value == kafka::bytes{std::byte{0x42}, std::byte{0xff}}));
                    ASSERT_EQ(transport->received.headers.front().key, std::string{"business"});
                    kafka::consumed_record carrier;
                    carrier.headers = transport->received.headers;
                    const auto propagated = cnetmod::observability::messaging::extract(carrier);
                    ASSERT_EQ(propagated.has_value(), enabled);
                    ASSERT_EQ(spans.size(), enabled ? std::size_t{1} : std::size_t{0});
                    if (enabled)
                    {
                        ASSERT_EQ(propagated->trace_id, parent.trace_id);
                        ASSERT_EQ(propagated->span_id, spans.front().context.span_id);
                        ASSERT_EQ(spans.front().parent_span_id, parent.span_id);
                        using status = cnetmod::instrumentation::operation_status;
                        const auto expected = throws ? status::error : outcome == kafka::error_code::none ? status::success
                            : outcome == kafka::error_code::cancelled                                     ? status::cancelled
                            : outcome == kafka::error_code::request_timed_out                             ? status::timeout
                                                                                                          : status::error;
                        ASSERT_TRUE(spans.front().result.status == expected);
                        ASSERT_EQ(spans.front().failed, expected == status::timeout || expected == status::error);
                        for (const auto& attribute : spans.front().attributes)
                            ASSERT_TRUE(attribute.second.find("backend diagnostic") == std::string::npos);
                    }
                    else
                        ASSERT_EQ(transport->received.headers.size(), std::size_t{1});
                    if (throws)
                    {
                        transport->throws = false;
                        transport->outcome = kafka::error_code::none;
                        auto next = producer.send("orders", {});
                        next.handle().resume();
                        const bool completed_before_close = next.handle().done();
                        producer.close();
                        ASSERT_TRUE(next.handle().done());
                        const auto next_result = next.handle().promise().result();
                        ASSERT_TRUE(completed_before_close);
                        ASSERT_EQ(next_result.has_value(), !idempotent);
                        if (idempotent)
                        {
                            ASSERT_TRUE(next_result.error().code == kafka::error_code::configuration);
                            ASSERT_EQ(next_result.error().message, std::string{"producer is closed"});
                        }
                    }
                }
}

TEST(kafka_producer_decorator_preserves_transaction_boundaries)
{
    namespace kafka = cnetmod::kafka;
    for (const bool enabled : {false, true})
        for (const bool commit : {false, true})
            for (const bool fail : {false, true})
            {
                auto backend = std::make_shared<kafka_observation_backend>();
                unsigned exports = 0;
                cnetmod::instrumentation::span_exporter sink;
                if (enabled)
                    sink = cnetmod::instrumentation::span_exporter{[&](const auto&)
                        {
                            ++exports;
                        }};
                cnetmod::observability::instrumented_kafka_producer producer{
                    kafka::producer{backend, {.linger = std::chrono::milliseconds{0}, .transactional_id = "transaction-test"}},
                    std::move(sink)};
                cnetmod::cancel_token token;
                ASSERT_TRUE(cnetmod::sync_wait(producer.begin_transaction(&token)).has_value());
                ASSERT_TRUE(producer.transaction_state() == kafka::producer_transaction_state::in_transaction);
                ASSERT_TRUE(producer.producer_identity().has_value());
                ASSERT_TRUE(cnetmod::sync_wait(producer.send("orders", {}, {}, &token)).has_value());
                const std::map<kafka::topic_partition, kafka::offset_and_metadata> offsets{{{"orders", 0}, {.offset = 18}}};
                ASSERT_TRUE(cnetmod::sync_wait(producer.send_offsets_to_transaction("workers", offsets, &token)).has_value());
                ASSERT_EQ(backend->transaction_group, std::string{"workers"});
                ASSERT_EQ(backend->transaction_offsets.at({"orders", 0}).offset, std::int64_t{18});
                ASSERT_TRUE(backend->transaction_token == &token);
                ASSERT_TRUE(cnetmod::sync_wait(producer.flush()).has_value());
                ASSERT_TRUE(backend->transaction_completions.empty());
                if (fail)
                    backend->transaction_error = kafka::error_code::transport;
                auto result = cnetmod::sync_wait(commit ? producer.commit_transaction(&token) : producer.abort_transaction(&token));
                ASSERT_EQ(result.has_value(), !fail);
                if (!result)
                {
                    ASSERT_TRUE(result.error().code == kafka::error_code::transport);
                    ASSERT_EQ(result.error().message, std::string{"transaction diagnostic"});
                }
                ASSERT_TRUE(producer.transaction_state() == (fail ? kafka::producer_transaction_state::fatal : kafka::producer_transaction_state::ready));
                ASSERT_EQ(backend->transaction_completions.size(), std::size_t{1});
                ASSERT_EQ(backend->transaction_completions.front(), commit);
                ASSERT_TRUE(backend->transaction_token == &token);
                ASSERT_EQ(exports, enabled ? 1U : 0U);
            }
}

TEST(kafka_closed_producer_rejects_transaction_operations)
{
    namespace kafka = cnetmod::kafka;
    {
        auto backend = std::make_shared<kafka_observation_backend>();
        kafka::producer producer{backend, {.transactional_id = "closing-initialization"}};
        backend->during_initialize = [&]
        {
            producer.close();
        };
        const auto result = cnetmod::sync_wait(producer.begin_transaction());
        ASSERT_FALSE(result.has_value());
        if (!result)
            ASSERT_EQ(result.error().message, std::string{"producer is closed"});
        ASSERT_TRUE(producer.transaction_state() != kafka::producer_transaction_state::in_transaction);
    }

    for (const bool active : {false, true})
    {
        auto backend = std::make_shared<kafka_observation_backend>();
        kafka::producer producer{backend, {.transactional_id = "closed-transaction"}};
        if (active)
            ASSERT_TRUE(cnetmod::sync_wait(producer.begin_transaction()).has_value());
        const std::map<kafka::topic_partition, kafka::offset_and_metadata> offsets{{{"orders", 0}, {.offset = 18}}};
        auto begin = producer.begin_transaction();
        auto send_offsets = producer.send_offsets_to_transaction("workers", offsets);
        auto commit = producer.commit_transaction();
        auto abort = producer.abort_transaction();
        producer.close();
        for (auto* operation : {&begin, &send_offsets, &commit, &abort})
        {
            const auto result = cnetmod::sync_wait(std::move(*operation));
            ASSERT_FALSE(result.has_value());
            if (!result)
            {
                ASSERT_TRUE(result.error().code == kafka::error_code::configuration);
                ASSERT_EQ(result.error().message, std::string{"producer is closed"});
            }
        }
        ASSERT_TRUE(backend->transaction_completions.empty());
        ASSERT_TRUE(backend->transaction_group.empty());
    }
}

    #include "kafka_runtime_lifecycle_cases.inc"

TEST(kafka_processing_uses_each_records_parent)
{
    std::vector<cnetmod::instrumentation::completed_span> spans;
    cnetmod::instrumentation::span_exporter sink{[&](const auto& span)
        {
            spans.push_back(span);
        }};
    const auto first_parent = cnetmod::instrumentation::new_root_context();
    const auto second_parent = cnetmod::instrumentation::new_root_context();
    cnetmod::kafka::record first, second;
    cnetmod::observability::messaging::inject(first, first_parent);
    cnetmod::observability::messaging::inject(second, second_parent);
    cnetmod::kafka::consumed_record first_record, second_record;
    first_record.headers = std::move(first.headers);
    second_record.headers = std::move(second.headers);
    auto first_scope = cnetmod::observability::start_kafka_processing(first_record, sink);
    auto second_scope = cnetmod::observability::start_kafka_processing(second_record, sink);
    second_scope.complete();
    first_scope.complete({.status = cnetmod::instrumentation::operation_status::error});
    ASSERT_EQ(spans.size(), std::size_t{2});
    ASSERT_EQ(spans[0].context.trace_id, second_parent.trace_id);
    ASSERT_EQ(spans[0].parent_span_id, second_parent.span_id);
    ASSERT_EQ(spans[1].context.trace_id, first_parent.trace_id);
    ASSERT_EQ(spans[1].parent_span_id, first_parent.span_id);
    ASSERT_TRUE(spans[0].kind == cnetmod::instrumentation::span_kind::consumer);
    ASSERT_FALSE(spans[0].failed);
    ASSERT_TRUE(spans[1].failed);
}

TEST(kafka_processing_rejects_invalid_parent_and_preserves_sampling)
{
    for (const bool malformed : {false, true})
    {
        cnetmod::kafka::consumed_record record;
        if (malformed)
        {
            const std::string invalid = "00-00000000000000000000000000000000-0000000000000000-01";
            cnetmod::kafka::bytes bytes;
            for (const auto value : invalid)
                bytes.push_back(static_cast<std::byte>(value));
            record.headers.push_back({"traceparent", std::move(bytes)});
        }
        const auto count = record.headers.size();
        std::vector<cnetmod::instrumentation::completed_span> spans;
        cnetmod::instrumentation::span_exporter sink{[&](const auto& span)
            {
                spans.push_back(span);
            }};
        auto scope = cnetmod::observability::start_kafka_processing(record, sink);
        ASSERT_TRUE(scope.context() != nullptr);
        ASSERT_TRUE(cnetmod::instrumentation::valid_trace_context(*scope.context()));
        scope.complete();
        ASSERT_EQ(spans.size(), std::size_t{1});
        ASSERT_TRUE(spans.front().parent_span_id.empty());
        ASSERT_EQ(record.headers.size(), count);
    }
    auto parent = cnetmod::instrumentation::new_root_context();
    parent.flags = 0;
    cnetmod::kafka::record outgoing;
    cnetmod::observability::messaging::inject(outgoing, parent);
    cnetmod::kafka::consumed_record incoming;
    incoming.headers = std::move(outgoing.headers);
    unsigned exports = 0, sampling_calls = 0;
    cnetmod::instrumentation::span_exporter sink{
        [&](const auto&)
        {
            ++exports;
        },
        [&](const auto&)
        {
            ++sampling_calls;
            return true;
        }};
    auto scope = cnetmod::observability::start_kafka_processing(incoming, sink);
    ASSERT_TRUE(scope.context() != nullptr);
    ASSERT_EQ(scope.context()->trace_id, parent.trace_id);
    ASSERT_EQ(scope.context()->flags, std::uint8_t{0});
    scope.complete();
    ASSERT_EQ(exports, 0U);
    ASSERT_EQ(sampling_calls, 0U);
}

TEST(kafka_observed_send_preserves_failure_and_parent)
{
    cnetmod::kafka::producer producer{nullptr};
    producer.close();
    const auto parent = cnetmod::instrumentation::new_root_context();
    std::vector<cnetmod::instrumentation::completed_span> spans;
    cnetmod::instrumentation::span_exporter sink{[&](const auto& span)
        {
            spans.push_back(span);
        }};
    cnetmod::observability::instrumented_kafka_producer observed{std::move(producer), std::move(sink)};
    auto operation = observed.send("private-topic", {}, parent);
    const auto result = cnetmod::sync_wait(std::move(operation));
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error().code == cnetmod::kafka::error_code::configuration);
    ASSERT_EQ(spans.size(), std::size_t{1});
    ASSERT_EQ(spans.front().context.trace_id, parent.trace_id);
    ASSERT_EQ(spans.front().parent_span_id, parent.span_id);
    ASSERT_TRUE(spans.front().kind == cnetmod::instrumentation::span_kind::producer);
    ASSERT_TRUE(spans.front().failed);
    ASSERT_TRUE(observed.transaction_state() == cnetmod::kafka::producer_transaction_state::disabled);
    ASSERT_FALSE(observed.producer_identity().has_value());
    observed.close();
}

TEST(kafka_observed_send_falls_back_when_exporter_copy_throws)
{
    struct exporter
    {
        exporter() = default;
        exporter(exporter&&) noexcept = default;

        exporter(const exporter&)
        {
            throw std::runtime_error("exporter copy failure");
        }

        void operator()(const cnetmod::instrumentation::completed_span&) const
        {
            ASSERT_TRUE(false);
        }
    };

    cnetmod::kafka::producer producer{nullptr};
    producer.close();
    const auto parent = cnetmod::instrumentation::new_root_context();
    cnetmod::instrumentation::span_exporter sink{exporter{}};
    for (const bool with_token : {false, true})
    {
        cnetmod::cancel_token token;
        const auto result = cnetmod::sync_wait(cnetmod::observability::send_kafka_record(
            producer, "orders", {}, parent, sink, with_token ? &token : nullptr));
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error().code == cnetmod::kafka::error_code::configuration);
        ASSERT_EQ(result.error().message, std::string{"producer is closed"});
    }
}

TEST(kafka_observed_send_contains_exporter_failure)
{
    cnetmod::kafka::producer producer{nullptr};
    producer.close();
    const auto parent = cnetmod::instrumentation::new_root_context();
    unsigned calls = 0;
    cnetmod::instrumentation::span_exporter sink{[&](const auto&)
        {
            ++calls;
            throw std::runtime_error("exporter failure");
        }};
    for (const bool with_token : {false, true})
    {
        cnetmod::cancel_token cancellation;
        const auto result = cnetmod::sync_wait(cnetmod::observability::send_kafka_record(
            producer, "private-topic", {}, parent, sink, with_token ? &cancellation : nullptr));
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error().code == cnetmod::kafka::error_code::configuration);
        ASSERT_EQ(result.error().message, std::string{"producer is closed"});
    }
    ASSERT_EQ(calls, 2U);
}

TEST(kafka_connection_observer_registration_during_callback_is_deferred)
{
    struct observer final : cnetmod::kafka::connection_observer
    {
        unsigned connected = 0;
        std::function<void()> callback;

        void on_connected(const cnetmod::kafka::broker_endpoint&) override
        {
            ++connected;
            if (callback)
                callback();
        }
    };

    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::kafka::broker_connection connection{*io, {-1, "127.0.0.1", endpoint->port(), {}}, {}};
    auto registering = std::make_shared<observer>();
    auto existing = std::make_shared<observer>();
    auto added = std::make_shared<observer>();
    registering->callback = [&]
    {
        if (registering->connected == 1)
            for (unsigned index = 0; index < 64; ++index)
                connection.add_observer(added);
    };
    connection.add_observer(registering);
    connection.add_observer(existing);
    bool connected = true;
    unsigned first_added = 0;
    auto run = [&]() -> cnetmod::task<void>
    {
        connected = (co_await connection.connect()).has_value();
        first_added = added->connected;
        connection.close();
        connected = (co_await connection.connect()).has_value() && connected;
        connection.close();
        io->stop();
    };
    auto task = run();
    task.handle().resume();
    io->run();
    ASSERT_TRUE(task.handle().done());
    task.handle().promise().result();
    ASSERT_TRUE(connected);
    ASSERT_EQ(first_added, 0U);
    ASSERT_EQ(registering->connected, 2U);
    ASSERT_EQ(existing->connected, 2U);
    ASSERT_EQ(added->connected, 64U);
}

TEST(kafka_nested_observer_notification_preserves_outer_indices)
{
    struct observer final : cnetmod::kafka::connection_observer
    {
        unsigned connected = 0;
        std::function<void()> callback;

        void on_connected(const cnetmod::kafka::broker_endpoint&) override
        {
            ++connected;
            if (callback)
                callback();
        }
    };

    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(listener.has_value());
    ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(listener->listen().has_value());
    const auto endpoint = listener->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    cnetmod::kafka::broker_connection connection{*io, {-1, "127.0.0.1", endpoint->port(), {}}, {}};
    auto reentrant = std::make_shared<observer>();
    auto existing = std::make_shared<observer>();
    connection.add_observer(reentrant);
    connection.add_observer(std::weak_ptr<cnetmod::kafka::connection_observer>{});
    connection.add_observer(existing);
    reentrant->callback = [&]
    {
        if (reentrant->connected != 1)
            return;
        auto nested = connection.connect();
        nested.handle().resume();
        // Deliberately dispatch nested I/O to exercise synchronous observer reentry.
        while (!nested.handle().done())
            io->run_one();
        ASSERT_TRUE(nested.handle().promise().result().has_value());
    };
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto connected = co_await connection.connect();
        ASSERT_TRUE(connected.has_value());
        connection.close();
        io->stop();
    };
    auto task = run();
    task.handle().resume();
    io->run();
    ASSERT_TRUE(task.handle().done());
    task.handle().promise().result();
    ASSERT_EQ(reentrant->connected, 2U);
    ASSERT_EQ(existing->connected, 2U);
}

TEST(kafka_metadata_observer_failure_does_not_interrupt_update)
{
    struct observer final : cnetmod::kafka::metadata_observer
    {
        unsigned calls = 0;
        bool throws = false;

        void on_metadata_changed(const cnetmod::kafka::protocol::metadata_response& metadata) override
        {
            ++calls;
            ASSERT_EQ(metadata.controller_id, 42);
            if (throws)
                throw std::runtime_error("metadata observer failure");
        }
    };

    cnetmod::kafka::metadata_cache cache;
    auto failing = std::make_shared<observer>();
    failing->throws = true;
    auto recording = std::make_shared<observer>();
    cache.add_observer(failing);
    cache.add_observer(recording);
    bool escaped = false;
    try
    {
        cnetmod::kafka::protocol::metadata_response metadata;
        metadata.controller_id = 42;
        cache.update(std::move(metadata));
    }
    catch (...)
    {
        escaped = true;
    }
    ASSERT_FALSE(escaped);
    ASSERT_EQ(cache.snapshot().controller_id, 42);
    ASSERT_EQ(failing->calls, 1U);
    ASSERT_EQ(recording->calls, 1U);
}

TEST(kafka_nested_metadata_update_preserves_notification_snapshots)
{
    struct observer final : cnetmod::kafka::metadata_observer
    {
        std::vector<std::int32_t> seen;
        std::function<void(const cnetmod::kafka::protocol::metadata_response&)> callback;

        void on_metadata_changed(const cnetmod::kafka::protocol::metadata_response& metadata) override
        {
            seen.push_back(metadata.controller_id);
            if (callback)
                callback(metadata);
        }
    };

    cnetmod::kafka::metadata_cache cache;
    auto reentrant = std::make_shared<observer>();
    auto existing = std::make_shared<observer>();
    auto added = std::make_shared<observer>();
    reentrant->callback = [&](const auto& snapshot)
    {
        if (snapshot.controller_id != 1)
            return;
        cache.add_observer(added);
        cnetmod::kafka::protocol::metadata_response nested;
        nested.controller_id = 2;
        nested.cluster_id = "nested";
        cache.update(std::move(nested));
        ASSERT_EQ(snapshot.controller_id, 1);
        ASSERT_EQ(*snapshot.cluster_id, std::string{"outer"});
        ASSERT_EQ(cache.snapshot().controller_id, 2);
    };
    cache.add_observer(reentrant);
    cache.add_observer(existing);
    cnetmod::kafka::protocol::metadata_response outer;
    outer.controller_id = 1;
    outer.cluster_id = "outer";
    cache.update(std::move(outer));
    ASSERT_TRUE((reentrant->seen == std::vector<std::int32_t>{1, 2}));
    ASSERT_TRUE((existing->seen == std::vector<std::int32_t>{2, 1}));
    ASSERT_TRUE((added->seen == std::vector<std::int32_t>{2}));
    ASSERT_EQ(cache.snapshot().controller_id, 2);
}

TEST(application_kafka_start_cleans_up_failed_exchanges)
{
    struct observer final : cnetmod::kafka::connection_observer
    {
        bool throws = false;
        unsigned connected = 0;
        unsigned disconnected = 0;

        void on_connected(const cnetmod::kafka::broker_endpoint&) override
        {
            ++connected;
            if (throws)
                throw std::runtime_error("observer failure");
        }

        void on_disconnected(const cnetmod::kafka::broker_endpoint&, const cnetmod::kafka::error&) override
        {
            ++disconnected;
            if (throws)
                throw std::runtime_error("observer failure");
        }
    };

    cnetmod::net_init network;
    enum class peer_behavior
    {
        stalled_prefix,
        cancelled_prefix,
        invalid_length,
        invalid_correlation,
        truncated_body,
        stalled_body
    };
    for (const auto mode : {peer_behavior::stalled_prefix, peer_behavior::cancelled_prefix,
             peer_behavior::invalid_length, peer_behavior::invalid_correlation,
             peer_behavior::truncated_body, peer_behavior::stalled_body})
    {
        auto io = cnetmod::make_io_context();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind(cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        const auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        cnetmod::kafka::client_options options;
        options.bootstrap_servers.push_back({.host = "127.0.0.1", .port = endpoint->port()});
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.export_traces = false, .export_metrics = false, .export_logs = false}};
        application::task_supervisor supervisor{*io};
        application::kafka_service service{*io, std::move(options), "stalled", application::service_requirement::required, {}};
        auto failing_observer = std::make_shared<observer>();
        failing_observer->throws = true;
        auto recording_observer = std::make_shared<observer>();
        service.client().add_connection_observer(failing_observer);
        service.client().add_connection_observer(recording_observer);
        cnetmod::cancel_token cancellation;
        application::service_context context{*io, telemetry, supervisor, cancellation,
            cnetmod::deadline::after(std::chrono::milliseconds{500})};
        bool requested = false, disconnected = false, stopped = false;
        std::error_code failure;
        unsigned finished = 0;
        const auto finish = [&]
        {
            if (++finished == 2)
                io->stop();
        };
        auto peer = [&]() -> cnetmod::task<void>
        {
            auto connection = co_await cnetmod::async_accept(*io, *listener);
            if (connection)
            {
                std::array<std::byte, 4096> buffer{};
                for (;;)
                {
                    auto received = co_await cnetmod::async_read(*io, *connection,
                        cnetmod::mutable_buffer{buffer.data(), buffer.size()});
                    if (!received || *received == 0)
                    {
                        disconnected = true;
                        break;
                    }
                    const bool first_request = !requested;
                    requested = true;
                    if (mode == peer_behavior::cancelled_prefix)
                        cancellation.cancel();
                    if (first_request && mode != peer_behavior::stalled_prefix && mode != peer_behavior::cancelled_prefix)
                    {
                        std::array<std::byte, 8> response{};
                        if (mode == peer_behavior::invalid_correlation)
                            response[3] = std::byte{4};
                        if (mode == peer_behavior::truncated_body || mode == peer_behavior::stalled_body)
                            response[3] = std::byte{8};
                        const auto written = co_await cnetmod::async_write_all(*io, *connection,
                            cnetmod::const_buffer{response.data(), mode == peer_behavior::invalid_length ? 4U : 8U});
                        ASSERT_TRUE(written.has_value());
                        if (mode == peer_behavior::truncated_body)
                        {
                            connection->close();
                            disconnected = true;
                            break;
                        }
                    }
                }
            }
            finish();
        };
        auto run = [&]() -> cnetmod::task<void>
        {
            const auto result = co_await service.start(context);
            if (!result)
                failure = result.error();
            stopped = (co_await service.stop(context)).has_value();
            finish();
        };
        auto broker_task = peer();
        auto service_task = run();
        broker_task.handle().resume();
        service_task.handle().resume();
        io->run();
        ASSERT_TRUE(broker_task.handle().done());
        ASSERT_TRUE(service_task.handle().done());
        broker_task.handle().promise().result();
        service_task.handle().promise().result();
        ASSERT_TRUE(requested);
        ASSERT_TRUE(disconnected);
        ASSERT_TRUE(stopped);
        ASSERT_EQ(failing_observer->connected, 1U);
        ASSERT_EQ(failing_observer->disconnected, 1U);
        ASSERT_EQ(recording_observer->connected, 1U);
        ASSERT_EQ(recording_observer->disconnected, 1U);
        if (mode == peer_behavior::stalled_prefix || mode == peer_behavior::cancelled_prefix || mode == peer_behavior::stalled_body)
            ASSERT_EQ(failure, std::make_error_code(mode == peer_behavior::cancelled_prefix ? std::errc::operation_canceled : std::errc::timed_out));
        else
        {
            ASSERT_EQ(std::string_view{failure.category().name()}, std::string_view{"cnetmod.kafka"});
            const bool truncated = mode == peer_behavior::truncated_body;
            ASSERT_EQ(failure.value(), static_cast<int>(truncated ? cnetmod::kafka::error_code::transport : cnetmod::kafka::error_code::malformed_response));
            ASSERT_TRUE(failure == (truncated ? std::errc::io_error : std::errc::protocol_error));
        }
    }
}

TEST(application_kafka_start_respects_cancelled_and_expired_context)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::task_supervisor supervisor{*io};
    for (const bool expired : {false, true})
    {
        application::kafka_service service{*io, {}, "test", application::service_requirement::required, {}};
        cnetmod::cancel_token cancellation;
        if (!expired)
            cancellation.cancel();
        application::service_context context{*io, telemetry, supervisor, cancellation,
            expired ? cnetmod::deadline::after(std::chrono::milliseconds{0}) : cnetmod::deadline{}};
        const auto result = cnetmod::sync_wait(service.start(context));
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), std::make_error_code(expired ? std::errc::timed_out : std::errc::operation_canceled));
        const auto health = cnetmod::sync_wait(service.probe(context));
        ASSERT_TRUE(health.status == application::service_health::down);
        ASSERT_TRUE(cnetmod::sync_wait(service.stop(context)).has_value());
    }
}

TEST(application_kafka_start_preserves_configuration_error_identity)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::task_supervisor supervisor{*io};
    application::kafka_service service{*io, {}, "invalid", application::service_requirement::required, {}};
    cnetmod::cancel_token cancellation;
    application::service_context context{*io, telemetry, supervisor, cancellation, {}};
    const auto result = cnetmod::sync_wait(service.start(context));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().value(), static_cast<int>(cnetmod::kafka::error_code::configuration));
    ASSERT_EQ(std::string_view{result.error().category().name()}, "cnetmod.kafka");
    ASSERT_TRUE(result.error() == std::errc::invalid_argument);
    ASSERT_EQ(result.error().message(), "Kafka error 1003");
    ASSERT_TRUE(cnetmod::sync_wait(service.stop(context)).has_value());
}
#endif

#if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL) || defined(CNETMOD_HAS_PROTOCOL_MONGODB) || defined(CNETMOD_HAS_PROTOCOL_MYSQL) || defined(CNETMOD_HAS_PROTOCOL_REDIS)
TEST(application_database_start_rejects_cancelled_and_expired_context_before_warmup)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    application::task_supervisor supervisor{*io};
    for (const bool cancelled : {false, true})
    {
        cnetmod::cancel_token cancellation;
        if (cancelled)
            cancellation.cancel();
        application::service_context context{*io, telemetry, supervisor, cancellation,
            cnetmod::deadline::after(std::chrono::milliseconds{0})};
        auto check = [&](auto& service)
        {
            // No event loop is run: rejected admission must not initiate I/O.
            const auto result = cnetmod::sync_wait(service.start(context));
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error(), std::make_error_code(cancelled ? std::errc::operation_canceled : std::errc::timed_out));
            ASSERT_EQ(service.pool().size(), std::size_t{0});
            const auto health = cnetmod::sync_wait(service.probe(context));
            ASSERT_TRUE(health.status == application::service_health::down);
            ASSERT_TRUE(cnetmod::sync_wait(service.stop(context)).has_value());
        };
    #ifdef CNETMOD_HAS_PROTOCOL_MYSQL
        cnetmod::mysql::pool_params mysql_options;
        mysql_options.initial_size = 0;
        application::mysql_service mysql{*io, mysql_options, "test",
            application::service_requirement::required, {}};
        check(mysql);
        ASSERT_FALSE(supervisor.state("mysql-pool:test").has_value());
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_REDIS
        cnetmod::redis::pool_params redis_options;
        redis_options.initial_size = 0;
        application::redis_service redis{*io, redis_options, "test",
            application::service_requirement::required, {}};
        check(redis);
        ASSERT_FALSE(supervisor.state("redis-pool:test").has_value());
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
        cnetmod::postgresql::connection_pool_options pg_options;
        pg_options.minimum_connections = 0;
        application::postgresql_service pg{*io, pg_options, "test",
            application::service_requirement::required, {}};
        check(pg);
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_MONGODB
        cnetmod::mongodb::connection_pool_options mongo_options;
        mongo_options.minimum_size = 0;
        application::mongodb_service mongo{*io, mongo_options, "test",
            application::service_requirement::required, {}};
        check(mongo);
    #endif
    }
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
namespace {

class recording_chat_model final : public cnetmod::ai::chat_model
{
public:
    auto invoke(cnetmod::ai::chat_request request,
        const cnetmod::ai::run_config& configuration)
        -> cnetmod::task<std::expected<cnetmod::ai::chat_response,
            std::string>> override
    {
        last_request = std::move(request);
        listener_count = configuration.listeners.size();
        ++invocations;
        co_return cnetmod::ai::chat_response{
            .id = "response",
            .model = last_request.model,
            .choices = {{.index = 0,
                .msg = cnetmod::ai::message::model_output("ok"),
                .finish_reason = "stop"}}};
    }

    cnetmod::ai::chat_request last_request;
    std::size_t listener_count = 0;
    std::size_t invocations = 0;
};

class passive_run_listener final : public cnetmod::ai::run_listener
{
public:
    void on_event(const cnetmod::ai::run_event&) override {}
};

} // namespace

TEST(application_chat_model_template_applies_defaults_observation_and_cancellation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto model = std::make_shared<recording_chat_model>();
    passive_run_listener listener;
    cnetmod::application::chat_model_pool pool{*io, {model}};
    cnetmod::application::chat_model_template model_api{pool,
        {.request = {.model = "gpt-test",
             .messages = {cnetmod::ai::message::developer("policy")}},
            .system_prompt = "system"}};

    auto response = cnetmod::sync_wait(
        model_api.invoke("hello", {.listeners = {&listener}}));
    ASSERT_TRUE(response.has_value());
    ASSERT_EQ(model->invocations, 1U);
    ASSERT_EQ(model->listener_count, 1U);
    ASSERT_EQ(model->last_request.model, "gpt-test");
    ASSERT_EQ(model->last_request.messages.size(), 3U);
    ASSERT_EQ(model->last_request.messages[0].role, "system");
    ASSERT_EQ(model->last_request.messages[1].role, "developer");
    ASSERT_EQ(model->last_request.messages[2].role, "user");

    cnetmod::cancel_token cancellation;
    cancellation.cancel();
    auto cancelled = cnetmod::sync_wait(model_api.invoke("ignored",
        {.listeners = {&listener}, .cancellation = &cancellation}));
    ASSERT_FALSE(cancelled.has_value());
    ASSERT_EQ(model->invocations, 1U);
}

TEST(application_chat_model_pool_uses_exclusive_reusable_leases)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto first_model = std::make_shared<recording_chat_model>();
    auto second_model = std::make_shared<recording_chat_model>();
    cnetmod::application::chat_model_pool pool{
        *io, {first_model, second_model}};

    {
        auto first = cnetmod::sync_wait(pool.acquire());
        auto second = cnetmod::sync_wait(pool.acquire());
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(&first->get() != &second->get());
    }

    auto reused_first = cnetmod::sync_wait(pool.acquire());
    auto reused_second = cnetmod::sync_wait(pool.acquire());
    ASSERT_TRUE(reused_first.has_value());
    ASSERT_TRUE(reused_second.has_value());
}

TEST(application_chat_model_pool_keeps_outstanding_generation_alive)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto previous = std::make_shared<recording_chat_model>();
    auto replacement = std::make_shared<recording_chat_model>();
    cnetmod::application::chat_model_pool pool{*io, {previous}};

    auto outstanding = cnetmod::sync_wait(pool.acquire());
    ASSERT_TRUE(outstanding.has_value());
    auto reset = cnetmod::sync_wait(pool.reset({replacement}));
    ASSERT_TRUE(reset.has_value());

    auto old_response = cnetmod::sync_wait(outstanding->get().invoke(
        {.model = "old"}, {}));
    ASSERT_TRUE(old_response.has_value());
    ASSERT_EQ(previous->invocations, 1U);

    auto current = cnetmod::sync_wait(pool.acquire());
    ASSERT_TRUE(current.has_value());
    auto new_response = cnetmod::sync_wait(current->get().invoke(
        {.model = "new"}, {}));
    ASSERT_TRUE(new_response.has_value());
    ASSERT_EQ(replacement->invocations, 1U);
}

TEST(application_chat_model_pool_cancels_saturated_admission)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto model = std::make_shared<recording_chat_model>();
    cnetmod::application::chat_model_pool pool{*io, {model}};
    auto occupied = cnetmod::sync_wait(pool.acquire());
    ASSERT_TRUE(occupied.has_value());

    cnetmod::cancel_token cancellation;
    bool cancelled = false;
    auto scenario = [&]() -> cnetmod::task<void>
    {
        auto acquire = [&]() -> cnetmod::task<void>
        {
            auto result = co_await pool.acquire(&cancellation);
            cancelled = !result &&
                result.error() ==
                    std::make_error_code(std::errc::operation_canceled);
        };
        auto stop_wait = [&]() -> cnetmod::task<void>
        {
            (void)co_await cnetmod::async_timer_wait(
                *io, std::chrono::milliseconds{5});
            cancellation.cancel();
        };
        co_await cnetmod::when_all(acquire(), stop_wait());
        io->stop();
    };
    cnetmod::spawn(*io, scenario());
    io->run();
    ASSERT_TRUE(cancelled);
}

TEST(application_chat_conversation_persists_complete_turns_and_history)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto model = std::make_shared<recording_chat_model>();
    cnetmod::application::chat_model_pool pool{*io, {model}};
    cnetmod::application::chat_model_template model_api{pool};
    cnetmod::ai::in_memory_conversation_store store;
    auto conversation = model_api.conversation("session-a", store);

    auto first = cnetmod::sync_wait(conversation.invoke("first"));
    auto second = cnetmod::sync_wait(conversation.invoke("second"));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(model->last_request.messages.size(), 3U);
    ASSERT_EQ(model->last_request.messages[0].content, "first");
    ASSERT_EQ(model->last_request.messages[1].content, "ok");
    ASSERT_EQ(model->last_request.messages[2].content, "second");

    auto stored = cnetmod::sync_wait(store.load_recent("session-a", 0));
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), 4U);

    auto other = model_api.conversation("session-b", store);
    auto isolated = cnetmod::sync_wait(other.invoke("isolated"));
    ASSERT_TRUE(isolated.has_value());
    ASSERT_EQ(model->last_request.messages.size(), 1U);
    ASSERT_EQ(model->last_request.messages[0].content, "isolated");
}

TEST(application_runtime_resolves_named_chat_model_template)
{
    auto host = application::application_builder{"openai-template"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& config)
                        {
                            config.logging.manage_lifecycle = false;
                            config.management.enabled = false;
                            application::configured_service service{
                                .name = "openai",
                                .instance = "assistant",
                                .enabled = true};
                            service.properties["api_key"] = "test";
                            config.services.emplace("assistant", std::move(service));
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;

    auto model_api = host->runtime().chat_model("assistant");
    ASSERT_TRUE(model_api.has_value());
    auto missing = host->runtime().chat_model("missing");
    ASSERT_FALSE(missing.has_value());
    if (!missing)
        ASSERT_EQ(missing.error(),
            std::make_error_code(std::errc::no_such_file_or_directory));
}

TEST(application_runtime_rejects_invalid_chat_model_reconfiguration)
{
    auto host = application::application_builder{"openai-reconfigure"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& config)
                        {
                            config.logging.manage_lifecycle = false;
                            config.management.enabled = false;
                            application::configured_service service{
                                .name = "openai",
                                .instance = "assistant",
                                .enabled = true};
                            service.properties["api_key"] = "test";
                            config.services.emplace("assistant", std::move(service));
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;

    application::chat_model_reconfiguration invalid;
    invalid.properties["api_key"] = "test";
    invalid.properties["pool_size"] = 0;
    auto rejected = cnetmod::sync_wait(
        host->runtime().reconfigure_chat_model("assistant", std::move(invalid)));
    ASSERT_FALSE(rejected.has_value());
    if (!rejected)
        ASSERT_EQ(rejected.error(),
            std::make_error_code(std::errc::invalid_argument));

    application::chat_model_reconfiguration missing;
    missing.properties["api_key"] = "test";
    auto absent = cnetmod::sync_wait(
        host->runtime().reconfigure_chat_model("missing", std::move(missing)));
    ASSERT_FALSE(absent.has_value());
    if (!absent)
        ASSERT_EQ(absent.error(),
            std::make_error_code(std::errc::no_such_file_or_directory));
}

TEST(application_openai_reconfiguration_commits_only_connected_generation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    auto first_listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    auto second_listener = cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(first_listener.has_value());
    ASSERT_TRUE(second_listener.has_value());
    if (!first_listener || !second_listener)
        return;
    ASSERT_TRUE(first_listener->bind(cnetmod::endpoint{
                                         cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(second_listener->bind(cnetmod::endpoint{
                                          cnetmod::ipv4_address::loopback(), 0})
            .has_value());
    ASSERT_TRUE(first_listener->listen().has_value());
    ASSERT_TRUE(second_listener->listen().has_value());
    auto first_endpoint = first_listener->local_endpoint();
    auto second_endpoint = second_listener->local_endpoint();
    ASSERT_TRUE(first_endpoint.has_value());
    ASSERT_TRUE(second_endpoint.has_value());
    if (!first_endpoint || !second_endpoint)
        return;

    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false,
            .export_metrics = false,
            .export_logs = false}};
    application::task_supervisor supervisor{*io};
    cnetmod::cancel_token cancellation;
    application::service_context context{*io, telemetry, supervisor,
        cancellation, cnetmod::deadline::after(std::chrono::seconds{2})};
    cnetmod::openai::connect_options initial;
    initial.api_base = std::format("http://127.0.0.1:{}/v1",
        first_endpoint->port());
    initial.api_key = "initial";
    initial.timeout_seconds = 1;
    application::openai_service service{*io, telemetry, std::move(initial),
        "reload", application::service_requirement::required, {}, 1};

    std::optional<cnetmod::socket> first_peer;
    std::optional<cnetmod::socket> second_peer;
    bool started = false;
    bool reconfigured = false;
    bool rejected = false;
    bool stayed_up = false;
    bool snapshot_connected = false;
    auto accept_first = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *first_listener);
        if (accepted)
            first_peer.emplace(std::move(*accepted));
    };
    auto accept_second = [&]() -> cnetmod::task<void>
    {
        auto accepted = co_await cnetmod::async_accept(*io, *second_listener);
        if (accepted)
            second_peer.emplace(std::move(*accepted));
    };
    auto scenario = [&]() -> cnetmod::task<void>
    {
        auto start = co_await service.start(context);
        started = start.has_value();

        application::chat_model_reconfiguration replacement;
        replacement.properties["base_url"] = std::format(
            "http://127.0.0.1:{}/v1", second_endpoint->port());
        replacement.properties["api_key"] = "replacement";
        replacement.properties["timeout_seconds"] = 1;
        replacement.properties["pool_size"] = 1;
        auto reload = co_await service.reconfigure(std::move(replacement));
        reconfigured = reload.has_value();
        auto snapshot = co_await service.current_client();
        snapshot_connected = snapshot && snapshot->is_connected();

        application::chat_model_reconfiguration unavailable;
        unavailable.properties["base_url"] =
            "http://127.0.0.1:1/v1";
        unavailable.properties["api_key"] = "unavailable";
        unavailable.properties["timeout_seconds"] = 1;
        unavailable.properties["pool_size"] = 1;
        auto failed = co_await service.reconfigure(std::move(unavailable));
        rejected = !failed;
        auto health = co_await service.probe(context);
        stayed_up = health.status == application::service_health::up;
        (void)co_await service.stop(context);
        io->stop();
    };
    cnetmod::spawn(*io, accept_first());
    cnetmod::spawn(*io, accept_second());
    cnetmod::spawn(*io, scenario());
    io->run();

    ASSERT_TRUE(started);
    ASSERT_TRUE(reconfigured);
    ASSERT_TRUE(snapshot_connected);
    ASSERT_TRUE(rejected);
    ASSERT_TRUE(stayed_up);
}

TEST(application_openai_rejects_invalid_pool_size)
{
    auto host = application::application_builder{"invalid-openai-pool"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& config)
                        {
                            config.logging.manage_lifecycle = false;
                            config.management.enabled = false;
                            application::configured_service service{
                                .name = "openai",
                                .enabled = true,
                            };
                            service.properties["api_key"] = "test";
                            service.properties["pool_size"] = 0;
                            config.services.emplace("model", std::move(service));
                        })
                    .build();
    ASSERT_FALSE(host.has_value());
    if (!host)
        ASSERT_EQ(host.error(),
            std::make_error_code(std::errc::invalid_argument));
}

TEST(application_openai_listener_is_optional_and_configuration_is_idempotent)
{
    for (const bool enabled : {false, true})
    {
        auto host = application::application_builder{"openai-observation"}
                        .enable_auto_configuration()
                        .configure([&](application::application_configuration& config)
                            {
                                config.logging.manage_lifecycle = false;
                                config.observability.tracing = enabled;
                                config.observability.metrics = false;
                                config.observability.logs = false;
                                config.observability.otlp.endpoint = "http://127.0.0.1:1/v1/traces";
                                config.services.emplace("model", application::configured_service{.name = "openai", .enabled = true});
                                config.services.at("model").properties["api_key"] = "test";
                            })
                        .build();
        ASSERT_TRUE(host.has_value());
        if (!host)
            return;
        auto* service = host->services().find<application::openai_service>();
        ASSERT_TRUE(service != nullptr);
        ASSERT_EQ(service->telemetry_listener() != nullptr, enabled);
        cnetmod::cancel_token cancellation;
        cnetmod::openai::run_config input;
        input.cancellation = &cancellation;
        input.metadata.emplace("tenant", "test");
        auto configured = service->run_configuration(std::move(input));
        configured = service->run_configuration(std::move(configured));
        ASSERT_EQ(configured.listeners.size(), enabled ? std::size_t{1} : std::size_t{0});
        ASSERT_TRUE(configured.cancellation == &cancellation);
        ASSERT_EQ(configured.metadata.at("tenant"), "test");
    }
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
TEST(application_auto_configuration_registers_redis_cluster_mode)
{
    auto host = application::application_builder{"redis-cluster-configuration"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& value)
                        {
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                            application::configured_service redis{
                                .name = "redis",
                                .instance = "sessions",
                                .enabled = true,
                                .requirement = application::service_requirement::optional,
                            };
                            redis.properties = {
                                {"mode", "cluster"},
                                {"database", 0},
                                {"seeds", {{{"host", "127.0.0.1"}, {"port", 7000}}, {{"host", "127.0.0.1"}, {"port", 7001}}}},
                            };
                            value.services.emplace("redis-cluster", std::move(redis));
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    ASSERT_TRUE(host->services().find<application::redis_cluster_service>(
                    "sessions") != nullptr);
    ASSERT_TRUE(host->services().find<application::redis_service>(
                    "sessions") == nullptr);
}

TEST(application_redis_cluster_rejects_nonzero_database)
{
    auto host = application::application_builder{"redis-cluster-invalid-db"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& value)
                        {
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                            application::configured_service redis{
                                .name = "redis",
                                .enabled = true,
                            };
                            redis.properties = {
                                {"mode", "cluster"},
                                {"database", 1},
                                {"seeds", {{{"host", "127.0.0.1"}, {"port", 7000}}}},
                            };
                            value.services.emplace("redis", std::move(redis));
                        })
                    .build();
    ASSERT_FALSE(host.has_value());
}
#endif

TEST(application_auto_configuration_registers_compiled_integrations)
{
    auto builder = application::application_builder{"auto-configuration-test"}
                       .enable_auto_configuration()
                       .configure([](application::application_configuration& value)
                           {
                               value.logging.manage_lifecycle = false;
                               value.management.enabled = false;
                               auto enable = [&value](std::string name)
                               {
                                   auto key = name;
                                   value.services.emplace(std::move(key),
                                       application::configured_service{
                                           .name = std::move(name),
                                           .enabled = true,
                                           .requirement = application::service_requirement::optional,
                                       });
                               };
                               enable("http_client");
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
                               enable("redis");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
                               enable("mysql");
                               value.services.at("mysql").properties["username"] = "test";
                               value.services.at("mysql").properties["database"] = "test";
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
                               enable("postgresql");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
                               enable("mongodb");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
                               enable("kafka");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
                               enable("mqtt");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
                               enable("amqp091");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
                               enable("amqp10");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
                               enable("openai");
                               value.services.at("openai").properties["api_key"] = "test";
#endif
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
                               enable("grpc");
                               value.services.at("grpc").properties["base_url"] =
                                   "http://127.0.0.1:50051";
                               enable("grpc_server");
#endif
                           });
    auto host = builder.build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    ASSERT_TRUE(host->services().frozen());
    ASSERT_TRUE(host->services().find<application::http_client_service>() != nullptr);
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
    ASSERT_TRUE(host->services().find<application::redis_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
    ASSERT_TRUE(host->services().find<application::mysql_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
    ASSERT_TRUE(host->services().find<application::postgresql_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
    ASSERT_TRUE(host->services().find<application::mongodb_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    ASSERT_TRUE(host->services().find<application::kafka_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
    ASSERT_TRUE(host->services().find<application::mqtt_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    ASSERT_TRUE(host->services().find<application::amqp091_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    ASSERT_TRUE(host->services().find<application::amqp10_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
    ASSERT_TRUE(host->services().find<application::openai_service>() != nullptr);
#endif
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
    auto grpc_client = host->services().find<application::grpc_client_service>();
    ASSERT_TRUE(grpc_client != nullptr);
    auto& observed_grpc = grpc_client->client();
    [[maybe_unused]] auto facade = observed_grpc.unary({
        .service = "application.observation.Probe",
        .method = "NoNetworkStart",
    });
    ASSERT_TRUE(host->services().find<application::grpc_server_service>() != nullptr);
#endif
}

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
TEST(application_mysql_configuration_accepts_transport_and_pool_timeouts)
{
    auto make_host = [](std::string ssl, std::int64_t connect_timeout)
    {
        return application::application_builder{"mysql-configuration"}
            .enable_auto_configuration()
            .configure([ssl = std::move(ssl), connect_timeout](
                           application::application_configuration& value)
                {
                    value.logging.manage_lifecycle = false;
                    value.management.enabled = false;
                    application::configured_service mysql{
                        .name = "mysql",
                        .enabled = true,
                        .requirement = application::service_requirement::optional,
                    };
                    mysql.properties = {
                        {"username", "test"},
                        {"database", "test"},
                        {"ssl", ssl},
                        {"tls_verify", true},
                        {"tls_ca_file", "ca.pem"},
                        {"connect_timeout_ms", connect_timeout},
                        {"pool_timeout_ms", 2'000},
                        {"retry_interval_ms", 3'000},
                        {"ping_interval_ms", 4'000},
                        {"ping_timeout_ms", 1'000},
                    };
                    value.services.emplace("mysql", std::move(mysql));
                })
            .build();
    };

    auto valid = make_host("require", 5'000);
    ASSERT_TRUE(valid.has_value());
    if (valid)
        ASSERT_TRUE(valid->services().find<application::mysql_service>() != nullptr);
    ASSERT_FALSE(make_host("opportunistic", 5'000).has_value());
    ASSERT_FALSE(make_host("require", 0).has_value());
}
#endif

TEST(application_auto_configuration_is_opt_in)
{
    auto host = application::application_builder{"manual-configuration-test"}
                    .configure([](application::application_configuration& value)
                        {
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                            value.services.emplace("http_client",
                                application::configured_service{
                                    .name = "http_client",
                                    .enabled = true,
                                });
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    ASSERT_TRUE(host->services().managed_size() == 0U);
}

#if defined(CNETMOD_HAS_PROTOCOL_MYSQL) && defined(CNETMOD_HAS_ORM)
TEST(application_auto_configures_named_orm_shard_topology)
{
    auto host = application::application_builder{"orm-sharding-test"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& value)
                        {
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                            for (const auto* instance : {"orders-0", "orders-1"})
                            {
                                application::configured_service database{
                                    .name = "mysql",
                                    .instance = instance,
                                    .enabled = true,
                                };
                                database.properties["username"] = "test";
                                database.properties["database"] = "test";
                                value.services.emplace(instance,
                                    std::move(database));
                            }
                            value.orm.sharding.enabled = true;
                            value.orm.sharding.topologies.emplace("orders",
                                application::orm_shard_topology_configuration{
                                    .logical_table = "orders",
                                    .table_count = 16,
                                    .databases = {"orders-0", "orders-1"},
                                });
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    ASSERT_TRUE(host->services()
                    .find<application::mysql_sharded_session_gateway>("orders") !=
        nullptr);
}

TEST(application_orm_sharding_disabled_preserves_unsharded_mode)
{
    auto host = application::application_builder{"orm-unsharded-test"}
                    .enable_auto_configuration()
                    .configure([](application::application_configuration& value)
                        {
                            value.logging.manage_lifecycle = false;
                            value.management.enabled = false;
                            application::configured_service database{
                                .name = "mysql",
                                .instance = "default",
                                .enabled = true,
                            };
                            database.properties["username"] = "test";
                            database.properties["database"] = "test";
                            value.services.emplace("mysql", std::move(database));
                        })
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    ASSERT_TRUE(host->services().find<application::mysql_service>() != nullptr);
    ASSERT_TRUE(host->services()
                    .find<application::mysql_sharded_session_gateway>("orders") ==
        nullptr);
}
#endif

#include "application_pool_configuration_cases.inc"
#include "application_port_configuration_cases.inc"
#include "application_scalar_configuration_cases.inc"
#include "application_timeout_configuration_cases.inc"

RUN_TESTS();
