module;
#include <cnetmod/config.hpp>
module cnetmod.observability.kafka_producer;
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;
import cnetmod.observability.messaging;

namespace cnetmod::observability {
namespace {
    struct send_request
    {
        std::string topic;
        kafka::record record;
        instrumentation::operation_scope scope;
    };

    /**
     * @brief Transfers the request only after coroutine frame allocation succeeds.
     *
     * The initial parameter borrows the caller's request. Its move into the
     * coroutine frame acquires ownership without allocation. A frame allocation
     * failure therefore leaves the original request available for an unobserved send.
     */
    struct send_transfer
    {
        explicit send_transfer(send_request& request) noexcept : source(&request) {}

        send_transfer(const send_transfer&) = delete;

        send_transfer(send_transfer&& other) noexcept
            : owned(std::move(*other.source))
        {}

        send_request* source = nullptr;
        std::optional<send_request> owned;
    };

    static_assert(std::is_nothrow_move_constructible_v<send_request>);

    auto observed_send(kafka::producer& producer, send_transfer transfer, cancel_token* cancellation)
        -> task<kafka::result<kafka::record_metadata>>
    {
        auto& [topic, record, scope] = *transfer.owned;
        if (const auto* context = scope.context())
            messaging::inject(record, *context);
        try
        {
            auto result = cancellation
                ? co_await producer.send(std::move(topic), std::move(record), *cancellation)
                : co_await producer.send(std::move(topic), std::move(record));
            instrumentation::operation_result outcome;
            if (!result)
            {
                outcome.status = result.error().code == kafka::error_code::cancelled
                    ? instrumentation::operation_status::cancelled
                    : result.error().code == kafka::error_code::request_timed_out
                    ? instrumentation::operation_status::timeout
                    : instrumentation::operation_status::error;
            }
            scope.complete(outcome);
            co_return result;
        }
        catch (...)
        {
            scope.complete({.status = instrumentation::operation_status::error});
            throw;
        }
    }
} // namespace

instrumented_kafka_producer::instrumented_kafka_producer(kafka::producer producer,
    instrumentation::span_exporter sink) noexcept
    : producer_(std::move(producer)), sink_(std::move(sink)) {}

auto instrumented_kafka_producer::send(std::string topic, kafka::record record,
    const instrumentation::trace_context& parent, cancel_token* cancellation)
    -> task<kafka::result<kafka::record_metadata>>
{
    return send_kafka_record(producer_, std::move(topic), std::move(record), parent, sink_, cancellation);
}

auto instrumented_kafka_producer::flush() -> task<kafka::result<void>>
{
    return producer_.flush();
}

auto instrumented_kafka_producer::begin_transaction(cancel_token* cancellation) -> task<kafka::result<void>>
{
    return producer_.begin_transaction(cancellation);
}

auto instrumented_kafka_producer::send_offsets_to_transaction(std::string_view group,
    const std::map<kafka::topic_partition, kafka::offset_and_metadata>& offsets,
    cancel_token* cancellation) -> task<kafka::result<void>>
{
    return producer_.send_offsets_to_transaction(group, offsets, cancellation);
}

auto instrumented_kafka_producer::commit_transaction(cancel_token* cancellation) -> task<kafka::result<void>>
{
    return producer_.commit_transaction(cancellation);
}

auto instrumented_kafka_producer::abort_transaction(cancel_token* cancellation) -> task<kafka::result<void>>
{
    return producer_.abort_transaction(cancellation);
}

auto instrumented_kafka_producer::transaction_state() const noexcept -> kafka::producer_transaction_state
{
    return producer_.transaction_state();
}

auto instrumented_kafka_producer::producer_identity() const noexcept -> std::optional<std::pair<std::int64_t, std::int16_t>>
{
    return producer_.producer_identity();
}

void instrumented_kafka_producer::close() noexcept
{
    producer_.close();
}

auto send_kafka_record(kafka::producer& producer, std::string topic, kafka::record record,
    const instrumentation::trace_context& parent, const instrumentation::span_exporter& sink,
    cancel_token* cancellation) -> task<kafka::result<kafka::record_metadata>>
{
    if (!sink)
        return cancellation ? producer.send(std::move(topic), std::move(record), *cancellation)
                            : producer.send(std::move(topic), std::move(record));
    auto scope = instrumentation::operation_scope::start(sink, [&]
        {
            auto span = instrumentation::start_client_span(parent, "kafka send",
                {{"messaging.system", "kafka"}, {"messaging.operation.type", "send"}});
            span.kind = instrumentation::span_kind::producer;
            return span;
        });
    if (!scope.context())
        return cancellation ? producer.send(std::move(topic), std::move(record), *cancellation)
                            : producer.send(std::move(topic), std::move(record));
    send_request request{std::move(topic), std::move(record), std::move(scope)};
    try
    {
        return observed_send(producer, send_transfer{request}, cancellation);
    }
    catch (const std::bad_alloc&)
    {
        return cancellation ? producer.send(std::move(request.topic), std::move(request.record), *cancellation)
                            : producer.send(std::move(request.topic), std::move(request.record));
    }
}
} // namespace cnetmod::observability
#endif
