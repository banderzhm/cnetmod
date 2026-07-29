# High-concurrency Kafka and AMQP services

The Kafka, AMQP 0-9-1, and AMQP 1.0 examples are service-shaped applications,
not one-shot protocol probes. They publish and consume 10,000 messages by
default and expose concurrency, backpressure, authentication, and TLS settings
through environment variables. All operational output uses cnetmod's logger.

## Spring Boot concept mapping

| Spring Boot concept | cnetmod example component |
| --- | --- |
| `@ConfigurationProperties` | `*_config.hpp::configuration` |
| application lifecycle bean | `*_application.hpp::application` |
| `KafkaTemplate` / `RabbitTemplate` / JMS producer | sender or publisher service |
| `@KafkaListener(concurrency=N)` | N consumers in `kafka/consumer_service.hpp` |
| `SimpleMessageListenerContainer` | `amqp091/listener_container.hpp` |
| JMS listener container | `amqp10/receiver_container.hpp` |
| listener method / domain service | `process_order()` or `process_delivery()` |

Each `main()` only loads configuration, constructs the application, and starts
the I/O context. Connection setup, workers, business processing,
acknowledgement, commit, settlement, and shutdown are separate components.

## Build

```bash
cmake --build cmake-build-debug-wsl --target \
  example_kafka_demo example_amqp091_demo example_amqp10_demo
```

## Kafka

The example uses one idempotent producer with N sending coroutines, batching,
gzip, `acks=all`, bounded in-flight requests, and retry backoff. N independent
consumers join one group with cooperative-sticky assignment. Domain processing
finishes before the offset is committed.

```bash
export CNETMOD_KAFKA_HOST=127.0.0.1
export CNETMOD_KAFKA_PORT=9092
export CNETMOD_KAFKA_TOPIC=orders.created
export CNETMOD_KAFKA_GROUP=orders-service
export CNETMOD_KAFKA_PRODUCER_CONCURRENCY=8
export CNETMOD_KAFKA_CONSUMER_CONCURRENCY=4
export CNETMOD_KAFKA_MESSAGE_COUNT=10000

# Optional SASL/PLAIN and TLS
export CNETMOD_KAFKA_USERNAME=application
export CNETMOD_KAFKA_PASSWORD='replace-me'
export CNETMOD_KAFKA_CA_FILE=/run/secrets/kafka-ca.pem

./cmake-build-debug-wsl/examples/kafka/example_kafka_demo
```

The topic should have at least as many partitions as consumer workers. Kafka
cannot run more active consumers than partitions in the same group.

## RabbitMQ / AMQP 0-9-1

The example uses N publisher channels, durable topology, persistent messages,
publisher confirms, and N consumer channels with independent prefetch windows.
Application work finishes before manual ACK. Heartbeats, reconnect backoff, and
automatic topology restoration are enabled.

```bash
export CNETMOD_AMQP091_HOST=127.0.0.1
export CNETMOD_AMQP091_PORT=5672
export CNETMOD_AMQP091_USERNAME=application
export CNETMOD_AMQP091_PASSWORD='replace-me'
export CNETMOD_AMQP091_EXCHANGE=orders.events
export CNETMOD_AMQP091_QUEUE=orders.created.worker
export CNETMOD_AMQP091_ROUTING_KEY=orders.created
export CNETMOD_AMQP091_PUBLISHER_CONCURRENCY=8
export CNETMOD_AMQP091_CONSUMER_CONCURRENCY=4
export CNETMOD_AMQP091_PREFETCH=128
export CNETMOD_AMQP091_MESSAGE_COUNT=10000

# Optional TLS
export CNETMOD_AMQP091_CA_FILE=/run/secrets/rabbitmq-ca.pem

./cmake-build-debug-wsl/examples/amqp091/example_amqp091_demo
```

Use a dedicated least-privilege RabbitMQ account in deployed environments. The
default `guest` credentials are only suitable for a localhost developer broker.

## Artemis / AMQP 1.0

The example uses one recoverable connection with N sender sessions and links.
Unsettled sends validate the broker's accepted outcome. N receiver links each
have a configurable credit window; application work finishes before accepted
settlement. Idle timeout, exponential reconnect, and link recovery are enabled.

```bash
export CNETMOD_AMQP10_HOST=127.0.0.1
export CNETMOD_AMQP10_PORT=5672
export CNETMOD_AMQP10_USERNAME=application
export CNETMOD_AMQP10_PASSWORD='replace-me'
export CNETMOD_AMQP10_ADDRESS=orders.created
export CNETMOD_AMQP10_SENDER_CONCURRENCY=8
export CNETMOD_AMQP10_RECEIVER_CONCURRENCY=4
export CNETMOD_AMQP10_RECEIVER_CREDIT=128
export CNETMOD_AMQP10_MESSAGE_COUNT=10000

# Optional TLS
export CNETMOD_AMQP10_CA_FILE=/run/secrets/artemis-ca.pem

./cmake-build-debug-wsl/examples/amqp10/example_amqp10_demo
```

## Business processing and idempotency

The one-millisecond processing delay marks the domain-operation boundary. In a
real service, replace it with the database transaction, HTTP call, or command
handler while preserving this order:

1. receive and validate the message;
2. perform the idempotent domain operation;
3. commit the Kafka offset, ACK AMQP 0-9-1, or settle AMQP 1.0;
4. do not acknowledge when processing fails.

Broker delivery guarantees do not make database or HTTP side effects
automatically idempotent. Store the message ID or business key in the same
database transaction as the domain changes.
