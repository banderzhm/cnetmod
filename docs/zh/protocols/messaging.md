# Kafka 与 AMQP 高并发消息服务

Kafka、AMQP 0-9-1 和 AMQP 1.0 示例采用真实服务结构，而不是一次性的协议探测程序。默认生产并消费 10,000 条消息，并通过环境变量配置并发度、背压、身份认证和 TLS。所有运行日志统一使用 cnetmod 的 logger。

## 与 Spring Boot 的概念映射

| Spring Boot 概念 | cnetmod 示例组件 |
| --- | --- |
| `@ConfigurationProperties` | `*_config.hpp::configuration` |
| 应用生命周期 Bean | `*_application.hpp::application` |
| `KafkaTemplate` / `RabbitTemplate` / JMS producer | sender 或 publisher service |
| `@KafkaListener(concurrency=N)` | `kafka/consumer_service.hpp` 中的 N 个 consumer |
| `SimpleMessageListenerContainer` | `amqp091/listener_container.hpp` |
| JMS listener container | `amqp10/receiver_container.hpp` |
| listener 方法 / 领域服务 | `process_order()` 或 `process_delivery()` |

每个 `main()` 只负责加载配置、构造 application 并启动 I/O context。连接、并发 worker、业务处理、确认、offset 提交、settlement 和关闭流程均由独立组件负责。

## 构建

```bash
cmake --build cmake-build-debug-wsl --target \
  example_kafka_demo example_amqp091_demo example_amqp10_demo
```

## Kafka

示例使用一个幂等 producer 和 N 个发送协程，启用 batching、gzip、`acks=all`、受控的 in-flight 请求与重试退避。N 个独立 consumer 加入同一个 group，使用 cooperative-sticky 分配策略，并且仅在业务处理成功后提交 offset。

```bash
export CNETMOD_KAFKA_HOST=127.0.0.1
export CNETMOD_KAFKA_PORT=9092
export CNETMOD_KAFKA_TOPIC=orders.created
export CNETMOD_KAFKA_GROUP=orders-service
export CNETMOD_KAFKA_PRODUCER_CONCURRENCY=8
export CNETMOD_KAFKA_CONSUMER_CONCURRENCY=4
export CNETMOD_KAFKA_MESSAGE_COUNT=10000

# 可选的 SASL/PLAIN 与 TLS
export CNETMOD_KAFKA_USERNAME=application
export CNETMOD_KAFKA_PASSWORD='replace-me'
export CNETMOD_KAFKA_CA_FILE=/run/secrets/kafka-ca.pem

./cmake-build-debug-wsl/examples/kafka/example_kafka_demo
```

Topic 的 partition 数量应不少于 consumer worker 数量；同一 group 中超过 partition 数量的 consumer 不会获得有效分配。

## RabbitMQ / AMQP 0-9-1

示例使用 N 个 publisher channel、持久化拓扑、持久化消息、publisher confirm，以及 N 个拥有独立 prefetch 窗口的 consumer channel。业务处理成功后才进行手动 ACK，同时启用心跳、重连退避和拓扑自动恢复。

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

# 可选 TLS
export CNETMOD_AMQP091_CA_FILE=/run/secrets/rabbitmq-ca.pem

./cmake-build-debug-wsl/examples/amqp091/example_amqp091_demo
```

部署时应使用最小权限的专用 RabbitMQ 账号。默认 `guest` 凭据仅适用于本机开发 broker。

## Artemis / AMQP 1.0

示例使用一个可恢复 connection 和 N 个 sender session/link。Unsettled send 会校验 broker 返回的 accepted outcome。N 个 receiver link 分别拥有可配置的 credit 窗口，业务完成后才发送 accepted settlement，并启用 idle timeout、指数重连和 link 恢复。

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

# 可选 TLS
export CNETMOD_AMQP10_CA_FILE=/run/secrets/artemis-ca.pem

./cmake-build-debug-wsl/examples/amqp10/example_amqp10_demo
```

## 业务处理与幂等性

示例中的 1ms 延迟用于标记业务处理边界。实际服务应替换为数据库事务、HTTP 调用或命令处理器，并保持以下顺序：

1. 接收并校验消息；
2. 执行幂等的领域操作；
3. 提交 Kafka offset、ACK AMQP 0-9-1 或 settle AMQP 1.0；
4. 业务失败时不进行确认。

Broker 的投递保证不会自动使数据库或 HTTP 副作用具备幂等性。应在同一个数据库事务中保存 message ID 或业务唯一键与领域变更。
