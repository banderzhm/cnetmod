module;
#include <cnetmod/config.hpp>

/**
 * @brief Per-message Kafka processing scopes with explicit coroutine ownership.
 */
export module cnetmod.observability.kafka_consumer;

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import cnetmod.protocol.kafka;
import cnetmod.instrumentation.tracing;
export import cnetmod.instrumentation.operation_scope;

export namespace cnetmod::observability {
/**
 * @brief Starts processing observation from this record's propagated parent only.
 * @details Complete the returned scope with the processing outcome, separately from
 * offset commit. An empty sink skips header parsing and context creation entirely.
 */
[[nodiscard]] auto start_kafka_processing(const kafka::consumed_record& record,
    const instrumentation::span_exporter& sink) noexcept -> instrumentation::operation_scope;
} // namespace cnetmod::observability
#endif
