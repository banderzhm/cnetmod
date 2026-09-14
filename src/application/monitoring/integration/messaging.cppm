module;

#include <cnetmod/config.hpp>

/**
 * @brief W3C Trace Context propagation adapters for message metadata.
 *
 * Injection replaces existing trace metadata. An empty incoming tracestate
 * removes previous vendor state instead of retaining it on a reused carrier.
 * Preparation failures are contained and leave all original metadata intact.
 */
export module cnetmod.observability.messaging;

import std;
import cnetmod.protocol.http.middleware.tracing;
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import cnetmod.protocol.kafka;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
import cnetmod.protocol.mqtt;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import cnetmod.protocol.amqp091;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import cnetmod.protocol.amqp10;
#endif

export namespace cnetmod::observability::messaging {

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
/**
 * @brief Injects W3C trace context into Kafka record headers.
 */
void inject(kafka::record& destination,
    const http::tracing::trace_context& context) noexcept;

/**
 * @brief Extracts W3C trace context from Kafka record headers.
 */
[[nodiscard]] auto extract(const kafka::consumed_record& source)
    -> std::optional<http::tracing::trace_context>;
#endif

#ifdef CNETMOD_HAS_PROTOCOL_MQTT
/**
 * @brief Injects W3C trace context into MQTT 5 user properties.
 */
void inject(mqtt::properties& destination,
    const http::tracing::trace_context& context) noexcept;

/**
 * @brief Extracts W3C trace context from MQTT 5 user properties.
 */
[[nodiscard]] auto extract(const mqtt::properties& source)
    -> std::optional<http::tracing::trace_context>;
#endif

#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
/**
 * @brief Injects W3C trace context into AMQP 0-9-1 headers.
 */
void inject(amqp091::message& destination,
    const http::tracing::trace_context& context) noexcept;

/**
 * @brief Extracts W3C trace context from AMQP 0-9-1 headers.
 */
[[nodiscard]] auto extract(const amqp091::message& source)
    -> std::optional<http::tracing::trace_context>;
#endif

#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
/**
 * @brief Injects W3C trace context into AMQP 1.0 application properties.
 */
void inject(amqp10::message& destination,
    const http::tracing::trace_context& context) noexcept;

/**
 * @brief Extracts W3C trace context from AMQP 1.0 application properties.
 */
[[nodiscard]] auto extract(const amqp10::message& source)
    -> std::optional<http::tracing::trace_context>;
#endif

} // namespace cnetmod::observability::messaging
