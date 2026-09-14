module;
#include <cnetmod/config.hpp>

/**
 * @brief Per-record Kafka producer observation without protocol-layer OTEL dependencies.
 */
export module cnetmod.observability.kafka_producer;

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka;
import cnetmod.instrumentation.tracing;

export namespace cnetmod::observability {
/**
 * @brief Owns a Kafka producer and its optional per-record observation sink.
 * @details Parent context is explicit and never stored in thread-local state.
 * Outstanding tasks must finish before this object is moved or destroyed.
 */
class instrumented_kafka_producer
{
public:
    instrumented_kafka_producer(kafka::producer producer, instrumentation::span_exporter sink = {}) noexcept;
    auto send(std::string topic, kafka::record record,
        const instrumentation::trace_context& parent = {}, cancel_token* cancellation = nullptr)
        -> task<kafka::result<kafka::record_metadata>>;
    auto flush() -> task<kafka::result<void>>;
    auto begin_transaction(cancel_token* cancellation = nullptr) -> task<kafka::result<void>>;
    auto send_offsets_to_transaction(std::string_view group,
        const std::map<kafka::topic_partition, kafka::offset_and_metadata>& offsets,
        cancel_token* cancellation = nullptr) -> task<kafka::result<void>>;
    auto commit_transaction(cancel_token* cancellation = nullptr) -> task<kafka::result<void>>;
    auto abort_transaction(cancel_token* cancellation = nullptr) -> task<kafka::result<void>>;
    [[nodiscard]] auto transaction_state() const noexcept -> kafka::producer_transaction_state;
    [[nodiscard]] auto producer_identity() const noexcept -> std::optional<std::pair<std::int64_t, std::int16_t>>;
    void close() noexcept;

private:
    kafka::producer producer_;
    instrumentation::span_exporter sink_;
};

/**
 * @brief Sends one record with explicit parent context and an optional observation sink.
 * @details An empty sink returns the original producer task without a wrapper frame.
 * The producer and optional cancellation token must outlive the returned task.
 */
auto send_kafka_record(kafka::producer& producer, std::string topic, kafka::record record,
    const instrumentation::trace_context& parent, const instrumentation::span_exporter& sink,
    cancel_token* cancellation = nullptr) -> task<kafka::result<kafka::record_metadata>>;
} // namespace cnetmod::observability
#endif
