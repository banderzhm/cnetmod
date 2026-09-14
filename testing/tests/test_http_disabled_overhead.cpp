#include "test_framework.hpp"
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_EPOLL
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

import std;
import cnetmod.instrumentation.tracing;
import cnetmod.observability;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import cnetmod.protocol.mongodb;
import cnetmod.application.mongodb;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
import cnetmod.protocol.postgresql;
import cnetmod.application.postgresql;
#endif
import cnetmod.observability.messaging;
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import cnetmod.protocol.kafka;
import cnetmod.protocol.kafka.protocol_value_codec;
import cnetmod.application.kafka;
import cnetmod.observability.kafka_producer;
import cnetmod.observability.kafka_consumer;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import cnetmod.protocol.mqtt;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import cnetmod.protocol.amqp091;
import cnetmod.application.amqp091;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import cnetmod.protocol.amqp10;
#endif
import cnetmod.application.task_supervisor;
import cnetmod.application.health_registry;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.application.service_lifecycle;
import cnetmod.application.service_registry;
import cnetmod.application.host;
import cnetmod.application.configuration;
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import cnetmod.protocol.mysql;
#endif
#ifdef CNETMOD_TEST_ORM
import cnetmod.orm.database_session;
import cnetmod.orm.sql_query_data;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.sql_dialect;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import cnetmod.protocol.redis;
#endif
import cnetmod.core;
import cnetmod.core.dns;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;
#ifdef CNETMOD_HAS_EPOLL
import cnetmod.io.platform.epoll;
#endif
import cnetmod.observability.http;
import cnetmod.observability.http_server;
import cnetmod.observability.otlp;
import cnetmod.coro.spawn;
import cnetmod.coro.task_group;
import cnetmod.executor.async_op;
import cnetmod.coro.timer;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.operation_scope;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.protocol.http.middleware.metrics;
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import cnetmod.protocol.openai;
import cnetmod.observability.openai;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import cnetmod.protocol.grpc;
import cnetmod.observability.grpc;
#endif

namespace {

struct allocation_totals
{
    std::size_t calls{};
    std::size_t bytes{};
};

// Only this test executable replaces allocation. Production allocators and
// coroutine promises are unchanged; unrelated worker allocations are excluded.
thread_local allocation_totals* active_totals{};
thread_local std::optional<std::size_t> fail_after;
thread_local std::size_t injected_allocation_failures = 0;
thread_local void (*before_allocation)() noexcept = nullptr;

auto allocate(std::size_t size, std::size_t alignment) -> void*
{
    if (auto hook = std::exchange(before_allocation, nullptr))
        hook();
    if (fail_after)
    {
        if (*fail_after == 0U)
        {
            fail_after.reset();
            ++injected_allocation_failures;
            throw std::bad_alloc{};
        }
        --*fail_after;
    }
    alignment = std::max(alignment, alignof(void*));
    const bool over_aligned = alignment > alignof(std::max_align_t);
    const auto padding = over_aligned ? sizeof(void*) + alignment - 1U : 0U;
    if (size > std::numeric_limits<std::size_t>::max() - padding)
        throw std::bad_alloc{};
    void* raw{};
    while (!(raw = std::malloc(std::max(size, std::size_t{1}) + padding)))
    {
        auto handler = std::get_new_handler();
        if (!handler)
            throw std::bad_alloc{};
        handler();
    }
    void* result = raw;
    if (over_aligned)
    {
        const auto address = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
        const auto aligned = (address + alignment - 1U) & ~(alignment - 1U);
        result = reinterpret_cast<void*>(aligned);
        static_cast<void**>(result)[-1] = raw;
    }
    if (active_totals)
    {
        ++active_totals->calls;
        active_totals->bytes += size;
    }
    return result;
}

void release(void* pointer) noexcept
{
    std::free(pointer);
}

void release_aligned(void* pointer, std::align_val_t alignment) noexcept
{
    if (pointer && static_cast<std::size_t>(alignment) > alignof(std::max_align_t))
        pointer = static_cast<void**>(pointer)[-1];
    std::free(pointer);
}

class allocation_window
{
public:
    explicit allocation_window(allocation_totals& totals) noexcept
        : previous_(std::exchange(active_totals, &totals)) {}

    ~allocation_window()
    {
        active_totals = previous_;
    }

    allocation_window(const allocation_window&) = delete;
    auto operator=(const allocation_window&) -> allocation_window& = delete;

private:
    allocation_totals* previous_;
};

template <typename Operation>
auto measure(Operation operation) -> allocation_totals
{
    allocation_totals totals;
    allocation_window window{totals};
    for (unsigned index{}; index < 256U; ++index)
        operation();
    return totals;
}

} // namespace

void* operator new(std::size_t size)
{
    return allocate(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer) noexcept
{
    release(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    release(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void operator delete(void* pointer, std::align_val_t alignment) noexcept
{
    release_aligned(pointer, alignment);
}

void operator delete[](void* pointer, std::align_val_t alignment) noexcept
{
    release_aligned(pointer, alignment);
}

void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
    release_aligned(pointer, alignment);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
    release_aligned(pointer, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    release(pointer);
}

void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    release_aligned(pointer, alignment);
}

void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    release_aligned(pointer, alignment);
}

#if defined(CNETMOD_HAS_PROTOCOL_KAFKA) || defined(CNETMOD_HAS_PROTOCOL_MQTT) || defined(CNETMOD_HAS_PROTOCOL_AMQP091) || defined(CNETMOD_HAS_PROTOCOL_AMQP10)
    #ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
        #include "postgresql_reconnect_fault_cases.inc"
        #include "postgresql_waiter_allocation_cases.inc"

TEST(postgresql_warmup_allocation_failure_is_retryable)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    unsigned mapped_failures = 0;
    for (std::size_t allocation = 0; allocation < 12; ++allocation)
    {
        cnetmod::postgresql::connection_pool_options options;
        options.minimum_connections = 1;
        options.maximum_connections = 1;
        options.connection.maximum_message_size = 0;
        cnetmod::postgresql::connection_pool pool{*io, options};
        cnetmod::cancel_token token;
        auto pending = pool.warm_up(token);
        fail_after = allocation;
        pending.handle().resume();
        fail_after.reset();
        ASSERT_TRUE(pending.handle().done());
        auto result = pending.handle().promise().result();
        ASSERT_FALSE(result.has_value());
        if (result.error() == std::errc::not_enough_memory)
            ++mapped_failures;
        ASSERT_EQ(pool.checked_out_count(), std::size_t{0});
        ASSERT_EQ(pool.waiter_count(), std::size_t{0});
        const auto retry = cnetmod::sync_wait(pool.warm_up(token));
        ASSERT_FALSE(retry.has_value());
        ASSERT_TRUE(retry.error() != std::errc::operation_canceled);
        cnetmod::sync_wait(pool.close());
        ASSERT_EQ(pool.size(), std::size_t{0});
    }
    ASSERT_TRUE(mapped_failures > 0);
}

TEST(postgresql_zero_minimum_warmup_rejects_closed_pool)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::postgresql::connection_pool_options options;
    options.minimum_connections = 0;
    cnetmod::postgresql::connection_pool pool{*io, options};
    cnetmod::cancel_token token;
    ASSERT_TRUE(cnetmod::sync_wait(pool.warm_up(token)).has_value());
    cnetmod::sync_wait(pool.close());
    const auto result = cnetmod::sync_wait(pool.warm_up(token));
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::make_error_code(std::errc::operation_canceled));
    ASSERT_EQ(pool.size(), std::size_t{0});
}

TEST(postgresql_pool_allocation_failure_releases_connection_reservation)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    unsigned mapped_failures = 0;
    for (std::size_t allocation = 0; allocation < 8; ++allocation)
    {
        cnetmod::postgresql::connection_pool_options options;
        options.minimum_connections = 0;
        options.maximum_connections = 1;
        // Invalid before network I/O, so every path must finish synchronously.
        options.connection.maximum_message_size = 0;
        cnetmod::postgresql::connection_pool pool{*io, options};
        cnetmod::cancel_token token;
        auto pending = pool.acquire(token);
        fail_after = allocation;
        pending.handle().resume();
        fail_after.reset();
        ASSERT_TRUE(pending.handle().done());
        try
        {
            auto result = pending.handle().promise().result();
            ASSERT_FALSE(result.has_value());
            if (result.error() == std::errc::not_enough_memory)
                ++mapped_failures;
        }
        catch (const std::bad_alloc&)
        {
            // Allocation before a slot exists may still propagate to the caller.
        }
        ASSERT_EQ(pool.checked_out_count(), std::size_t{0});
        ASSERT_EQ(pool.waiter_count(), std::size_t{0});
        auto retry = cnetmod::sync_wait(pool.acquire(token));
        ASSERT_FALSE(retry.has_value());
        ASSERT_EQ(pool.checked_out_count(), std::size_t{0});
        cnetmod::sync_wait(pool.close());
        ASSERT_EQ(pool.size(), std::size_t{0});
    }
    ASSERT_TRUE(mapped_failures > 0);
}
    #endif

TEST(messaging_injection_allocation_failures_preserve_original_context)
{
    namespace messaging = cnetmod::observability::messaging;
    auto previous = cnetmod::instrumentation::new_root_context();
    previous.tracestate = "vendor=" + std::string(120, 'a');
    auto replacement = cnetmod::instrumentation::new_root_context();
    replacement.tracestate = "vendor=" + std::string(160, 'b');
    const std::string business_value(256, 'x');
    const std::vector<std::byte> payload(4096, std::byte{0xa5});
    const auto exercise = [&](auto original, auto extract)
    {
        messaging::inject(original, previous);
        unsigned failures = 0;
        unsigned successes = 0;
        for (std::size_t allocation = 0; allocation < 64; ++allocation)
        {
            auto carrier = original;
            fail_after = allocation;
            messaging::inject(carrier, replacement);
            const bool failed = !fail_after.has_value();
            fail_after.reset();
            failed ? ++failures : ++successes;
            const auto observed = extract(carrier);
            ASSERT_TRUE(observed.has_value());
            const auto& expected = failed ? previous : replacement;
            ASSERT_EQ(observed->trace_id, expected.trace_id);
            ASSERT_EQ(observed->span_id, expected.span_id);
            ASSERT_EQ(observed->tracestate, expected.tracestate);
        }
        ASSERT_TRUE(failures > 0);
        ASSERT_TRUE(successes > 0);
    };
    #ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    cnetmod::kafka::record kafka_message;
    kafka_message.value = payload;
    kafka_message.headers.push_back({"business", payload});
    exercise(kafka_message, [&](const auto& carrier)
        {
            ASSERT_TRUE(carrier.value == kafka_message.value);
            const auto business = std::ranges::find(carrier.headers, "business", &cnetmod::kafka::header::key);
            ASSERT_TRUE(business != carrier.headers.end());
            ASSERT_TRUE(business->value == payload);
            ASSERT_EQ(carrier.headers.size(), std::size_t{3});
            cnetmod::kafka::consumed_record consumed;
            consumed.headers = carrier.headers;
            return messaging::extract(consumed);
        });
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_MQTT
    cnetmod::mqtt::properties mqtt_properties;
    mqtt_properties.push_back(cnetmod::mqtt::mqtt_property::string_pair_prop(
        cnetmod::mqtt::property_id::user_property, "business", business_value));
    exercise(mqtt_properties, [&](const auto& carrier)
        {
            unsigned business_count = 0;
            for (const auto& property : carrier)
            {
                const auto* pair = std::get_if<std::pair<std::string, std::string>>(&property.value);
                if (property.id == cnetmod::mqtt::property_id::user_property && pair && pair->first == "business")
                {
                    ++business_count;
                    ASSERT_EQ(pair->second, business_value);
                }
            }
            ASSERT_EQ(business_count, 1U);
            ASSERT_EQ(carrier.size(), std::size_t{3});
            return messaging::extract(carrier);
        });
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    cnetmod::amqp091::message amqp091_message;
    amqp091_message.body = payload;
    amqp091_message.headers.emplace("business", business_value);
    exercise(amqp091_message, [&](const auto& carrier)
        {
            ASSERT_TRUE(carrier.body == payload);
            ASSERT_EQ(carrier.headers.at("business"), business_value);
            ASSERT_EQ(carrier.headers.size(), std::size_t{3});
            return messaging::extract(carrier);
        });
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    cnetmod::amqp10::message amqp10_message;
    amqp10_message.body = payload;
    amqp10_message.application.emplace("business", cnetmod::amqp10::value{business_value});
    exercise(amqp10_message, [&](const auto& carrier)
        {
            ASSERT_TRUE(std::get<cnetmod::amqp10::binary>(carrier.body) == payload);
            ASSERT_EQ(std::get<std::string>(carrier.application.at("business").data), business_value);
            ASSERT_EQ(carrier.application.size(), std::size_t{3});
            return messaging::extract(carrier);
        });
    #endif
}

#endif

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    #define CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT
    #include "kafka_runtime_lifecycle_cases.inc"
    #undef CNETMOD_TEST_KAFKA_MAINTENANCE_FAULT

TEST(kafka_trace_injection_does_not_copy_business_header_storage)
{
    auto context = cnetmod::instrumentation::new_root_context();
    context.tracestate = "vendor=value";
    allocation_totals baseline;
    for (const std::size_t size : {std::size_t{16}, std::size_t{1024 * 1024}})
    {
        cnetmod::kafka::record carrier;
        carrier.headers.push_back({"business", cnetmod::kafka::bytes(size, std::byte{0xff})});
        const auto* storage = carrier.headers.front().value.data();
        const auto totals = measure([&]
            {
                cnetmod::observability::messaging::inject(carrier, context);
            });
        ASSERT_EQ(carrier.headers.size(), std::size_t{3});
        ASSERT_TRUE(carrier.headers.front().value.data() == storage);
        ASSERT_EQ(carrier.headers.front().value.size(), size);
        if (size == 16)
            baseline = totals;
        else
        {
            ASSERT_EQ(totals.calls, baseline.calls);
            ASSERT_EQ(totals.bytes, baseline.bytes);
        }
    }
}

TEST(kafka_trace_extraction_skips_unrelated_binary_headers_without_allocation)
{
    cnetmod::kafka::consumed_record carrier;
    carrier.headers.push_back({"business", cnetmod::kafka::bytes(65536, std::byte{0xff})});
    carrier.headers.push_back({"empty", {}});
    bool found = false;
    fail_after = 0U;
    const auto totals = measure([&]
        {
            found = cnetmod::observability::messaging::extract(carrier).has_value();
        });
    const bool untouched = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(untouched);
    ASSERT_FALSE(found);
    ASSERT_EQ(totals.calls, std::size_t{0});
    ASSERT_EQ(totals.bytes, std::size_t{0});
    ASSERT_EQ(carrier.headers.front().value.size(), std::size_t{65536});
}

TEST(kafka_metadata_notification_allocation_failure_preserves_update)
{
    struct observer final : cnetmod::kafka::metadata_observer
    {
        unsigned calls = 0;

        void on_metadata_changed(const cnetmod::kafka::protocol::metadata_response&) override
        {
            ++calls;
        }
    };

    unsigned failures = 0, successes = 0;
    for (std::size_t allocation = 0; allocation < 8; ++allocation)
    {
        cnetmod::kafka::metadata_cache cache;
        auto recording = std::make_shared<observer>();
        cache.add_observer(recording);
        cnetmod::kafka::protocol::metadata_response metadata;
        metadata.cluster_id = std::string(65536, 'c');
        metadata.controller_id = 42;
        bool escaped = false;
        fail_after = allocation;
        try
        {
            cache.update(std::move(metadata));
        }
        catch (...)
        {
            escaped = true;
        }
        const bool injected = !fail_after.has_value();
        fail_after.reset();
        injected ? ++failures : ++successes;
        ASSERT_FALSE(escaped);
        auto committed = cache.snapshot();
        ASSERT_EQ(committed.controller_id, 42);
        ASSERT_EQ(committed.cluster_id->size(), std::size_t{65536});
        ASSERT_EQ(recording->calls, injected ? 0U : 1U);
        const auto before = recording->calls;
        cache.update(std::move(committed));
        ASSERT_EQ(recording->calls, before + 1U);
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
}

TEST(kafka_processing_allocation_failure_preserves_record)
{
    auto parent = cnetmod::instrumentation::new_root_context();
    parent.tracestate = "vendor=" + std::string(120, 'a');
    cnetmod::kafka::record outgoing;
    outgoing.value = cnetmod::kafka::bytes(4096, std::byte{0x42});
    cnetmod::observability::messaging::inject(outgoing, parent);
    cnetmod::kafka::consumed_record incoming;
    incoming.headers = std::move(outgoing.headers);
    incoming.value = std::move(outgoing.value);
    const auto* payload = incoming.value->data();
    unsigned exported = 0, failures = 0, successes = 0;
    cnetmod::instrumentation::span_exporter sink{[&](const auto&)
        {
            ++exported;
        }};
    for (std::size_t allocation = 0; allocation < 32; ++allocation)
    {
        const auto before = exported;
        fail_after = allocation;
        auto scope = cnetmod::observability::start_kafka_processing(incoming, sink);
        const bool injected = !fail_after.has_value();
        fail_after.reset();
        injected ? ++failures : ++successes;
        const bool active = scope.context() != nullptr;
        scope.complete();
        ASSERT_EQ(exported, before + (active ? 1U : 0U));
        if (!injected)
            ASSERT_TRUE(active);
        const auto extracted = cnetmod::observability::messaging::extract(incoming);
        ASSERT_TRUE(extracted.has_value());
        ASSERT_EQ(extracted->trace_id, parent.trace_id);
        ASSERT_EQ(extracted->span_id, parent.span_id);
        ASSERT_EQ(extracted->tracestate, parent.tracestate);
        ASSERT_TRUE(incoming.value->data() == payload);
        ASSERT_EQ(incoming.value->size(), std::size_t{4096});
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
}

TEST(kafka_disabled_processing_skips_context_allocation)
{
    cnetmod::kafka::consumed_record record;
    record.headers.push_back({"tracestate", cnetmod::kafka::bytes(65536, std::byte{'a'})});
    fail_after = 0;
    const auto totals = measure([&]
        {
            auto scope = cnetmod::observability::start_kafka_processing(record, {});
            scope.complete();
        });
    const bool unused = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(unused);
    ASSERT_EQ(totals.calls, std::size_t{0});
    ASSERT_EQ(totals.bytes, std::size_t{0});
}

TEST(kafka_disabled_send_matches_raw_allocations)
{
    cnetmod::kafka::producer producer{nullptr};
    producer.close();
    cnetmod::observability::instrumented_kafka_producer owned{cnetmod::kafka::producer{nullptr}};
    owned.close();
    const auto parent = cnetmod::instrumentation::new_root_context();
    for (const bool with_token : {false, true})
    {
        cnetmod::cancel_token token;
        const auto raw = measure([&]
            {
                auto result = cnetmod::sync_wait(with_token ? producer.send("topic", {}, token)
                                                            : producer.send("topic", {}));
                if (result)
                    throw std::runtime_error("expected closed producer");
            });
        const auto disabled = measure([&]
            {
                auto result = cnetmod::sync_wait(cnetmod::observability::send_kafka_record(
                    producer, "topic", {}, parent, {}, with_token ? &token : nullptr));
                if (result)
                    throw std::runtime_error("expected closed producer");
            });
        ASSERT_EQ(disabled.calls, raw.calls);
        ASSERT_EQ(disabled.bytes, raw.bytes);
        const auto decorated = measure([&]
            {
                auto result = cnetmod::sync_wait(owned.send("topic", {}, parent, with_token ? &token : nullptr));
                if (result)
                    throw std::runtime_error("expected closed producer");
            });
        ASSERT_EQ(decorated.calls, raw.calls);
        ASSERT_EQ(decorated.bytes, raw.bytes);
    }
}

TEST(kafka_observed_frame_allocation_failure_preserves_send)
{
    namespace kafka = cnetmod::kafka;

    struct backend final : kafka::producer_backend
    {
        kafka::record received;
        std::function<void()> during_send;

        auto partitions(std::string_view) -> kafka::result<std::vector<std::int32_t>> override
        {
            return std::vector<std::int32_t>{0};
        }

        auto initialize_idempotent(std::optional<std::string_view>, std::chrono::milliseconds, cnetmod::cancel_token*)
            -> cnetmod::task<kafka::result<std::pair<std::int64_t, std::int16_t>>> override
        {
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
            received = records.front();
            if (during_send)
                during_send();
            co_return std::vector<kafka::record_metadata>{{destination, 17, 123}};
        }

        auto add_transaction_partitions(std::string_view, std::int64_t, std::int16_t,
            std::span<const kafka::topic_partition>, cnetmod::cancel_token*) -> cnetmod::task<kafka::result<void>> override
        {
            co_return kafka::result<void>{};
        }

        auto add_transaction_offsets(std::string_view, std::int64_t, std::int16_t, std::string_view,
            const std::map<kafka::topic_partition, kafka::offset_and_metadata>&, cnetmod::cancel_token*)
            -> cnetmod::task<kafka::result<void>> override
        {
            co_return kafka::result<void>{};
        }

        auto finish_transaction(std::string_view, std::int64_t, std::int16_t, bool, cnetmod::cancel_token*)
            -> cnetmod::task<kafka::result<void>> override
        {
            co_return kafka::result<void>{};
        }
    };

    {
        auto transport = std::make_shared<backend>();
        kafka::producer producer{transport, {.linger = std::chrono::milliseconds{0}, .idempotent = false}};
        bool injected = false;
        bool settled = false;
        transport->during_send = [&]
        {
            auto pending = producer.send("orders", {});
            pending.handle().resume();
            ASSERT_FALSE(pending.handle().done());
            fail_after = 0U;
            producer.close();
            injected = !fail_after.has_value();
            fail_after.reset();
            settled = pending.handle().done();
            ASSERT_TRUE(settled);
            if (settled)
            {
                const auto result = pending.handle().promise().result();
                ASSERT_FALSE(result.has_value());
                if (!result)
                    ASSERT_TRUE(result.error().code == kafka::error_code::configuration);
            }
        };
        (void)cnetmod::sync_wait(producer.send("orders", {}));
        ASSERT_TRUE(injected);
        ASSERT_TRUE(settled);
    }
    for (const bool with_token : {false, true})
    {
        auto transport = std::make_shared<backend>();
        kafka::producer producer{transport, {.linger = std::chrono::milliseconds{0}, .idempotent = false}};
        const std::string topic(128, 't');
        kafka::record record;
        record.value = kafka::bytes(4096, std::byte{0x42});
        record.headers.push_back({"business", {std::byte{1}, std::byte{2}}});
        cnetmod::cancel_token token;
        unsigned sampled = 0;
        cnetmod::instrumentation::span_exporter sink{
            [](const auto&) {},
            [&](const auto&)
            {
                ++sampled;
                fail_after = 0U;
                return true;
            }};
        bool escaped = false;
        cnetmod::kafka::result<cnetmod::kafka::record_metadata> result;
        try
        {
            result = cnetmod::sync_wait(cnetmod::observability::send_kafka_record(
                producer, topic, std::move(record), {}, sink, with_token ? &token : nullptr));
        }
        catch (...)
        {
            escaped = true;
        }
        const bool injected = !fail_after.has_value();
        fail_after.reset();
        ASSERT_EQ(sampled, 1U);
        ASSERT_TRUE(injected);
        ASSERT_FALSE(escaped);
        ASSERT_TRUE(result.has_value());
        if (result)
        {
            ASSERT_EQ(result->target.topic, topic);
            ASSERT_EQ(result->offset, std::int64_t{17});
        }
        ASSERT_TRUE(transport->received.value.has_value());
        ASSERT_TRUE(*transport->received.value == kafka::bytes(4096, std::byte{0x42}));
        ASSERT_EQ(transport->received.headers.size(), std::size_t{1});
        ASSERT_EQ(transport->received.headers.front().key, std::string{"business"});
        ASSERT_TRUE(transport->received.headers.front().value == kafka::bytes({std::byte{1}, std::byte{2}}));

        const auto send = [&](bool observed)
        {
            auto pending = observed
                ? cnetmod::observability::send_kafka_record(producer, topic, transport->received, {}, {}, with_token ? &token : nullptr)
                : with_token ? producer.send(topic, transport->received, token)
                             : producer.send(topic, transport->received);
            auto sent = cnetmod::sync_wait(std::move(pending));
            if (!sent || sent->target.topic != topic || sent->offset != 17)
                throw std::runtime_error("successful-send allocation fixture failed");
        };
        send(false);
        send(true);
        const auto raw = measure([&]
            {
                send(false);
            });
        const auto disabled = measure([&]
            {
                send(true);
            });
        ASSERT_EQ(disabled.calls, raw.calls);
        ASSERT_EQ(disabled.bytes, raw.bytes);
    }
}

TEST(kafka_metadata_update_without_live_observers_does_not_allocate)
{
    for (const bool expired_registration : {false, true})
    {
        cnetmod::kafka::metadata_cache cache;
        if (expired_registration)
            cache.add_observer(std::weak_ptr<cnetmod::kafka::metadata_observer>{});
        cnetmod::kafka::protocol::metadata_response metadata;
        metadata.cluster_id = std::string(65536, 'c');
        metadata.controller_id = 42;
        bool escaped = false;
        fail_after = 0U;
        allocation_totals totals;
        {
            allocation_window window{totals};
            try
            {
                cache.update(std::move(metadata));
            }
            catch (...)
            {
                escaped = true;
            }
        }
        const bool unused_failure = fail_after.has_value();
        fail_after.reset();
        ASSERT_FALSE(escaped);
        ASSERT_TRUE(unused_failure);
        ASSERT_EQ(totals.calls, std::size_t{0});
        ASSERT_EQ(totals.bytes, std::size_t{0});
        const auto committed = cache.snapshot();
        ASSERT_EQ(committed.controller_id, 42);
        ASSERT_TRUE(committed.cluster_id.has_value());
        ASSERT_EQ(committed.cluster_id->size(), std::size_t{65536});
    }
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MQTT
TEST(mqtt_trace_injection_does_not_copy_business_property_storage)
{
    auto context = cnetmod::instrumentation::new_root_context();
    context.tracestate = "vendor=value";
    allocation_totals baseline;
    for (const std::size_t size : {std::size_t{256}, std::size_t{1024 * 1024}})
    {
        cnetmod::mqtt::properties carrier;
        carrier.push_back(cnetmod::mqtt::mqtt_property::string_pair_prop(
            cnetmod::mqtt::property_id::user_property, "business", std::string(size, 'x')));
        const auto* storage = std::get<std::pair<std::string, std::string>>(carrier.front().value).second.data();
        const auto totals = measure([&]
            {
                cnetmod::observability::messaging::inject(carrier, context);
            });
        const auto& business = std::get<std::pair<std::string, std::string>>(carrier.front().value);
        ASSERT_EQ(carrier.size(), std::size_t{3});
        ASSERT_TRUE(business.second.data() == storage);
        ASSERT_EQ(business.second.size(), size);
        if (size == 256)
            baseline = totals;
        else
        {
            ASSERT_EQ(totals.calls, baseline.calls);
            ASSERT_EQ(totals.bytes, baseline.bytes);
        }
    }
}
#endif

#if defined(CNETMOD_HAS_PROTOCOL_AMQP091) || defined(CNETMOD_HAS_PROTOCOL_AMQP10)
TEST(amqp_trace_injection_retains_business_map_nodes_and_storage)
{
    const auto context = cnetmod::instrumentation::new_root_context();
    const auto exercise = [&](auto empty, auto prepare, auto read)
    {
        allocation_totals baseline;
        for (const std::size_t size : {std::size_t{256}, std::size_t{1024 * 1024}})
        {
            auto carrier = empty;
            prepare(carrier, std::string(size, 'x'));
            const auto* value = &read(carrier);
            const auto* storage = value->data();
            const auto totals = measure([&]
                {
                    cnetmod::observability::messaging::inject(carrier, context);
                });
            ASSERT_TRUE(&read(carrier) == value);
            ASSERT_TRUE(read(carrier).data() == storage);
            ASSERT_EQ(read(carrier).size(), size);
            if (size == 256)
                baseline = totals;
            else
            {
                ASSERT_EQ(totals.calls, baseline.calls);
                ASSERT_EQ(totals.bytes, baseline.bytes);
            }
        }
    };
    #ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    exercise(cnetmod::amqp091::message{}, [](auto& carrier, std::string value)
        {
            carrier.headers.emplace("business", std::move(value));
        },
        [](const auto& carrier) -> const std::string&
        {
            return carrier.headers.at("business");
        });
    #endif
    #ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    exercise(cnetmod::amqp10::message{}, [](auto& carrier, std::string value)
        {
            carrier.application.emplace("business", cnetmod::amqp10::value{std::move(value)});
        },
        [](const auto& carrier) -> const std::string&
        {
            return std::get<std::string>(carrier.application.at("business").data);
        });
    #endif
}
#endif

#include "application_reload_allocation_cases.inc"
#include "http_disabled_success_cases.inc"

TEST(disabled_http_dispatch_allocates_exactly_the_original_task_frame)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    cnetmod::observability::instrumented_http_client disabled{raw, {}, {}};
    cnetmod::http::request request{cnetmod::http::http_method::GET, "invalid://request"};
    cnetmod::http::tracing::trace_context parent;
    cnetmod::cancel_token token;
    bool valid_frames = true;
    for (bool cancellable : {false, true})
    {
        const auto baseline = measure([&]
            {
                auto operation = cancellable ? raw.send(request, token) : raw.send(request);
                valid_frames = valid_frames && static_cast<bool>(operation.handle());
            });
        const auto decorated = measure([&]
            {
                auto operation = cancellable ? disabled.send(request, parent, token) : disabled.send(request, parent);
                valid_frames = valid_frames && static_cast<bool>(operation.handle());
            });
        ASSERT_TRUE(baseline.calls > 0U);
        ASSERT_EQ(decorated.calls, baseline.calls);
        ASSERT_EQ(decorated.bytes, baseline.bytes);
    }
    ASSERT_TRUE(valid_frames);
}

TEST(allocation_probe_detects_enabled_observation_work)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    cnetmod::observability::instrumented_http_client enabled{raw, {}, [](auto) {}};
    cnetmod::http::request request{cnetmod::http::http_method::GET, "invalid://request"};
    cnetmod::http::tracing::trace_context parent;
    (void)cnetmod::sync_wait(raw.send(request));
    (void)cnetmod::sync_wait(enabled.send(request, parent));
    const auto baseline = measure([&]
        {
            (void)cnetmod::sync_wait(raw.send(request));
        });
    const auto observed = measure([&]
        {
            (void)cnetmod::sync_wait(enabled.send(request, parent));
        });
    ASSERT_TRUE(observed.calls > baseline.calls);
    ASSERT_TRUE(observed.bytes > baseline.bytes);
}

TEST(disabled_http_execution_matches_original_error_path_allocations)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    cnetmod::observability::instrumented_http_client disabled{raw, {}, {}};
    cnetmod::http::request request{cnetmod::http::http_method::GET, "invalid://request"};
    auto parent = cnetmod::http::tracing::new_root_context();
    parent.tracestate = "vendor=" + std::string(240, 'a');
    cnetmod::cancel_token token;
    for (bool cancellable : {false, true})
    {
        const auto expected = cnetmod::sync_wait(cancellable ? raw.send(request, token) : raw.send(request));
        ASSERT_FALSE(expected.has_value());
        (void)cnetmod::sync_wait(cancellable ? disabled.send(request, parent, token) : disabled.send(request, parent));
        bool matching_results = true;
        const auto baseline = measure([&]
            {
                const auto result = cnetmod::sync_wait(cancellable ? raw.send(request, token) : raw.send(request));
                matching_results = matching_results && !result && result.error() == expected.error();
            });
        const auto decorated = measure([&]
            {
                const auto result = cnetmod::sync_wait(cancellable ? disabled.send(request, parent, token) : disabled.send(request, parent));
                matching_results = matching_results && !result && result.error() == expected.error();
            });
        ASSERT_TRUE(matching_results);
        ASSERT_TRUE(baseline.calls > 0U);
        ASSERT_EQ(decorated.calls, baseline.calls);
        ASSERT_EQ(decorated.bytes, baseline.bytes);
    }
}

TEST(disabled_exporter_statistic_refresh_allocates_nothing)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io, {.export_metrics = false}};
    const auto measured = measure([&]
        {
            telemetry.refresh_exporter_metrics();
        });
    ASSERT_EQ(measured.calls, 0U);
    ASSERT_EQ(measured.bytes, 0U);
}

TEST(exporter_statistic_refresh_contains_allocation_failures)
{
    for (std::size_t allocation = 0; allocation < 32; ++allocation)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io};
        fail_after = allocation;
        telemetry.refresh_exporter_metrics();
        fail_after.reset();
        telemetry.refresh_exporter_metrics();
        const auto rendered = telemetry.metrics().render_openmetrics();
        ASSERT_TRUE(rendered.contains("otel_exporter_invalid_responses_total 0\n"));
        ASSERT_TRUE(rendered.contains("otel_exporter_worker_failures_total 0\n"));
    }
}

TEST(disabled_server_metrics_factory_allocates_nothing)
{
    bool empty = true;
    const auto totals = measure([&]
        {
            auto middleware = cnetmod::observability::server_metrics({});
            empty = empty && !middleware;
        });
    ASSERT_TRUE(empty);
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
}

TEST(exporter_submission_contains_callback_and_post_node_allocation_failures)
{
    for (std::size_t successful_allocations : {0U, 1U})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::otlp_http_exporter exporter{*io,
            {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
        cnetmod::observability::otel_metric_record measurement{.name = "fault.test", .value = 1};
        fail_after = successful_allocations;
        const bool accepted = exporter.submit(std::move(measurement));
        const bool injected = !fail_after;
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_TRUE(accepted);
        ASSERT_EQ(exporter.statistics().worker_failures, 1U);
        ASSERT_TRUE(exporter.submit(cnetmod::observability::otel_metric_record{
            .name = "next",
            .value = 1}));
        ASSERT_EQ(exporter.statistics().accepted_metrics, 2U);
        ASSERT_EQ(exporter.statistics().worker_failures, 1U);
        exporter.close();
    }
}

TEST(exporter_shutdown_survives_flush_frame_failure_or_allocation_elision)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::otlp_http_exporter exporter{*io,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
    bool injected{};
    bool settled{};
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto operation = exporter.shutdown(std::chrono::milliseconds{10}, std::chrono::milliseconds{10});
        fail_after = 0;
        const auto result = co_await std::move(operation);
        injected = !fail_after;
        fail_after.reset();
        settled = result.has_value();
        io->stop();
    };
    cnetmod::spawn(*io, exercise());
    io->run();
#ifdef _MSC_VER
    ASSERT_TRUE(injected);
#else
    // Clang can elide this nested frame. An unconsumed fail-at-zero sentinel
    // then proves the idle path made no allocation attempt, not fault handling.
    (void)injected;
#endif
    ASSERT_TRUE(settled);
    ASSERT_EQ(exporter.statistics().worker_failures, 0U);
}

TEST(telemetry_hub_abort_is_allocation_free_and_closes_producers)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
    fail_after = 0;
    telemetry.abort();
    telemetry.abort();
    const bool allocated = !fail_after;
    fail_after.reset();
    ASSERT_FALSE(allocated);
    ASSERT_FALSE(telemetry.submit_metric({.name = "closed", .value = 1}));
}

TEST(exporter_abort_cleans_failed_submission_without_allocating)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::otlp_http_exporter exporter{*io,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
    cnetmod::observability::otel_metric_record measurement{.name = "fault.test", .value = 1};
    fail_after = 0;
    const bool accepted = exporter.submit(std::move(measurement));
    const bool submission_fault = !fail_after;
    fail_after = 0;
    exporter.abort();
    exporter.abort();
    const bool abort_allocated = !fail_after;
    fail_after.reset();
    ASSERT_TRUE(accepted && submission_fault);
    ASSERT_FALSE(abort_allocated);
    ASSERT_EQ(exporter.statistics().worker_failures, 1U);
    ASSERT_EQ(exporter.statistics().dropped_metrics, 1U);
    ASSERT_EQ(exporter.statistics().dropped, 1U);
}

TEST(cancellation_registration_arbitrates_completion_without_allocation)
{
    cnetmod::cancel_token token;
    unsigned notifications{};
    const auto notify = +[](void* operation) noexcept
    {
        ++*static_cast<unsigned*>(operation);
    };
    token.cancel();
    ASSERT_FALSE(token.register_callback(&notifications, notify));
    ASSERT_EQ(notifications, 0U);
    token.reset();
    ASSERT_TRUE(token.register_callback(&notifications, notify));
    ASSERT_TRUE(token.complete_callback(&notifications));
    token.cancel();
    ASSERT_EQ(notifications, 0U);
    token.reset();
    ASSERT_TRUE(token.register_callback(&notifications, notify));
    fail_after = 0U;
    token.cancel();
    token.cancel();
    const auto claimed = token.complete_callback(&notifications);
    const bool allocation_free = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(allocation_free);
    ASSERT_FALSE(claimed);
    ASSERT_EQ(notifications, 1U);
    ASSERT_TRUE(token.pending_.load());
    token.finish_callback(&notifications);
    ASSERT_FALSE(token.pending_.load());
    for (unsigned attempt = 0; attempt < 128; ++attempt)
    {
        token.reset();
        notifications = 0;
        ASSERT_TRUE(token.register_callback(&notifications, notify));
        std::atomic<bool> start{};
        std::jthread cancellation{[&]
            {
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                token.cancel();
            }};
        start.store(true, std::memory_order_release);
        const bool completed = token.complete_callback(&notifications);
        cancellation.join();
        ASSERT_EQ(notifications, completed ? 0U : 1U);
        token.finish_callback(&notifications);
        ASSERT_FALSE(token.pending_.load());
    }
}

TEST(completed_cancellation_registration_does_not_mask_next_platform_operation)
{
    cnetmod::cancel_token token;
    unsigned notifications{};
    ASSERT_TRUE(token.register_callback(&notifications, +[](void*) noexcept {}));
    ASSERT_TRUE(token.complete_callback(&notifications));
    token.ctx_ = &notifications;
    token.cancel_fn_ = +[](cnetmod::cancel_token& current) noexcept
    {
        ++*static_cast<unsigned*>(current.ctx_);
        current.pending_.store(false);
    };
    token.pending_.store(true);
    token.cancel();
    ASSERT_EQ(notifications, 1U);
    ASSERT_FALSE(token.pending_.load());
}

TEST(io_context_handoff_uses_frame_owned_queue_storage)
{
    auto io = cnetmod::make_io_context();
    unsigned resumed{};
    auto run = [&]() -> cnetmod::task<void>
    {
        co_await cnetmod::post_awaitable{*io};
        ++resumed;
        co_await cnetmod::post_awaitable{*io};
        ++resumed;
    };
    auto operation = run();
    fail_after = 0U;
    operation.handle().resume();
    const bool allocation_free = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(allocation_free);
    ASSERT_EQ(resumed, 0U);
    io->poll();
    ASSERT_TRUE(resumed >= 1U && resumed <= 2U);
    io->poll();
    ASSERT_EQ(resumed, 2U);
    ASSERT_TRUE(operation.handle().done());
}

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
TEST(redis_waiter_foreign_cancellation_races_pool_stop)
{
    for (unsigned iteration = 0; iteration < 128; ++iteration)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::redis::pool_params options;
        options.initial_size = 0;
        cnetmod::redis::connection_pool pool{*io, options};
        cnetmod::cancel_token token;
        unsigned completions = 0;
        std::error_code observed;
        auto acquire = [&]() -> cnetmod::task<void>
        {
            auto result = co_await pool.async_get_connection(token);
            ++completions;
            if (!result)
                observed = result.error();
        };
        auto pending = acquire();
        pending.handle().resume();
        ASSERT_EQ(pool.waiter_count(), 1U);
        std::atomic<bool> ready{false};
        std::atomic<bool> go{false};
        std::jthread cancelling{[&]
            {
                ready.store(true, std::memory_order_release);
                while (!go.load(std::memory_order_acquire))
                    std::this_thread::yield();
                if (iteration % 3 == 0)
                    std::this_thread::yield();
                token.cancel();
            }};
        while (!ready.load(std::memory_order_acquire))
            std::this_thread::yield();
        auto stopping = pool.cancel();
        go.store(true, std::memory_order_release);
        if (iteration % 3 == 1)
            std::this_thread::yield();
        stopping.handle().resume();
        io->poll();
        cancelling.join();
        io->poll();
        ASSERT_TRUE(stopping.handle().done());
        stopping.handle().promise().result();
        ASSERT_TRUE(pending.handle().done());
        pending.handle().promise().result();
        ASSERT_EQ(completions, 1U);
        ASSERT_TRUE(observed == std::errc::operation_canceled);
        ASSERT_EQ(pool.waiter_count(), 0U);
        io->poll();
        ASSERT_EQ(completions, 1U);
    }
}

TEST(redis_waiter_cancellation_and_stop_schedule_without_allocation)
{
    for (bool stop_pool : {false, true})
    {
        auto io = cnetmod::make_io_context();
        cnetmod::redis::pool_params options;
        options.initial_size = 0;
        cnetmod::redis::connection_pool pool{*io, options};
        cnetmod::cancel_token token;
        auto pending = pool.async_get_connection(token);
        pending.handle().resume();
        ASSERT_EQ(pool.waiter_count(), 1U);
        auto stopping = pool.cancel();
        allocation_totals totals;
        fail_after = 0U;
        {
            allocation_window window{totals};
            if (stop_pool)
                stopping.handle().resume();
            token.cancel();
            token.cancel();
        }
        fail_after.reset();
        ASSERT_EQ(totals.calls, 0U);
        ASSERT_EQ(totals.bytes, 0U);
        io->poll();
        ASSERT_TRUE(pending.handle().done());
        auto result = pending.handle().promise().result();
        ASSERT_FALSE(result.has_value());
        ASSERT_TRUE(result.error() == std::errc::operation_canceled);
        ASSERT_EQ(pool.waiter_count(), 0U);
        if (stop_pool)
        {
            ASSERT_TRUE(stopping.handle().done());
            stopping.handle().promise().result();
        }
    }
}
#endif

TEST(request_deadline_reports_wrapper_allocation_cost)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    cnetmod::http::header_map headers;
    cnetmod::http::response response;
    cnetmod::http::request_context request{*io, peer, "GET", "/", headers, {}, response, {}};
    auto factory = [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<int, std::error_code>>
    {
        co_return 42;
    };
    auto sample = [&](bool request_scoped)
    {
        allocation_totals totals;
        cnetmod::task<std::expected<int, std::error_code>> operation;
        {
            allocation_window window{totals};
            operation = request_scoped ? request.with_deadline(factory)
                                       : cnetmod::with_deadline(*io, cnetmod::deadline{}, factory);
            operation.handle().resume();
        }
        ASSERT_TRUE(operation.handle().done());
        const auto result = operation.handle().promise().result();
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(*result, 42);
        return totals;
    };
    (void)sample(false);
    (void)sample(true);
    const auto direct = sample(false);
    const auto scoped = sample(true);
    logger::init("deadline-allocation", logger::level::info);
    logger::info{"deadline allocation: direct calls={} bytes={}, request calls={} bytes={}, request_context bytes={}",
        direct.calls, direct.bytes, scoped.calls, scoped.bytes, sizeof(cnetmod::http::request_context)};
    logger::shutdown();
    ASSERT_EQ(scoped.calls, direct.calls);
}

TEST(guarded_dispatch_allocation_cost_matches_plain_dispatch)
{
    auto measure_dispatch = [](bool guarded)
    {
        auto io = cnetmod::make_io_context();
        auto work = [&]() -> cnetmod::task<void>
        {
            io->stop();
            co_return;
        };
        auto operation = work();
        allocation_totals totals;
        {
            allocation_window window{totals};
            if (guarded)
                cnetmod::spawn_guarded<[](std::exception_ptr) {}>(*io, std::move(operation));
            else
                cnetmod::spawn(*io, std::move(operation));
        }
        io->run();
        return totals;
    };
    (void)measure_dispatch(false);
    (void)measure_dispatch(true);
    const auto plain = measure_dispatch(false);
    const auto guarded = measure_dispatch(true);
    ASSERT_EQ(guarded.calls, plain.calls);
    ASSERT_EQ(guarded.bytes, plain.bytes);
}

TEST(static_guarded_dispatch_preserves_failure_and_contains_observer_exception)
{
    static unsigned reports;
    static std::error_code observed;
    reports = 0;
    observed.clear();
    auto io = cnetmod::make_io_context();
    auto operation = [&]() -> cnetmod::task<void>
    {
        io->stop();
        throw std::system_error(std::make_error_code(std::errc::permission_denied));
        co_return;
    };
    cnetmod::spawn_guarded<[](std::exception_ptr failure)
        {
            ++reports;
            try
            {
                std::rethrow_exception(failure);
            }
            catch (const std::system_error& error)
            {
                observed = error.code();
            }
            throw std::runtime_error("observer failure");
        }>(*io, operation());
    io->run();
    ASSERT_EQ(reports, 1U);
    ASSERT_EQ(observed, std::make_error_code(std::errc::permission_denied));
}

TEST(guarded_dispatch_faults_and_queue_discard_release_owned_resources)
{
    struct resource
    {
        unsigned& released;

        ~resource()
        {
            ++released;
        }
    };

    static unsigned reports;
    static bool allocation_error;
    constexpr auto report = [](std::exception_ptr failure)
    {
        ++reports;
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::bad_alloc&)
        {
            allocation_error = true;
        }
    };
    for (const bool compile_time : {false, true})
    {
        for (const bool discard : {false, true})
        {
            for (std::size_t failure = 0; failure != 5; ++failure)
            {
                auto io = cnetmod::make_io_context();
                unsigned released = 0;
                unsigned executed = 0;
                reports = 0;
                allocation_error = false;
                auto work = [&](std::unique_ptr<resource> ownership) -> cnetmod::task<void>
                {
                    (void)ownership;
                    ++executed;
                    co_return;
                };
                auto owned = std::unique_ptr<resource>{new resource{released}};
                auto operation = work(std::move(owned));
                bool escaped = false;
                fail_after = failure;
                try
                {
                    if (compile_time)
                        cnetmod::spawn_guarded<report>(*io, std::move(operation));
                    else
                        cnetmod::spawn_guarded(*io, std::move(operation), report);
                }
                catch (const std::bad_alloc&)
                {
                    escaped = true;
                }
                fail_after.reset();
                if (!discard)
                {
                    io->post([](void* context)
                        {
                            static_cast<cnetmod::io_context*>(context)->stop();
                        },
                        io.get());
                    io->run();
                }
                io.reset();
                ASSERT_EQ(released, 1U);
                ASSERT_TRUE(reports <= 1U);
                ASSERT_FALSE(escaped && reports != 0);
                ASSERT_TRUE(reports == 0 || allocation_error);
                ASSERT_EQ(executed, discard || escaped || reports != 0 ? 0U : 1U);
            }
        }
    }
}

TEST(application_cleanup_retry_allocation_failures_preserve_owned_services)
{
    namespace app = cnetmod::application;

    struct retained_service final : app::managed_service
    {
        bool release = false;
        unsigned starts = 0;
        unsigned releases = 0;

        auto key() const -> app::service_key override
        {
            return {"retained"};
        }

        auto requirement() const noexcept -> app::service_requirement override
        {
            return app::service_requirement::required;
        }

        auto start(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++starts;
            co_return std::expected<void, std::error_code>{};
        }

        auto stop(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            if (!release)
                co_return std::unexpected(std::make_error_code(std::errc::device_or_resource_busy));
            ++releases;
            co_return std::expected<void, std::error_code>{};
        }

        auto probe(app::service_context&) -> cnetmod::task<app::health_report> override
        {
            co_return app::health_report{.status = app::service_health::up};
        }
    };

    cnetmod::net_init network;
    auto occupied = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(occupied.has_value());
    if (!occupied)
        return;
    ASSERT_TRUE(occupied->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(occupied->listen().has_value());
    const auto endpoint = occupied->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    if (!endpoint)
        return;
    unsigned injections = 0;
    for (std::size_t offset = 0; offset != 16; ++offset)
    {
        auto service = std::make_shared<retained_service>();
        auto built = app::application_builder{"retry-allocation"}
                         .configure([&](auto& value)
                             {
                                 value.http.address = "127.0.0.1";
                                 value.http.port = endpoint->port();
                                 value.management.enabled = false;
                                 value.logging.manage_lifecycle = false;
                                 value.install_signal_handlers = false;
                                 value.observability.tracing = false;
                                 value.observability.metrics = false;
                                 value.observability.logs = false;
                                 value.lifecycle.total_stop_timeout = std::chrono::milliseconds{20};
                             })
                         .service(service)
                         .build();
        ASSERT_TRUE(built.has_value());
        if (!built)
            return;
        ASSERT_FALSE(built->run().has_value());
        ASSERT_TRUE(built->state() == app::application_state::cleanup_failed);
        service->release = true;
        fail_after = offset;
        const auto attempted = built->retry_cleanup(std::chrono::milliseconds{100});
        injections += !fail_after;
        fail_after.reset();
        ASSERT_TRUE(built->state() != app::application_state::stopping);
        if (!attempted)
            ASSERT_TRUE(built->retry_cleanup(std::chrono::milliseconds{100}).has_value());
        ASSERT_TRUE(built->state() == app::application_state::stopped);
        ASSERT_EQ(service->starts, 1U);
        ASSERT_EQ(service->releases, 1U);
    }
    ASSERT_TRUE(injections != 0);
}

TEST(application_root_startup_allocation_failure_returns_without_detached_termination)
{
    namespace app = cnetmod::application;
    auto built = app::application_builder{"root-allocation-failure"}
                     .configure([](app::application_configuration& configuration)
                         {
                             configuration.logging.manage_lifecycle = false;
                             configuration.install_signal_handlers = false;
                             configuration.management.enabled = false;
                             configuration.observability.tracing = false;
                             configuration.observability.metrics = false;
                             configuration.observability.logs = false;
                         })
                     .build();
    ASSERT_TRUE(built.has_value());
    fail_after = 0U;
    const auto result = built->run();
    fail_after.reset();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), std::make_error_code(std::errc::not_enough_memory));
    ASSERT_TRUE(built->state() == app::application_state::stopped);
}

TEST(application_orchestration_failure_cancels_a_pending_handler)
{
    namespace app = cnetmod::application;

    struct dependency final : app::managed_service
    {
        bool handler_finished = false;
        bool stopped_after_handler = false;
        bool worker_stopped_after_handler = false;
        bool worker_finished = false;
        unsigned starts = 0;
        unsigned stops = 0;

        auto key() const -> app::service_key override
        {
            return {"request-dependency"};
        }

        auto requirement() const noexcept -> app::service_requirement override
        {
            return app::service_requirement::required;
        }

        auto start(app::service_context& context) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++starts;
            co_return context.supervisor.supervise("request-dependency-worker", [this, io = &context.io](cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    (void)co_await cnetmod::async_timer_wait(*io, std::chrono::hours{1}, token);
                    worker_finished = true;
                    co_return std::expected<void, std::error_code>{};
                },
                {}, true, [this]
                {
                    worker_stopped_after_handler = handler_finished;
                });
        }

        auto stop(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++stops;
            stopped_after_handler = handler_finished;
            co_return std::expected<void, std::error_code>{};
        }

        auto probe(app::service_context&) -> cnetmod::task<app::health_report> override
        {
            co_return app::health_report{.status = app::service_health::up};
        }
    };

    struct injected_wait
    {
        cnetmod::cancel_token& token;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            token.coroutine_ = continuation;
            token.cancel_fn_ = [](cnetmod::cancel_token& value) noexcept
            {
                if (value.pending_.exchange(false))
                    value.coroutine_.resume();
            };
            token.pending_.store(true);
            fail_after = 0U;
        }

        void await_resume() noexcept {}
    };

    cnetmod::net_init network;
    auto reservation = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(reservation.has_value());
    ASSERT_TRUE(reservation->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    const auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint.has_value());
    bool cancelled = false;
    bool fault_consumed = false;
    auto resource = std::make_shared<dependency>();
    auto host = app::application_builder{"active-request-allocation-failure"}
                    .configure([&](auto& configuration)
                        {
                            configuration.http.address = "127.0.0.1";
                            configuration.http.port = endpoint->port();
                            configuration.http.access_logging = false;
                            configuration.management.enabled = false;
                            configuration.install_signal_handlers = false;
                            configuration.logging.manage_lifecycle = false;
                            configuration.observability.tracing = false;
                            configuration.observability.metrics = false;
                            configuration.observability.logs = false;
                            configuration.health.interval = std::chrono::hours{1};
                        })
                    .routes([&](cnetmod::http::router& routes)
                        {
                            routes.get("/pending", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
                                {
                                    co_await injected_wait{request.cancellation_token()};
                                    fault_consumed = !fail_after.has_value();
                                    fail_after.reset();
                                    co_await cnetmod::async_sleep(request.io_ctx(), std::chrono::milliseconds{5});
                                    cancelled = request.cancellation_token().is_cancelled();
                                    resource->handler_finished = true;
                                });
                        })
                    .service(resource)
                    .build();
    ASSERT_TRUE(host.has_value());
    reservation->close();
    std::optional<std::expected<void, std::error_code>> outcome;
    std::jthread runner{[&]
        {
            outcome = host->run();
            fail_after.reset();
        }};
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (host->state() != app::application_state::running && std::chrono::steady_clock::now() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    auto io = cnetmod::make_io_context();
    auto exchange = [&]() -> cnetmod::task<void>
    {
        cnetmod::http::client client{*io};
        (void)co_await client.get(std::format("http://127.0.0.1:{}/pending", endpoint->port()));
        client.close();
        host->request_stop();
        io->stop();
    };
    auto request = exchange();
    request.handle().resume();
    io->run();
    runner.join();
    request.handle().promise().result();
    ASSERT_TRUE(cancelled && fault_consumed);
    ASSERT_EQ(resource->starts, 1U);
    ASSERT_EQ(resource->stops, 1U);
    ASSERT_TRUE(resource->stopped_after_handler);
    ASSERT_TRUE(resource->worker_stopped_after_handler);
    ASSERT_TRUE(resource->worker_finished);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    ASSERT_EQ(outcome->error(), std::make_error_code(std::errc::not_enough_memory));
    ASSERT_TRUE(host->state() == app::application_state::stopped);
}

TEST(lifecycle_records_started_resources_before_post_start_allocation_failure)
{
    namespace app = cnetmod::application;

    struct service final : app::managed_service
    {
        std::string name = std::string(128, 's');
        bool stopped = false;
        unsigned starts = 0;

        auto key() const -> app::service_key override
        {
            return {name};
        }

        auto requirement() const noexcept -> app::service_requirement override
        {
            return app::service_requirement::required;
        }

        auto start(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            fail_after = 0U;
            ++starts;
            co_return std::expected<void, std::error_code>{};
        }

        auto stop(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            stopped = true;
            co_return std::expected<void, std::error_code>{};
        }

        auto probe(app::service_context&) -> cnetmod::task<app::health_report> override
        {
            co_return app::health_report{.status = app::service_health::up};
        }
    };

    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::task_supervisor supervisor{*io};
    app::service_registry services;
    app::health_registry health;
    auto resource = std::make_shared<service>();
    ASSERT_TRUE(services.manage(resource));
    ASSERT_TRUE(health.add(resource));
    services.freeze();
    app::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
    auto run = [&]() -> cnetmod::task<void>
    {
        auto preparation = lifecycle.start();
        fail_after = 0U;
        const auto prepared = co_await std::move(preparation);
        fail_after.reset();
        ASSERT_FALSE(prepared.has_value());
        ASSERT_EQ(prepared.error(), std::make_error_code(std::errc::not_enough_memory));
        ASSERT_EQ(resource->starts, 0U);
        ASSERT_TRUE(lifecycle.started_services().empty());
        const auto started = co_await lifecycle.start();
        fail_after.reset();
        ASSERT_EQ(resource->starts, 1U);
        if (started)
        {
            ASSERT_FALSE(resource->stopped);
            ASSERT_EQ(lifecycle.started_services().size(), 1U);
        }
        else
        {
            ASSERT_EQ(started.error(), std::make_error_code(std::errc::not_enough_memory));
            ASSERT_TRUE(resource->stopped);
            ASSERT_TRUE(lifecycle.started_services().empty());
        }
        ASSERT_TRUE(co_await lifecycle.stop());
        ASSERT_TRUE(resource->stopped);
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    fail_after.reset();
    ASSERT_TRUE(operation.handle().done());
}

TEST(health_refresh_allocation_failures_do_not_preserve_unprobed_up)
{
    namespace app = cnetmod::application;

    struct service final : app::managed_service
    {
        std::string name{"probe"};
        bool probed{};

        auto requirement() const noexcept -> app::service_requirement override
        {
            return app::service_requirement::required;
        }

        auto key() const -> app::service_key override
        {
            return {name};
        }

        auto start(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            co_return std::expected<void, std::error_code>{};
        }

        auto stop(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            co_return std::expected<void, std::error_code>{};
        }

        auto probe(app::service_context&) -> cnetmod::task<app::health_report> override
        {
            probed = true;
            co_return app::health_report{.status = app::service_health::up};
        }
    };

    unsigned invalidated{};
    unsigned mixed{};
    for (std::size_t position = 0; position < 40; ++position)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io, {.export_traces = false, .export_metrics = false, .export_logs = false}};
        app::task_supervisor supervisor{*io};
        cnetmod::cancel_token token;
        app::service_context context{*io, telemetry, supervisor, token, {}};
        app::health_registry health{{.successes_before_up = 1}};
        auto dependency = std::make_shared<service>();
        auto sibling = std::make_shared<service>();
        sibling->name = "sibling";
        ASSERT_TRUE(health.add(dependency));
        ASSERT_TRUE(health.add(sibling));
        health.mark_running();
        health.update(dependency->key(), {.status = app::service_health::up});
        health.update(sibling->key(), {.status = app::service_health::up});
        bool completed{};
        auto run = [&]() -> cnetmod::task<void>
        {
            auto refresh = health.refresh(context);
            fail_after = position;
            bool returned{};
            try
            {
                co_await std::move(refresh);
                returned = true;
            }
            catch (const std::bad_alloc&)
            {}
            fail_after.reset();
            ASSERT_TRUE(returned);
            const auto snapshots = health.snapshots();
            for (const auto& item : {dependency, sibling})
            {
                const auto found = std::ranges::find(snapshots, item->name,
                    [](const auto& snapshot)
                    {
                        return snapshot.key.name;
                    });
                ASSERT_TRUE(found != snapshots.end());
                if (!item->probed)
                {
                    ASSERT_FALSE(health.ready());
                    ASSERT_TRUE(found->report.error);
                    ASSERT_TRUE(found->report.status != app::service_health::up);
                    ++invalidated;
                }
                else
                    ASSERT_TRUE(found->report.status == app::service_health::up);
            }
            if (dependency->probed != sibling->probed)
                ++mixed;
            completed = true;
            io->stop();
        };
        cnetmod::spawn(*io, run());
        io->run();
        ASSERT_TRUE(completed);
    }
    ASSERT_TRUE(invalidated > 0U);
    ASSERT_TRUE(mixed > 0U);
}

TEST(task_group_deadline_allocation_failure_settles_active_children)
{
    for (const bool inject_event_loop : {false, true})
    {
        unsigned injected{};
        for (std::size_t position = 0; position < 20; ++position)
        {
            auto io = cnetmod::make_io_context();
            cnetmod::task_group group{*io, cnetmod::deadline::after(std::chrono::milliseconds{10})};
            bool entered{};
            bool exited{};
            bool completed{};
            ASSERT_TRUE(group.run([&](cnetmod::cancel_token& token)
                                      -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    entered = true;
                    const auto result = co_await cnetmod::async_timer_wait(*io, std::chrono::seconds{5}, token);
                    exited = true;
                    co_return result;
                }));
            io->poll();
            ASSERT_TRUE(entered);
            ASSERT_FALSE(exited);
            auto run = [&]() -> cnetmod::task<void>
            {
                auto joining = group.join();
                fail_after = position;
                const auto result = co_await std::move(joining);
                if (inject_event_loop)
                {
                    if (!fail_after)
                        ++injected;
                    fail_after.reset();
                }
                ASSERT_FALSE(result.has_value());
                ASSERT_TRUE(exited);
                completed = true;
                io->stop();
            };
            const auto began = std::chrono::steady_clock::now();
            if (inject_event_loop)
            {
                cnetmod::spawn(*io, run());
                io->run();
            }
            else
            {
                auto operation = run();
                operation.handle().resume();
                if (!fail_after)
                    ++injected;
                fail_after.reset();
                if (!completed)
                    io->run();
            }
            ASSERT_TRUE(completed);
            ASSERT_TRUE(std::chrono::steady_clock::now() - began < std::chrono::seconds{2});
        }
        ASSERT_TRUE(injected > 0U);
    }
}

TEST(deadline_operation_exception_cancels_watchdog_before_returning)
{
    auto io = cnetmod::make_io_context();
    cnetmod::cancel_token token;
    bool completed{};
    std::error_code error;
    auto throwing = [&]() -> cnetmod::task<std::expected<int, std::error_code>>
    {
        co_await cnetmod::post_awaitable{*io};
        throw std::system_error{std::make_error_code(std::errc::permission_denied)};
        co_return 0;
    };
    auto exercise = [&]() -> cnetmod::task<void>
    {
        try
        {
            (void)co_await cnetmod::with_timeout(*io, std::chrono::seconds{10}, throwing(), token);
        }
        catch (const std::system_error& failure)
        {
            error = failure.code();
        }
        completed = true;
        io->stop();
    };
    const auto started = std::chrono::steady_clock::now();
    cnetmod::spawn(*io, exercise());
    io->run();
    ASSERT_TRUE(completed);
    ASSERT_EQ(error, std::make_error_code(std::errc::permission_denied));
    ASSERT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds{2});
}

TEST(happy_eyeballs_allocation_failures_settle_before_context_destruction)
{
    cnetmod::net_init network;
    unsigned injected_count{};
    unsigned successful_count{};
    for (std::size_t position = 0; position < 64; ++position)
    {
        auto io = cnetmod::make_io_context();
        (void)io->poll();
        auto listener = cnetmod::socket::create(cnetmod::address_family::ipv4,
            cnetmod::socket_type::stream);
        ASSERT_TRUE(listener.has_value());
        ASSERT_TRUE(listener->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
        ASSERT_TRUE(listener->listen().has_value());
        const auto endpoint = listener->local_endpoint();
        ASSERT_TRUE(endpoint.has_value());
        cnetmod::cancel_token token;
        bool completed{};
        bool succeeded{};
        std::error_code error;
        auto connect = [&]() -> cnetmod::task<void>
        {
            try
            {
                const auto result = co_await cnetmod::async_connect_happy_eyeballs(*io,
                    "127.0.0.1", endpoint->port(), {}, token);
                succeeded = result.has_value();
                if (!result)
                    error = result.error();
            }
            catch (const std::bad_alloc&)
            {
                error = std::make_error_code(std::errc::not_enough_memory);
            }
            completed = true;
        };
        auto operation = connect();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        fail_after = position;
        operation.handle().resume();
        while (!completed && std::chrono::steady_clock::now() < deadline)
            (void)io->poll();
        if (!fail_after)
            ++injected_count;
        fail_after.reset();
        ASSERT_TRUE(completed);
        ASSERT_TRUE(operation.handle().done());
        ASSERT_FALSE(token.pending_.load());
        if (succeeded)
            ++successful_count;
        else
            ASSERT_EQ(error, std::make_error_code(std::errc::not_enough_memory));
    }
    ASSERT_TRUE(injected_count >= 10U);
    ASSERT_TRUE(successful_count > 0U);
}

TEST(task_group_startup_allocation_failures_do_not_strand_join)
{
    unsigned injected_count{};
    for (std::size_t allocation = 0; allocation < 10; ++allocation)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::task_group group{*io};
        bool joined{};
        std::function<cnetmod::task<std::expected<void, std::error_code>>(cnetmod::cancel_token&)> child =
            [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return std::expected<void, std::error_code>{};
        };
        fail_after = allocation;
        try
        {
            (void)group.run(std::move(child));
        }
        catch (const std::bad_alloc&)
        {
        }
        if (!fail_after)
            ++injected_count;
        fail_after.reset();
        auto wait = [&]() -> cnetmod::task<void>
        {
            (void)co_await group.join();
            joined = true;
            io->stop();
        };
        cnetmod::spawn(*io, wait());
        io->run();
        ASSERT_TRUE(joined);
    }
    ASSERT_TRUE(injected_count >= 3U);
}

TEST(exporter_flush_settles_cancelled_worker_start_failure_without_rescheduling)
{
    auto io = cnetmod::make_io_context();
    (void)io->poll();
    cnetmod::observability::otlp_http_exporter exporter{*io,
        {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
    ASSERT_TRUE(exporter.submit(cnetmod::observability::otel_metric_record{.name = "queued", .value = 1}));
    exporter.abort();
    fail_after = 0;
    (void)io->poll();
    const bool startup_failed = !fail_after;
    fail_after.reset();
    bool settled{};
    bool allocated{};
    auto exercise = [&]() -> cnetmod::task<void>
    {
        auto operation = exporter.flush(std::chrono::milliseconds{0});
        fail_after = 0;
        const auto result = co_await std::move(operation);
        allocated = !fail_after;
        fail_after.reset();
        settled = result.has_value();
        io->stop();
    };
    cnetmod::spawn(*io, exercise());
    io->run();
    ASSERT_TRUE(startup_failed && settled);
    ASSERT_FALSE(allocated);
    ASSERT_EQ(exporter.statistics().worker_failures, 1U);
    ASSERT_EQ(exporter.statistics().dropped_metrics, 1U);
}

TEST(exporter_worker_start_contains_coroutine_and_nested_post_allocation_failures)
{
    for (std::size_t successful_allocations : {0U, 1U, 2U, 3U})
    {
        auto io = cnetmod::make_io_context();
        (void)io->poll();
        cnetmod::observability::otlp_http_exporter exporter{*io,
            {.metrics_endpoint = "http://127.0.0.1:1/v1/metrics"}};
        ASSERT_TRUE(exporter.submit(cnetmod::observability::otel_metric_record{
            .name = "fault.test",
            .value = 1}));
        fail_after = successful_allocations;
        (void)io->poll();
        const bool injected = !fail_after;
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_EQ(exporter.statistics().worker_failures, 1U);
        ASSERT_TRUE(exporter.submit(cnetmod::observability::otel_metric_record{
            .name = "after.failure",
            .value = 1}));
        ASSERT_EQ(exporter.statistics().accepted_metrics, 2U);
        exporter.close();
    }
}

TEST(disabled_http_server_instrumentation_registers_without_allocation)
{
    auto io = cnetmod::make_io_context();
    cnetmod::http::server server{*io};
    bool all_empty = true;
    const auto totals = measure([&]
        {
            auto tracing = cnetmod::http::tracing::tracing_middleware();
            all_empty = all_empty && !tracing;
            server.use(std::move(tracing));
            server.use(cnetmod::observability::server_metrics({}));
        });
    ASSERT_TRUE(all_empty);
    ASSERT_EQ(totals.calls, std::size_t{0});
    ASSERT_EQ(totals.bytes, std::size_t{0});
}

TEST(http_client_escaping_allocation_errors_are_not_reported_as_abandonment)
{
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    const cnetmod::http::request request{cnetmod::http::http_method::GET, "invalid-uri"};
    const auto parent = cnetmod::http::tracing::new_root_context();
    unsigned verified = 0;
    for (std::size_t allocation = 0; allocation != 64; ++allocation)
    {
        unsigned spans = 0;
        auto outcome = cnetmod::instrumentation::operation_status::success;
        std::string failure;
        cnetmod::observability::instrumented_http_client client{raw,
            [&](const cnetmod::http::tracing::completed_span& span)
            {
                ++spans;
                outcome = span.result.status;
            },
            [&](auto metric)
            {
                for (const auto& [key, value] : metric.attributes)
                    if (key == "error.type")
                        failure = value;
            }};
        auto work = client.send(request, parent);
        bool escaped = false;
        auto run = [&]() -> cnetmod::task<void>
        {
            fail_after = allocation;
            try
            {
                (void)co_await work;
            }
            catch (const std::bad_alloc&)
            {
                escaped = true;
            }
            fail_after.reset();
        };
        cnetmod::sync_wait(run());
        if (escaped && spans == 1)
        {
            ++verified;
            ASSERT_TRUE(outcome == cnetmod::instrumentation::operation_status::error);
            ASSERT_EQ(failure, "exception");
        }
    }
    ASSERT_TRUE(verified > 0);
}

TEST(histogram_initialization_remains_usable_after_allocation_failure)
{
    unsigned injected = 0;
    for (std::size_t allocation = 0; allocation != 32; ++allocation)
    {
        cnetmod::metrics::registry metrics;
        auto buckets = std::vector<double>{0.1, 1.0};
        fail_after = allocation;
        try
        {
            metrics.histogram_observe("fault_duration", 0.5, std::move(buckets));
        }
        catch (const std::bad_alloc&)
        {
            ++injected;
        }
        fail_after.reset();
        metrics.histogram_observe("fault_duration", 0.5, {0.1, 1.0});
        ASSERT_TRUE(metrics.render_openmetrics().contains("fault_duration_count"));
    }
    ASSERT_TRUE(injected > 0);
}

TEST(http_trace_preparation_failures_preserve_route_execution_and_exceptions)
{
    struct route_failure
    {
    };

    unsigned injected = 0;
    for (const bool throw_route : {false, true})
    {
        for (std::size_t allocation = 0; allocation != 48; ++allocation)
        {
            auto io = cnetmod::make_io_context();
            cnetmod::socket socket;
            cnetmod::http::response response;
            cnetmod::http::header_map headers{
                {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
                {"tracestate", "vendor=a-long-upstream-state-value"}};
            cnetmod::http::request_context context{*io, socket, "GET", "/observed", headers, {}, response, {}};
            unsigned routes = 0;
            unsigned reports = 0;
            auto handler = [&]() -> cnetmod::task<void>
            {
                ++routes;
                if (throw_route)
                    throw route_failure{};
                co_return;
            };
            auto route = handler();
            auto middleware = cnetmod::http::tracing::tracing_middleware({.on_end = [&](const cnetmod::http::tracing::completed_span&)
                {
                    ++reports;
                    throw route_failure{};
                }});
            auto operation = middleware(context, [&]
                {
                    return std::move(route);
                });
            bool original_exception = false;
            bool unexpected_exception = false;
            auto run = [&](cnetmod::task<void> work) -> cnetmod::task<void>
            {
                fail_after = allocation;
                try
                {
                    co_await work;
                }
                catch (const route_failure&)
                {
                    original_exception = true;
                }
                catch (...)
                {
                    unexpected_exception = true;
                }
                if (!fail_after)
                    ++injected;
                fail_after.reset();
            };
            cnetmod::sync_wait(run(std::move(operation)));
            ASSERT_EQ(routes, 1U);
            ASSERT_TRUE(reports <= 1U);
            ASSERT_EQ(original_exception, throw_route);
            ASSERT_FALSE(unexpected_exception);
        }
    }
    ASSERT_TRUE(injected > 0);
}

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
TEST(disabled_openai_lazy_completion_allocates_nothing)
{
    cnetmod::openai::run_config config;
    unsigned factories = 0;
    const auto totals = measure([&]
        {
            auto scope = cnetmod::openai::run_scope::start_lazy(config,
                cnetmod::openai::run_event_type::model_start,
                cnetmod::openai::run_event_type::model_end,
                cnetmod::openai::run_event_type::model_error,
                "a-model-name-longer-than-small-string-storage",
                [&]() -> cnetmod::openai::json
                {
                    ++factories;
                    throw std::bad_alloc{};
                });
            scope.fail_lazy([&]() -> cnetmod::openai::json
                {
                    ++factories;
                    throw std::bad_alloc{};
                },
                "a-provider-error-detail-longer-than-small-string-storage");
        });
    ASSERT_EQ(factories, 0U);
    ASSERT_EQ(totals.calls, std::size_t{0});
    ASSERT_EQ(totals.bytes, std::size_t{0});
}

TEST(openai_complete_event_dispatch_does_not_copy_or_allocate)
{
    unsigned callbacks = 0;
    bool borrowed = true;
    cnetmod::openai::run_event event{
        .run_id = "a-complete-long-run-identity",
        .attributes = {{"nested", {{"payload", "a-long-preexisting-string-payload"}}}}};
    cnetmod::openai::run_config config{
        .callback = [&](const cnetmod::openai::run_event& received)
        {
            borrowed = borrowed && &received == &event;
            ++callbacks;
        }};
    const auto totals = measure([&]
        {
            config.notify(event);
        });
    ASSERT_EQ(callbacks, 256U);
    ASSERT_TRUE(borrowed);
    ASSERT_EQ(totals.calls, std::size_t{0});
    ASSERT_EQ(totals.bytes, std::size_t{0});
}

TEST(disabled_openai_instant_notification_allocates_nothing)
{
    cnetmod::openai::run_config config{.listeners = {nullptr}};
    unsigned factories = 0;
    const auto totals = measure([&]
        {
            config.notify_lazy([&]() -> cnetmod::openai::run_event
                {
                    ++factories;
                    throw std::bad_alloc{};
                });
        });
    ASSERT_EQ(factories, 0U);
    ASSERT_EQ(totals.calls, std::size_t{0});
    ASSERT_EQ(totals.bytes, std::size_t{0});
}

TEST(openai_listener_without_outputs_skips_operation_tracking)
{
    cnetmod::openai::telemetry_listener listener{
        cnetmod::instrumentation::metric_sink{}, cnetmod::http::tracing::span_exporter{}};
    cnetmod::openai::run_event event{
        .run_id = "a-long-private-run-identifier-for-disabled-observation",
        .name = "a-long-model-name-for-disabled-observation"};
    bool threw = false;
    allocation_totals totals;
    fail_after = 0U;
    try
    {
        allocation_window window{totals};
        for (const auto type : {cnetmod::openai::run_event_type::model_start,
                 cnetmod::openai::run_event_type::model_end,
                 cnetmod::openai::run_event_type::model_error,
                 cnetmod::openai::run_event_type::model_retry})
        {
            event.type = type;
            listener.on_event(event);
        }
    }
    catch (...)
    {
        threw = true;
    }
    const bool unused_failure = fail_after.has_value();
    fail_after.reset();
    ASSERT_FALSE(threw);
    ASSERT_TRUE(unused_failure);
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
    ASSERT_EQ(listener.statistics().started, std::uint64_t{0});
    ASSERT_EQ(listener.statistics().completed, std::uint64_t{0});
    ASSERT_EQ(listener.statistics().unmatched_end_events, std::uint64_t{0});
}

TEST(openai_event_enrichment_failure_delivers_original_once)
{
    cnetmod::openai::run_event event;
    unsigned calls = 0;
    bool borrowed = false;
    cnetmod::openai::run_config config{
        .run_id = "a-default-run-identity-requiring-an-allocation",
        .callback = [&](const cnetmod::openai::run_event& received)
        {
            borrowed = &received == &event;
            ++calls;
        }};
    fail_after = 0;
    config.notify(event);
    const bool injected = !fail_after;
    fail_after.reset();
    ASSERT_TRUE(injected);
    ASSERT_EQ(calls, 1U);
    ASSERT_TRUE(borrowed);
    ASSERT_TRUE(event.run_id.empty());
}

TEST(openai_metric_allocation_failures_do_not_suppress_completed_spans)
{
    unsigned verified_failures = 0;
    for (std::size_t allocation = 0; allocation != 96; ++allocation)
    {
        cnetmod::metrics::registry metrics;
        unsigned exported = 0;
        cnetmod::openai::telemetry_listener listener{metrics,
            [&](const cnetmod::http::tracing::completed_span&)
            {
                ++exported;
            }};
        listener.on_event({.type = cnetmod::openai::run_event_type::model_start});
        cnetmod::openai::run_event end{.type = cnetmod::openai::run_event_type::model_end};
        fail_after = allocation;
        try
        {
            listener.on_event(end);
        }
        catch (const std::bad_alloc&)
        {
            // Earlier lifecycle bookkeeping is outside this signal-isolation test.
        }
        fail_after.reset();
        if (listener.statistics().metric_failures != 0)
        {
            ++verified_failures;
            ASSERT_EQ(exported, 1U);
            ASSERT_EQ(listener.statistics().completed, 1U);
            ASSERT_EQ(listener.statistics().dropped_spans, 0U);
            listener.on_event({.type = cnetmod::openai::run_event_type::model_start});
            listener.on_event(end);
            ASSERT_EQ(exported, 2U);
        }
    }
    ASSERT_TRUE(verified_failures > 0);
}

TEST(openai_scope_initialization_and_abandonment_contain_allocation_failure)
{
    unsigned injected = 0;
    for (std::size_t allocation = 0; allocation != 48; ++allocation)
    {
        unsigned starts = 0;
        unsigned ends = 0;
        cnetmod::openai::run_config config{
            .run_id = "a-long-run-identity-for-allocation-testing",
            .callback = [&](const cnetmod::openai::run_event& event)
            {
                if (event.type == cnetmod::openai::run_event_type::model_start)
                    ++starts;
                else
                    ++ends;
            }};
        fail_after = allocation;
        cnetmod::openai::run_scope scope{config,
            cnetmod::openai::run_event_type::model_start,
            cnetmod::openai::run_event_type::model_end,
            cnetmod::openai::run_event_type::model_error, "long-model-name-requiring-allocation"};
        if (!fail_after)
            ++injected;
        fail_after.reset();
        scope.succeed();
        ASSERT_EQ(starts, ends);
        ASSERT_EQ(scope.child_config().run_id, config.run_id);
    }
    ASSERT_TRUE(injected > 0);
    cnetmod::openai::run_config config{.callback = [](const cnetmod::openai::run_event&) {}};
    {
        cnetmod::openai::run_scope scope{config,
            cnetmod::openai::run_event_type::model_start,
            cnetmod::openai::run_event_type::model_end,
            cnetmod::openai::run_event_type::model_error, "model"};
        fail_after = 0;
    }
    const bool destructor_fault = !fail_after;
    fail_after.reset();
    ASSERT_TRUE(destructor_fault);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
TEST(disabled_grpc_unary_matches_original_task_frame_and_status)
{
    auto io = cnetmod::make_io_context();
    cnetmod::grpc::client raw{*io, "invalid://grpc-observation"};
    cnetmod::observability::instrumented_grpc_client disabled{raw};
    const auto parent = cnetmod::instrumentation::new_root_context();
    auto dispatch = [&](bool observed)
    {
        cnetmod::grpc::unary_request request{
            .service = "example.observation.Service",
            .method = "DisabledPath",
            .headers = {{"private-header", std::string(4096, 'x')}},
            .payload = cnetmod::grpc::byte_buffer(4096, std::byte{0x32}),
        };
        return observed ? disabled.unary(std::move(request), parent)
                        : raw.unary(std::move(request));
    };

    const auto expected = cnetmod::sync_wait(dispatch(false));
    const auto actual = cnetmod::sync_wait(dispatch(true));
    ASSERT_FALSE(expected.has_value());
    ASSERT_FALSE(actual.has_value());
    ASSERT_EQ(static_cast<int>(actual.error().code), static_cast<int>(expected.error().code));

    const auto baseline = measure([&]
        {
            auto operation = dispatch(false);
            if (!operation.handle())
                throw std::runtime_error("raw gRPC task has no frame");
        });
    const auto decorated = measure([&]
        {
            auto operation = dispatch(true);
            if (!operation.handle())
                throw std::runtime_error("disabled gRPC task has no frame");
        });
    ASSERT_EQ(decorated.calls, baseline.calls);
    ASSERT_EQ(decorated.bytes, baseline.bytes);
}

TEST(grpc_observation_exports_and_propagates_the_exported_client_span)
{
    auto io = cnetmod::make_io_context();
    cnetmod::grpc::metadata intercepted;
    cnetmod::grpc::client_options options;
    options.request_interceptors.push_back([&](cnetmod::grpc::client_call& call)
                                               -> std::expected<void, cnetmod::grpc::status>
        {
            intercepted = call.headers;
            return {};
        });
    cnetmod::grpc::client raw{*io, "invalid://grpc-observation", std::move(options)};
    std::vector<cnetmod::instrumentation::completed_span> spans;
    std::vector<cnetmod::instrumentation::metric_measurement> metrics;
    cnetmod::observability::instrumented_grpc_client observed{raw,
        [&](const cnetmod::instrumentation::completed_span& span)
        {
            spans.push_back(span);
        },
        [&](cnetmod::instrumentation::metric_measurement measurement)
        {
            metrics.push_back(std::move(measurement));
        }};
    const auto parent = cnetmod::instrumentation::new_root_context();
    const auto result = cnetmod::sync_wait(observed.unary({
                                                              .service = "example.observation.Service",
                                                              .method = "ObservedPath",
                                                          },
        parent));

    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(spans.size(), std::size_t{1});
    ASSERT_EQ(metrics.size(), std::size_t{1});
    ASSERT_EQ(spans.front().parent_span_id, parent.span_id);
    ASSERT_EQ(intercepted.count("traceparent"), std::size_t{1});
    ASSERT_EQ(intercepted.find("traceparent")->second,
        cnetmod::instrumentation::format_traceparent(spans.front().context));
    ASSERT_EQ(metrics.front().name, std::string("rpc.client.duration"));
    ASSERT_EQ(metrics.front().attributes.at(0).first, std::string("rpc.system"));
    ASSERT_EQ(metrics.front().attributes.at(0).second, std::string("grpc"));
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
TEST(disabled_redis_entry_points_match_original_allocations_and_results)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::redis::client client{*io};
    cnetmod::http::tracing::trace_context parent;
    std::vector<std::string> arguments{"GET", "long-private-key-for-allocation-regression"};
    const std::initializer_list<std::string_view> argument_views{
        "GET", "long-private-key-for-allocation-regression"};
    const std::initializer_list<std::initializer_list<std::string_view>> pipeline_views{
        {"GET", "first-private-key"}, {"GET", "second-private-key"}};
    const std::vector<std::vector<std::string>> pipeline{
        {"GET", "first-private-key"}, {"GET", "second-private-key"}};
    cnetmod::redis::request request;
    request.push("GET", "long-private-key-for-allocation-regression");
    for (unsigned entry = 0; entry < 5; ++entry)
    {
        auto dispatch = [&](bool observed)
            -> cnetmod::task<std::expected<std::vector<cnetmod::redis::resp3_node>, std::string>>
        {
            switch (entry)
            {
            case 0:
                return observed ? client.cmd(arguments, parent, {}) : client.cmd(arguments);
            case 1:
                return observed ? client.cmd(argument_views, parent, {}) : client.cmd(argument_views);
            case 2:
                return observed ? client.exec(request, parent, {}) : client.exec(request);
            case 3:
                return observed ? client.pipe(pipeline, parent, {}) : client.pipe(pipeline);
            default:
                return observed ? client.pipe(pipeline_views, parent, {}) : client.pipe(pipeline_views);
            }
        };
        const auto baseline_frames = measure([&]
            {
                auto pending = dispatch(false);
            });
        const auto disabled_frames = measure([&]
            {
                auto pending = dispatch(true);
            });
        ASSERT_TRUE(baseline_frames.calls > 0);
        ASSERT_EQ(disabled_frames.calls, baseline_frames.calls);
        ASSERT_EQ(disabled_frames.bytes, baseline_frames.bytes);
        const auto expected = cnetmod::sync_wait(dispatch(false));
        ASSERT_FALSE(expected.has_value());
        (void)cnetmod::sync_wait(dispatch(true));
        bool preserved = true;
        auto execute = [&](bool observed)
        {
            const auto result = cnetmod::sync_wait(dispatch(observed));
            preserved = preserved && !result && result.error() == expected.error();
        };
        const auto baseline = measure([&]
            {
                execute(false);
            });
        const auto disabled = measure([&]
            {
                execute(true);
            });
        ASSERT_TRUE(preserved);
        ASSERT_EQ(disabled.calls, baseline.calls);
        ASSERT_EQ(disabled.bytes, baseline.bytes);
    }
}

TEST(redis_exporter_copy_failure_preserves_all_entry_point_results)
{
    struct throwing_sink
    {
        bool* fail;

        explicit throwing_sink(bool& value) : fail(&value) {}

        throwing_sink(const throwing_sink& other) : fail(other.fail)
        {
            if (*fail)
                throw std::bad_alloc{};
        }

        void operator()(const cnetmod::instrumentation::completed_span&) const {}
    };

    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::redis::client client{*io};
    bool fail = false;
    cnetmod::instrumentation::span_exporter sink{throwing_sink{fail}};
    std::vector<std::string> arguments{"GET", "key"};
    const std::initializer_list<std::string_view> views{"GET", "key"};
    const std::vector<std::vector<std::string>> pipeline{arguments};
    const std::initializer_list<std::initializer_list<std::string_view>> batches{{"GET", "key"}};
    cnetmod::redis::request request;
    request.push("GET", "key");
    for (unsigned entry = 0; entry < 5; ++entry)
    {
        auto dispatch = [&](bool observed)
            -> cnetmod::task<std::expected<std::vector<cnetmod::redis::resp3_node>, std::string>>
        {
            switch (entry)
            {
            case 0:
                return observed ? client.cmd(arguments, {}, sink) : client.cmd(arguments);
            case 1:
                return observed ? client.cmd(views, {}, sink) : client.cmd(views);
            case 2:
                return observed ? client.exec(request, {}, sink) : client.exec(request);
            case 3:
                return observed ? client.pipe(pipeline, {}, sink) : client.pipe(pipeline);
            default:
                return observed ? client.pipe(batches, {}, sink) : client.pipe(batches);
            }
        };
        auto baseline = cnetmod::sync_wait(dispatch(false));
        fail = true;
        auto result = cnetmod::sync_wait(dispatch(true));
        fail = false;
        ASSERT_FALSE(baseline.has_value());
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), baseline.error());
    }
}

TEST(redis_unsampled_operations_skip_attribute_allocations)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::redis::client client{*io};
    std::vector<std::string> args{"GET", "private-key"};
    cnetmod::redis::request batch;
    batch.push("GET", "private-key");
    auto parent = cnetmod::http::tracing::new_root_context();
    unsigned exported = 0;
    cnetmod::instrumentation::span_exporter sink = [&](const auto&)
    {
        ++exported;
    };
    for (bool pipeline : {false, true})
    {
        auto execute = [&]
        {
            auto operation = pipeline ? client.exec(batch, parent, sink) : client.cmd(args, parent, sink);
            auto result = cnetmod::sync_wait(std::move(operation));
            if (result)
                throw std::logic_error("disconnected client unexpectedly succeeded");
        };
        parent.flags = 1;
        execute();
        exported = 0;
        const auto sampled = measure(execute);
        ASSERT_EQ(exported, 256U);
        parent.flags = 0;
        execute();
        exported = 0;
        const auto unsampled = measure(execute);
        ASSERT_EQ(exported, 0U);
        ASSERT_TRUE(unsampled.calls < sampled.calls);
        ASSERT_TRUE(unsampled.bytes < sampled.bytes);
    }
}

TEST(redis_observation_preserves_allocation_exceptions_as_errors)
{
    auto io = cnetmod::make_io_context();
    for (const bool pipeline : {false, true})
    {
        unsigned verified{};
        for (std::size_t allocation = 0; allocation < 64; ++allocation)
        {
            cnetmod::redis::client client{*io};
            std::vector<std::string> args{"GET", "private-key"};
            cnetmod::redis::request batch;
            batch.push("GET", "private-key");
            unsigned reports{};
            cnetmod::instrumentation::operation_status outcome{};
            cnetmod::http::tracing::span_exporter sink = [&](const auto& span)
            {
                ++reports;
                outcome = span.result.status;
                throw std::runtime_error("export failure");
            };
            auto pending = pipeline ? client.exec(batch, {}, sink) : client.cmd(args, {}, sink);
            bool escaped{};
            fail_after = allocation;
            try
            {
                (void)cnetmod::sync_wait(std::move(pending));
            }
            catch (const std::bad_alloc&)
            {
                escaped = true;
            }
            fail_after.reset();
            if (escaped && reports != 0)
            {
                ++verified;
                ASSERT_EQ(reports, 1U);
                ASSERT_TRUE(outcome == cnetmod::instrumentation::operation_status::error);
            }
        }
        ASSERT_TRUE(verified > 0);
    }
}

    #include "redis_return_contention_cases.inc"

TEST(redis_pool_startup_allocation_failures_release_maintenance_ownership)
{
    unsigned propagated = 0;
    for (std::size_t allocation = 0; allocation < 12; ++allocation)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        cnetmod::redis::connection_pool pool{*io, {.initial_size = 1, .max_size = 1, .ping_interval = std::chrono::hours{1}}};
        auto run = pool.async_run();
        fail_after = allocation;
        run.handle().resume();
        fail_after.reset();
        // Cancel before dispatching queued maintenance, so this test does not
        // require a broker and isolates startup/dispatch allocation ownership.
        auto stop = pool.cancel();
        stop.handle().resume();
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while ((!run.handle().done() || !stop.handle().done()) &&
            std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        ASSERT_TRUE(run.handle().done());
        ASSERT_TRUE(stop.handle().done());
        ASSERT_EQ(pool.pending_maintenance(), 0);
        if (!run.handle().done() || !stop.handle().done())
            std::terminate(); // Never destroy a frame still referenced by I/O.
        stop.handle().promise().result();
        try
        {
            run.handle().promise().result();
        }
        catch (const std::bad_alloc&)
        {
            ++propagated;
        }
    }
    ASSERT_TRUE(propagated > 0U);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
    #include "mongodb_close_allocation_cases.inc"
    #include "mongodb_return_allocation_cases.inc"
    #include "mongodb_ping_allocation_cases.inc"
    #include "mongodb_startup_allocation_cases.inc"
#endif

TEST(supervisor_stop_dispatch_does_not_allocate_and_allows_reentrant_queries)
{
    auto io = cnetmod::make_io_context();
    cnetmod::application::task_supervisor supervisor{*io};
    unsigned cancelled = 0, callbacks = 0;
    bool query_valid = true;
    const std::string prefix(128, 'n');
    const auto known_name = prefix + "0";
    const auto missing_name = prefix + "missing";
    for (unsigned index = 0; index < 32; ++index)
    {
        auto added = supervisor.supervise(prefix + std::to_string(index), [&](cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
            {
                if (token.is_cancelled())
                    ++cancelled;
                co_return {};
            },
            {}, true, [&]() noexcept
            {
                ++callbacks;
                query_valid = query_valid && supervisor.state(known_name).has_value() &&
                    !supervisor.state(missing_name).has_value() &&
                    !supervisor.last_error(known_name) && !supervisor.last_error(missing_name);
                supervisor.request_stop();
            });
        ASSERT_TRUE(added.has_value());
    }
    allocation_totals totals;
    fail_after = 0;
    {
        allocation_window window{totals};
        supervisor.request_stop();
        supervisor.request_stop();
    }
    const bool allocation_unused = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(allocation_unused);
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(callbacks, 32U);
    ASSERT_TRUE(query_valid);
    auto wait = [&]() -> cnetmod::task<void>
    {
        (void)co_await supervisor.join();
        io->stop();
    };
    auto joining = wait();
    joining.handle().resume();
    io->run();
    ASSERT_EQ(cancelled, 32U);
    ASSERT_TRUE(joining.handle().done());
}

TEST(supervisor_stop_allocation_exceptions_are_recorded_without_allocating)
{
    auto io = cnetmod::make_io_context();
    cnetmod::application::task_supervisor supervisor{*io};
    unsigned callbacks = 0;
    for (const auto name : {"first", "second"})
        ASSERT_TRUE(supervisor.supervise(name, [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
            {
                co_return std::expected<void, std::error_code>{};
            },
            {}, true, [&]
            {
                ++callbacks;
                throw std::bad_alloc{};
            }));
    allocation_totals totals;
    fail_after = 0U;
    {
        allocation_window window{totals};
        supervisor.request_stop();
    }
    const bool no_allocation_attempted = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(no_allocation_attempted);
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
    ASSERT_EQ(callbacks, 2U);
    auto run = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await supervisor.join();
        ASSERT_FALSE(result.has_value());
        ASSERT_EQ(result.error(), std::make_error_code(std::errc::not_enough_memory));
        io->stop();
    };
    auto operation = run();
    operation.handle().resume();
    io->run();
    ASSERT_TRUE(operation.handle().done());
}

TEST(supervisor_retry_logging_failure_does_not_exhaust_recovery)
{
    auto io = cnetmod::make_io_context();
    cnetmod::application::task_supervisor supervisor{*io};
    unsigned attempts = 0;
    const auto registered = supervisor.supervise("retry-log-allocation",
        [&](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
        {
            if (++attempts == 1)
            {
                fail_after = 0U;
                co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
            }
            co_return {};
        },
        {.initial_delay = std::chrono::milliseconds{1},
            .maximum_delay = std::chrono::milliseconds{1},
            .budget = std::chrono::seconds{1},
            .jitter = 0.0});
    ASSERT_TRUE(registered.has_value());
    auto wait = [&]() -> cnetmod::task<void>
    {
        const auto result = co_await supervisor.join();
        const bool injected = !fail_after.has_value();
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_TRUE(result.has_value());
        io->stop();
    };
    auto joining = wait();
    joining.handle().resume();
    io->run();
    fail_after.reset();
    ASSERT_TRUE(joining.handle().done());
    joining.handle().promise().result();
    ASSERT_EQ(attempts, 2U);
    ASSERT_TRUE(supervisor.state("retry-log-allocation") ==
        cnetmod::application::supervised_task_state::stopped);
}

TEST(supervisor_registration_allocation_failures_do_not_leak_completion)
{
    unsigned injected = 0;
    for (std::size_t allocation = 0; allocation < 20; ++allocation)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::application::task_supervisor supervisor{*io};
        fail_after = allocation;
        try
        {
            (void)supervisor.supervise("allocation-task",
                [](cnetmod::cancel_token&) -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    co_return {};
                });
        }
        catch (const std::bad_alloc&)
        {
        }
        if (!fail_after)
            ++injected;
        fail_after.reset();
        supervisor.request_stop();
        auto joining = supervisor.join();
        joining.handle().resume();
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!joining.handle().done() && std::chrono::steady_clock::now() < limit)
            (void)io->poll();
        ASSERT_TRUE(joining.handle().done());
        if (!joining.handle().done())
            std::terminate();
        const auto result = joining.handle().promise().result();
        const auto state = supervisor.state("allocation-task");
        if (state == cnetmod::application::supervised_task_state::failed)
        {
            ASSERT_FALSE(result.has_value());
            ASSERT_EQ(result.error(), supervisor.last_error("allocation-task"));
        }
        else
            ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(!state || *state == cnetmod::application::supervised_task_state::failed ||
            *state == cnetmod::application::supervised_task_state::stopped);
    }
    ASSERT_TRUE(injected > 0U);
}

#ifdef CNETMOD_TEST_ORM
namespace {
struct allocation_database_client
{
    bool reject = false;

    auto result() -> cnetmod::orm::query_result
    {
        cnetmod::orm::query_result value;
        if (reject)
        {
            value.error_code = 1054;
            value.sql_state = "42S22";
            value.error_msg = "private database diagnostic exceeding small-string storage";
        }
        return value;
    }

    auto query(std::string_view) -> cnetmod::task<cnetmod::orm::query_result>
    {
        co_return result();
    }

    auto execute(std::string_view sql) -> cnetmod::task<cnetmod::orm::query_result>
    {
        return query(sql);
    }

    auto execute(cnetmod::orm::parameterized_query) -> cnetmod::task<cnetmod::orm::query_result>
    {
        co_return result();
    }
};
} // namespace

TEST(disabled_orm_observation_matches_original_frames_and_allocations)
{
    allocation_database_client client;
    cnetmod::orm::database_session session{client};
    const auto parent = cnetmod::http::tracing::new_root_context();
    const std::string sql(4096, 'x');
    for (const bool rejected : {false, true})
    {
        client.reject = rejected;
        for (unsigned operation : {0U, 1U, 2U})
        {
            auto dispatch = [&](bool observed)
            {
                if (operation == 2U)
                {
                    auto statement = cnetmod::orm::with_params(sql,
                        {cnetmod::orm::param_value::from_string("private-binding")});
                    return observed ? session.execute(std::move(statement), parent, {}, {.capture_query_text = true})
                                    : session.execute(std::move(statement));
                }
                if (operation == 1U)
                    return observed ? session.execute(sql, parent, {}, {.capture_query_text = true})
                                    : session.execute(sql);
                return observed ? session.query(sql, parent, {}, {.capture_query_text = true})
                                : session.query(sql);
            };
            auto baseline = measure([&]
                {
                    auto pending = dispatch(false);
                });
            auto disabled = measure([&]
                {
                    auto pending = dispatch(true);
                });
            ASSERT_EQ(disabled.calls, baseline.calls);
            ASSERT_EQ(disabled.bytes, baseline.bytes);
            bool preserved = true;
            auto run = [&](bool observed)
            {
                const auto result = cnetmod::sync_wait(dispatch(observed));
                preserved = result.ok() == !rejected && preserved;
                if (rejected)
                    preserved = preserved && result.error_code == 1054 && result.sql_state == "42S22" &&
                        result.error_msg == "private database diagnostic exceeding small-string storage";
            };
            run(false);
            run(true);
            baseline = measure([&]
                {
                    run(false);
                });
            disabled = measure([&]
                {
                    run(true);
                });
            ASSERT_TRUE(preserved);
            ASSERT_EQ(disabled.calls, baseline.calls);
            ASSERT_EQ(disabled.bytes, baseline.bytes);
        }
    }
}

TEST(orm_context_copy_failure_falls_back_to_database_execution)
{
    allocation_database_client client;
    cnetmod::orm::database_session session{client};
    const auto parent = cnetmod::http::tracing::new_root_context();
    unsigned exports = 0;
    const cnetmod::instrumentation::span_exporter sink{[&](const auto&)
        {
            ++exports;
        }};
    for (const unsigned operation : {0U, 1U, 2U})
    {
        auto statement = cnetmod::orm::with_params("SELECT ?",
            {cnetmod::orm::param_value::from_string("private-binding")});
        fail_after = 0U;
        auto pending = operation == 0U ? session.query("SELECT 1", parent, sink)
            : operation == 1U          ? session.execute("SELECT 1", parent, sink)
                                       : session.execute(std::move(statement), parent, sink);
        const bool injected = !fail_after;
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_TRUE(cnetmod::sync_wait(std::move(pending)).ok());
    }
    ASSERT_EQ(exports, 0U);
    ASSERT_TRUE(cnetmod::sync_wait(session.query("SELECT 1", parent, sink)).ok());
    ASSERT_EQ(exports, 1U);
}

TEST(orm_error_annotation_allocation_failure_preserves_database_result)
{
    struct rejected_client
    {
        unsigned calls{};

        auto query(std::string_view) -> cnetmod::task<cnetmod::orm::query_result>
        {
            ++calls;
            cnetmod::orm::query_result result{
                .error_msg = "private database diagnostic",
                .sql_state = "42S22",
                .error_code = 1054};
            fail_after = 0U;
            co_return result;
        }

        auto execute(std::string_view sql) -> cnetmod::task<cnetmod::orm::query_result>
        {
            return query(sql);
        }

        auto execute(cnetmod::orm::parameterized_query) -> cnetmod::task<cnetmod::orm::query_result>
        {
            return query("");
        }
    };

    for (const auto dialect : {cnetmod::orm::sql_dialect::mysql, cnetmod::orm::sql_dialect::postgresql})
    {
        rejected_client client;
        cnetmod::orm::database_session session{client, dialect};
        unsigned exports{};
        bool failed{};
        const cnetmod::instrumentation::span_exporter sink{[&](const auto& span)
            {
                ++exports;
                failed = span.failed;
            }};
        const auto result = cnetmod::sync_wait(session.query("SELECT private", {}, sink));
        const bool injected = !fail_after;
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_EQ(client.calls, 1U);
        ASSERT_EQ(result.error_code, 1054U);
        ASSERT_EQ(result.sql_state, "42S22");
        ASSERT_EQ(result.error_msg, "private database diagnostic");
        ASSERT_EQ(exports, 1U);
        ASSERT_TRUE(failed);
    }
}

TEST(orm_observation_frame_failure_preserves_owned_bindings)
{
    struct recording_client
    {
        std::string received_sql;
        std::string received_binding;
        unsigned calls = 0;

        auto query(std::string_view sql) -> cnetmod::task<cnetmod::orm::query_result>
        {
            ++calls;
            received_sql = sql;
            co_return cnetmod::orm::query_result{};
        }

        auto execute(std::string_view sql) -> cnetmod::task<cnetmod::orm::query_result>
        {
            return query(sql);
        }

        auto execute(cnetmod::orm::parameterized_query sql) -> cnetmod::task<cnetmod::orm::query_result>
        {
            ++calls;
            received_sql = std::move(sql.query);
            if (!sql.args.empty())
                received_binding = std::move(sql.args.front().str_val);
            co_return cnetmod::orm::query_result{};
        }
    } client;

    cnetmod::orm::database_session session{client};
    unsigned exported = 0;
    const cnetmod::instrumentation::span_exporter sink{[&](const auto&)
        {
            ++exported;
        }};
    const auto sink_copies = measure([&]
        {
            auto copy = sink;
        });
    ASSERT_EQ(sink_copies.calls, 0U);
    for (const unsigned operation : {0U, 1U, 2U})
    {
        auto statement = cnetmod::orm::with_params("SELECT ?",
            {cnetmod::orm::param_value::from_string("private-binding")});
        fail_after = 0U;
        auto pending = operation == 0U ? session.query("SELECT ?", {}, sink)
            : operation == 1U          ? session.execute("SELECT ?", {}, sink)
                                       : session.execute(std::move(statement), {}, sink);
        const bool injected = !fail_after;
        fail_after.reset();
        ASSERT_TRUE(injected);
        ASSERT_EQ(client.calls, operation);
        ASSERT_TRUE(cnetmod::sync_wait(std::move(pending)).ok());
        ASSERT_EQ(client.calls, operation + 1U);
        ASSERT_EQ(client.received_sql, std::string{"SELECT ?"});
        if (operation == 2U)
            ASSERT_EQ(client.received_binding, std::string{"private-binding"});
    }
    ASSERT_EQ(exported, 0U);
    {
        auto discarded = session.execute(cnetmod::orm::with_params("SELECT ?",
                                             {cnetmod::orm::param_value::from_string("discarded-binding")}),
            {}, sink);
        ASSERT_EQ(client.calls, 3U);
        ASSERT_EQ(exported, 0U);
    }
    auto pending = session.execute(cnetmod::orm::with_params("SELECT ?",
                                       {cnetmod::orm::param_value::from_string("recovered-binding")}),
        {}, sink);
    ASSERT_EQ(client.calls, 3U);
    ASSERT_TRUE(cnetmod::sync_wait(std::move(pending)).ok());
    ASSERT_EQ(client.calls, 4U);
    ASSERT_EQ(client.received_binding, std::string{"recovered-binding"});
    ASSERT_EQ(exported, 1U);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
TEST(mysql_client_allocation_failure_does_not_publish_a_null_pool_slot)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 1;
    cnetmod::mysql::connection_pool pool{*io, options};
    auto pending = pool.async_run();
    fail_after = 0U;
    pending.handle().resume();
    fail_after.reset();
    ASSERT_TRUE(pending.handle().done());
    bool failed = false;
    try
    {
        pending.handle().promise().result();
    }
    catch (const std::bad_alloc&)
    {
        failed = true;
    }
    ASSERT_TRUE(failed);
    ASSERT_EQ(pool.size(), 0U);
    ASSERT_FALSE(pool.try_get_connection().has_value());
    ASSERT_EQ(pool.size(), 0U);
    pool.request_stop();
}

TEST(mysql_waiter_cancellation_schedules_without_heap_allocation)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::connection_pool pool{*io, options};
    cnetmod::cancel_token token;
    auto pending = pool.async_get_connection(token);
    pending.handle().resume();
    ASSERT_EQ(pool.waiter_count(), 1U);
    fail_after = 0U;
    const auto totals = measure([&]
        {
            token.cancel();
        });
    fail_after.reset();
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
    io->poll();
    ASSERT_TRUE(pending.handle().done());
    auto result = pending.handle().promise().result();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error() == std::errc::operation_canceled);
    ASSERT_EQ(pool.waiter_count(), 0U);
}

TEST(mysql_pool_stop_schedules_waiters_without_heap_allocation)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::pool_params options;
    options.initial_size = 0;
    cnetmod::mysql::connection_pool pool{*io, options};
    cnetmod::cancel_token token;
    auto pending = pool.async_get_connection(token);
    pending.handle().resume();
    ASSERT_EQ(pool.waiter_count(), 1U);
    auto stopping = pool.cancel();
    allocation_totals totals;
    fail_after = 0U;
    {
        allocation_window window{totals};
        stopping.handle().resume();
    }
    fail_after.reset();
    ASSERT_TRUE(stopping.handle().done());
    stopping.handle().promise().result();
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
    io->poll();
    ASSERT_TRUE(pending.handle().done());
    auto result = pending.handle().promise().result();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error() == std::errc::operation_canceled);
    ASSERT_EQ(pool.waiter_count(), 0U);
}

TEST(mysql_pool_startup_allocation_failures_reach_primary_task)
{
    unsigned failures = 0;
    for (std::size_t position = 0; position < 12; ++position)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::mysql::pool_params options;
        options.initial_size = 1;
        cnetmod::mysql::connection_pool pool{*io, options};
        auto running = pool.async_run();
        fail_after = position;
        running.handle().resume();
        fail_after.reset();
        // Stop before queued connection work reaches any network operation.
        pool.request_stop();
        for (unsigned step = 0; step < 32; ++step)
            io->poll();
        ASSERT_TRUE(running.handle().done());
        try
        {
            running.handle().promise().result();
        }
        catch (const std::bad_alloc&)
        {
            ++failures;
        }
        catch (const std::system_error& error)
        {
            ASSERT_TRUE(error.code() == std::errc::not_enough_memory);
            ++failures;
        }
        ASSERT_FALSE(pool.try_get_connection().has_value());
        ASSERT_EQ(pool.waiter_count(), 0U);
    }
    ASSERT_TRUE(failures > 0U);
}

TEST(mysql_sharded_startup_failure_joins_dispatched_shards)
{
    for (std::size_t position = 0; position < 16; ++position)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::mysql::pool_params options;
        options.initial_size = 0;
        auto pool = std::make_unique<cnetmod::mysql::sharded_connection_pool>(*io, options, 3);
        auto running = pool->async_run();
        fail_after = position;
        running.handle().resume();
        fail_after.reset();
        pool->request_stop();
        for (unsigned step = 0; step < 32; ++step)
            io->poll();
        ASSERT_TRUE(running.handle().done());
        try
        {
            running.handle().promise().result();
        }
        catch (const std::bad_alloc&)
        {
        }
        pool.reset();
        io->poll();
    }
}

TEST(mysql_pool_startup_failure_releases_existing_waiter)
{
    auto io = cnetmod::make_io_context();
    cnetmod::mysql::connection_pool pool{*io, {}};
    cnetmod::cancel_token token;
    auto acquisition = pool.async_get_connection(token);
    acquisition.handle().resume();
    ASSERT_EQ(pool.waiter_count(), 1U);
    auto running = pool.async_run();
    fail_after = 0U;
    running.handle().resume();
    fail_after.reset();
    ASSERT_TRUE(running.handle().done());
    bool failed = false;
    try
    {
        running.handle().promise().result();
    }
    catch (const std::bad_alloc&)
    {
        failed = true;
    }
    ASSERT_TRUE(failed);
    io->poll();
    ASSERT_TRUE(acquisition.handle().done());
    auto result = acquisition.handle().promise().result();
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error() == std::errc::operation_canceled);
    ASSERT_EQ(pool.waiter_count(), 0U);
}
#endif

TEST(http_parent_snapshot_allocation_failure_preserves_transport_result)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::http::client raw{*io};
    unsigned exported = 0;
    cnetmod::observability::instrumented_http_client observed{raw,
        [&](const auto&)
        {
            ++exported;
        }};
    cnetmod::http::request request{cnetmod::http::http_method::GET, "invalid://request"};
    auto parent = cnetmod::http::tracing::new_root_context();
    auto baseline = cnetmod::sync_wait(raw.send(request));
    fail_after = 0U;
    auto pending = observed.send(request, parent);
    fail_after.reset();
    auto result = cnetmod::sync_wait(std::move(pending));
    ASSERT_FALSE(baseline.has_value());
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(result.error() == baseline.error());
    ASSERT_EQ(exported, 0U);
}

TEST(local_lifecycle_metrics_are_disabled_or_exception_isolated)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub disabled{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    fail_after = 0U;
    const auto totals = measure([&]
        {
            disabled.increment_local_counter("application_disabled_metric_total");
        });
    fail_after.reset();
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
    cnetmod::observability::telemetry_hub enabled{*io,
        {.export_traces = false, .export_metrics = true, .export_logs = false}};
    fail_after = 0U;
    enabled.increment_local_counter("application_fault_injection_metric_total");
    fail_after.reset();
    enabled.increment_local_counter("application_recovered_metric_total");
    ASSERT_TRUE(enabled.metrics().render_openmetrics().find("application_recovered_metric_total") != std::string::npos);
}

TEST(telemetry_adapter_construction_is_failure_isolated)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.endpoint = "http://127.0.0.1:1/v1/traces"}};
    static_assert(noexcept(telemetry.spans()));
    static_assert(noexcept(telemetry.measurements()));
    for (std::size_t position = 0; position < 4; ++position)
    {
        fail_after = position;
        auto tracing = telemetry.spans();
        const bool trace_fault = !fail_after;
        fail_after.reset();
        if (trace_fault)
            ASSERT_FALSE(static_cast<bool>(tracing));
        else
            ASSERT_TRUE(static_cast<bool>(tracing));

        fail_after = position;
        auto metrics = telemetry.measurements();
        const bool metric_fault = !fail_after;
        fail_after.reset();
        if (metric_fault)
            ASSERT_FALSE(static_cast<bool>(metrics));
        else
            ASSERT_TRUE(static_cast<bool>(metrics));
    }
    ASSERT_TRUE(static_cast<bool>(telemetry.spans()));
    ASSERT_TRUE(static_cast<bool>(telemetry.measurements()));

    telemetry.close();
    fail_after = 0U;
    auto tracing = telemetry.spans();
    auto metrics = telemetry.measurements();
    const bool allocation_unused = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(allocation_unused);
    ASSERT_FALSE(static_cast<bool>(tracing));
    ASSERT_FALSE(static_cast<bool>(metrics));
}

TEST(telemetry_shutdown_settlement_checks_do_not_allocate)
{
    cnetmod::net_init network;
    for (unsigned mode = 0; mode != 3; ++mode)
    {
        auto io = cnetmod::make_io_context();
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:1/v1/traces",
                .export_traces = mode != 0,
                .export_metrics = mode != 0,
                .export_logs = mode != 0}};
        if (mode == 2)
            ASSERT_TRUE(telemetry.submit_log({.body = "queued"}));
        bool settled = false;
        fail_after = 0U;
        const auto totals = measure([&]
            {
                settled = telemetry.try_settle_shutdown();
            });
        const bool allocation_unused = fail_after.has_value();
        fail_after.reset();
        ASSERT_TRUE(allocation_unused);
        ASSERT_EQ(totals.calls, 0U);
        ASSERT_EQ(totals.bytes, 0U);
        ASSERT_EQ(settled, mode != 2);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (!settled && std::chrono::steady_clock::now() < limit)
        {
            (void)io->poll();
            settled = telemetry.try_settle_shutdown();
        }
        ASSERT_TRUE(settled);
        fail_after = 0U;
        const bool repeated = telemetry.try_settle_shutdown();
        const bool repeat_allocation_unused = fail_after.has_value();
        fail_after.reset();
        ASSERT_TRUE(repeated);
        ASSERT_TRUE(repeat_allocation_unused);
    }
}

TEST(disabled_exporter_construction_skips_endpoint_derivation_allocations)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::otlp_http_options options;
    options.endpoint = "http://127.0.0.1:4318/collector/v1/traces";
    options.export_traces = false;
    options.export_metrics = false;
    options.export_logs = false;
    auto baseline_options = options;
    allocation_totals baseline;
    {
        allocation_window window{baseline};
        // MSVC's std::map move allocates empty sentinels. Compare the same
        // by-value options transfer, excluding allocations in caller setup.
        const auto transferred = std::move(baseline_options);
        (void)transferred;
    }
    bool constructed = false;
    bool settled = false;
    allocation_totals totals;
    fail_after = baseline.calls;
    try
    {
        allocation_window window{totals};
        cnetmod::observability::otlp_http_exporter exporter{*io, std::move(options)};
        constructed = true;
        settled = exporter.try_settle_shutdown();
    }
    catch (const std::bad_alloc&)
    {
    }
    const bool no_allocation_attempted = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(constructed);
    ASSERT_TRUE(settled);
    ASSERT_TRUE(no_allocation_attempted);
    ASSERT_EQ(totals.calls, baseline.calls);
    ASSERT_EQ(totals.bytes, baseline.bytes);
}

TEST(disabled_telemetry_producers_do_not_attempt_allocation_or_build_payloads)
{
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.endpoint = "http://127.0.0.1:1/v1/traces",
            .export_traces = false,
            .export_metrics = false,
            .export_logs = false}};
    unsigned payloads = 0;
    bool adapter_present = false;
    bool accepted = false;
    fail_after = 0U;
    const auto totals = measure([&]
        {
            for (unsigned iteration = 0; iteration < 32; ++iteration)
            {
                adapter_present |= static_cast<bool>(telemetry.spans());
                adapter_present |= static_cast<bool>(telemetry.measurements());
                adapter_present |= static_cast<bool>(telemetry.server_tracing().on_end);
                auto scope = cnetmod::instrumentation::operation_scope::start(telemetry.spans(), [&]
                    {
                        ++payloads;
                        return cnetmod::instrumentation::active_span{};
                    });
                scope.annotate([&]
                    {
                        ++payloads;
                        return std::vector<std::pair<std::string, std::string>>{};
                    });
                adapter_present |= scope.context() != nullptr;
                scope.complete();
                telemetry.increment_local_counter("disabled.application.counter");
                telemetry.refresh_exporter_metrics();
                accepted |= telemetry.submit_metric_lazy([&]
                    {
                        ++payloads;
                        return cnetmod::observability::otel_metric_record{};
                    });
                accepted |= telemetry.submit_log_lazy([&]
                    {
                        ++payloads;
                        return cnetmod::observability::otel_log_record{};
                    });
            }
        });
    const bool no_allocation_attempted = fail_after.has_value();
    fail_after.reset();
    ASSERT_TRUE(no_allocation_attempted);
    ASSERT_EQ(totals.calls, 0U);
    ASSERT_EQ(totals.bytes, 0U);
    ASSERT_EQ(payloads, 0U);
    ASSERT_FALSE(adapter_present);
    ASSERT_FALSE(accepted);
}

#ifdef CNETMOD_HAS_EPOLL
TEST(epoll_registration_allocation_failure_does_not_publish_empty_slots)
{
    unsigned failures = 0;
    for (std::size_t position = 0; position < 4; ++position)
    {
        cnetmod::epoll_context io;
        fail_after = position;
        const auto added = io.add(-1, EPOLLOUT | EPOLLONESHOT, nullptr);
        const bool injected = !fail_after;
        fail_after.reset();
        ASSERT_FALSE(added.has_value());
        if (injected)
        {
            ++failures;
            ASSERT_TRUE(added.error() == std::errc::not_enough_memory);
        }
        const auto retried = io.add(-1, EPOLLOUT | EPOLLONESHOT, nullptr);
        ASSERT_FALSE(retried.has_value());
        ASSERT_TRUE(retried.error() == std::errc::bad_file_descriptor);
        ASSERT_TRUE(io.remove(-1).has_value());
    }
    ASSERT_TRUE(failures >= 2U);
}

TEST(epoll_cross_thread_cancellation_competes_with_ready_timer)
{
    for (const bool race_publication : {false, true})
    {
        cnetmod::cancel_token token;
        for (unsigned iteration = 0; iteration < 2000; ++iteration)
        {
            cnetmod::epoll_context io;
            token.reset();
            unsigned resumed{};
            auto run = [&]() -> cnetmod::task<void>
            {
                (void)co_await cnetmod::async_timer_wait(io, std::chrono::milliseconds{1}, token);
                ++resumed;
            };
            auto operation = run();
            if (!race_publication)
            {
                operation.handle().resume();
                ASSERT_TRUE(token.pending_.load(std::memory_order_acquire));
            }
            std::atomic<bool> start{};
            std::jthread canceller{[&]
                {
                    while (!start.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    token.cancel();
                }};
            if (!race_publication)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            start.store(true, std::memory_order_release);
            if (race_publication)
                operation.handle().resume();
            (void)io.poll();
            canceller.join();
            (void)io.poll();
            ASSERT_EQ(resumed, 1U);
            ASSERT_TRUE(operation.handle().done());
            ASSERT_FALSE(token.pending_.load(std::memory_order_acquire));
        }
    }
}

TEST(epoll_socket_read_cancellation_preserves_unconsumed_data)
{
    cnetmod::epoll_context io;
    unsigned cancelled{};
    for (unsigned iteration = 0; iteration < 1000; ++iteration)
    {
        int descriptors[2]{};
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, descriptors), 0);
        auto receiver = cnetmod::socket::from_native(descriptors[0]);
        auto sender = cnetmod::socket::from_native(descriptors[1]);
        cnetmod::cancel_token token;
        char received{};
        unsigned resumed{};
        std::expected<std::size_t, std::error_code> result;
        auto read = [&]() -> cnetmod::task<void>
        {
            result = co_await cnetmod::async_read(io, receiver,
                cnetmod::mutable_buffer{&received, 1}, token);
            ++resumed;
        };
        auto operation = read();
        operation.handle().resume();
        ASSERT_TRUE(token.pending_.load(std::memory_order_acquire));
        const char sent = 'q';
        ASSERT_EQ(::send(descriptors[1], &sent, 1, MSG_NOSIGNAL), static_cast<ssize_t>(1));
        std::atomic<bool> start{};
        std::jthread canceller{[&]
            {
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                token.cancel();
            }};
        start.store(true, std::memory_order_release);
        if (iteration % 2U == 0U)
            canceller.join();
        (void)io.poll();
        if (canceller.joinable())
            canceller.join();
        (void)io.poll();
        ASSERT_EQ(resumed, 1U);
        ASSERT_TRUE(operation.handle().done());
        ASSERT_FALSE(token.pending_.load(std::memory_order_acquire));
        if (!result)
        {
            ++cancelled;
            ASSERT_EQ(result.error(), cnetmod::make_error_code(cnetmod::errc::operation_aborted));
            token.reset();
            auto retry = read();
            retry.handle().resume();
            (void)io.poll();
            ASSERT_TRUE(retry.handle().done());
            ASSERT_EQ(resumed, 2U);
            ASSERT_TRUE(result.has_value());
        }
        ASSERT_EQ(*result, 1U);
        ASSERT_EQ(received, sent);
    }
    ASSERT_TRUE(cancelled >= 500U);
}

TEST(epoll_batch_does_not_dispatch_replacement_from_stale_registration)
{
    cnetmod::epoll_context io;

    struct owned_event
    {
        int descriptor = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

        ~owned_event()
        {
            if (descriptor >= 0)
                ::close(descriptor);
        }
    } first, second;

    ASSERT_TRUE(first.descriptor >= 0 && second.descriptor >= 0);
    unsigned obsolete{};
    unsigned replacement{};
    auto old_work = [&]() -> cnetmod::task<void>
    {
        ++obsolete;
        co_return;
    };
    auto new_work = [&]() -> cnetmod::task<void>
    {
        ++replacement;
        co_return;
    };
    auto old_task = old_work();
    auto new_task = new_work();
    auto cancel = [&]() -> cnetmod::task<void>
    {
        ASSERT_TRUE(io.remove(second.descriptor));
        ASSERT_TRUE(io.add(second.descriptor, EPOLLIN | EPOLLONESHOT, new_task.handle().address()));
        co_return;
    };
    auto cancel_task = cancel();
    ASSERT_TRUE(io.add(first.descriptor, EPOLLIN | EPOLLONESHOT, cancel_task.handle().address()));
    ASSERT_TRUE(io.add(second.descriptor, EPOLLIN | EPOLLONESHOT, old_task.handle().address()));
    const std::uint64_t signal = 1;
    ASSERT_EQ(::write(first.descriptor, &signal, sizeof(signal)), static_cast<ssize_t>(sizeof(signal)));
    ASSERT_EQ(::write(second.descriptor, &signal, sizeof(signal)), static_cast<ssize_t>(sizeof(signal)));
    (void)io.poll();
    ASSERT_TRUE(cancel_task.handle().done());
    ASSERT_EQ(obsolete, 0U);
    ASSERT_EQ(replacement, 0U);
    (void)io.poll();
    ASSERT_EQ(replacement, 1U);
    (void)io.poll();
    ASSERT_EQ(replacement, 1U);
}

TEST(epoll_registration_recovers_and_delivers_readiness_once)
{
    struct owned_event
    {
        int descriptor = ::eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);

        ~owned_event()
        {
            if (descriptor >= 0)
                ::close(descriptor);
        }
    };

    unsigned failures = 0;
    for (std::size_t position = 0; position < 4; ++position)
    {
        cnetmod::epoll_context io;
        owned_event event;
        ASSERT_TRUE(event.descriptor >= 0);
        unsigned resumed = 0;
        auto completion = [&]() -> cnetmod::task<void>
        {
            ++resumed;
            co_return;
        };
        auto pending = completion();
        fail_after = position;
        const auto added = io.add(event.descriptor, EPOLLIN | EPOLLONESHOT,
            pending.handle().address());
        const bool injected = !fail_after;
        fail_after.reset();
        if (injected)
        {
            ++failures;
            ASSERT_FALSE(added.has_value());
            ASSERT_TRUE(added.error() == std::errc::not_enough_memory);
        }
        else
            ASSERT_TRUE(added.has_value());
        ASSERT_TRUE(io.add(event.descriptor, EPOLLIN | EPOLLONESHOT,
                          pending.handle().address())
                .has_value());
        ASSERT_EQ(resumed, 0U);
        (void)io.poll();
        ASSERT_EQ(resumed, 1U);
        ASSERT_TRUE(pending.handle().done());
        (void)io.poll();
        ASSERT_EQ(resumed, 1U);
        ASSERT_TRUE(io.remove(event.descriptor).has_value());
    }
    ASSERT_TRUE(failures >= 2U);
}
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
    #include "mysql_pool_contention_cases.inc"
#endif

#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    #include "amqp091_confirmation_allocation_cases.inc"
    #include "amqp091_recovery_allocation_cases.inc"
    #include "amqp091_rpc_allocation_cases.inc"
#endif

RUN_TESTS()
