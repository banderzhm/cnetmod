# Kafka 协议模块

> 完整的 Apache Kafka 协议客户端，支持生产者、消费者、消费组、事务与 SASL 认证。

**import**: `import cnetmod.protocol.kafka;`
**CMake**: `-DCNETMOD_ENABLE_KAFKA=ON`
**源码**: `src/protocol/kafka/`

## 场景导航

| 场景 | 关键类型 |
|------|---------|
| 异步生产消息 | `producer`, `producer_options`, `record` |
| 消费组消费 | `consumer`, `consumer_options`, `consumed_record` |
| 连接 Broker | `client_facade`, `client_options`, `broker_connection` |
| 分区策略 / SASL | `partitioner`, `sasl_authenticator` |
| 偏移量 / 消费组 | `offset_manager`, `group_coordinator` |
| 协议编解码 | `encoder`, `decoder`, `broker_request_codec`, `record_batch` |

## API 参考

### 协议常量与基础类型

**签名**:
```cpp
namespace cnetmod::kafka {
using bytes = std::vector<std::byte>;
enum class error_code : std::int16_t { none = 0, unknown_server_error = -1, ... };
struct error { error_code code; std::string message; bool retriable; };
template <typename T> using result = std::expected<T, error>;
struct topic_partition { std::string topic; std::int32_t partition; };
struct record { std::optional<bytes> key, value; std::vector<header> headers; };
struct consumed_record {
    topic_partition source; std::int64_t offset, timestamp;
    std::optional<bytes> key, value; std::vector<header> headers;
};
enum class compression : std::int8_t { none, gzip, snappy, lz4, zstd };
enum class acknowledgement : std::int16_t { none = 0, leader = 1, all = -1 };
enum class sasl_mechanism { none, plain, scram_sha_256, scram_sha_512 };
}
```

### `client_options` — 连接配置

**签名**:
```cpp
struct client_options {
    std::vector<client_endpoint> bootstrap_servers;
    std::string client_id = "cnetmod";
    authentication_credentials credentials;
    sasl_mechanism sasl = sasl_mechanism::none;
    std::chrono::milliseconds request_timeout{30000};
    std::size_t retries = 5;
    std::shared_ptr<scram_crypto_provider> scram_crypto;
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.kafka;

kafka::client_options opts;
opts.bootstrap_servers.push_back({.host = "kafka-broker", .port = 9092});
opts.client_id = "my-service";
opts.credentials = {.username = "user", .password = "pass"};
opts.sasl = kafka::sasl_mechanism::plain;
```

### `request_header` / `response_header` — 请求响应头

**签名**:
```cpp
namespace cnetmod::kafka::protocol {
enum class api_key : std::int16_t {
    produce = 0, fetch = 1, metadata = 3, join_group = 11,
    sasl_handshake = 17, api_versions = 18, sasl_authenticate = 36, ...
};
struct request_header {
    api_key key; std::int16_t version;
    std::int32_t correlation_id; std::string client_id;
};
struct response_header { std::int32_t correlation_id; };
}
```

### `protocol_value_codec` — 协议值编解码

**签名**:
```cpp
namespace cnetmod::kafka::protocol {
class encoder {
    void int8/int16/int32/int64(...); void string(std::string_view);
    void varint(std::int32_t); void varlong(std::int64_t);
    auto take() && -> bytes;
};
class decoder {
    explicit decoder(std::span<const std::byte> input) noexcept;
    auto int8/int16/int32/int64() -> result<...>;
    auto string() -> result<std::string>;
    auto remaining() const noexcept -> std::size_t;
};
auto encode_request(request_header, std::span<const std::byte>) -> bytes;
auto decode_response_header(decoder&) -> result<response_header>;
auto crc32c(std::span<const std::byte>) noexcept -> std::uint32_t;
}
```

### `record_batch` — 消息批次编解码

**签名**:
```cpp
class compression_codec {
    virtual auto compress(std::span<const std::byte>) -> result<bytes> = 0;
    virtual auto decompress(std::span<const std::byte>, std::size_t) -> result<bytes> = 0;
};
class compression_registry {
    void install(std::shared_ptr<compression_codec>);
    auto find(compression) const -> std::shared_ptr<compression_codec>;
};
auto encode_record_batch(std::span<const record>, const record_batch_options&,
    const compression_registry&) -> result<bytes>;
auto decode_record_batch(std::span<const std::byte>, const topic_partition&,
    const compression_registry&) -> result<decoded_record_batch>;
```

### `broker_request_codec` — Broker 请求编解码

**签名**:
```cpp
auto encode_api_versions() -> bytes;
auto decode_api_versions(std::span<const std::byte>, std::int16_t) -> result<api_versions_response>;
auto encode_produce(const produce_request&, std::int16_t) -> bytes;
auto decode_produce(std::span<const std::byte>, std::int16_t, const produce_request&)
    -> result<std::vector<produce_result>>;
auto encode_fetch(const fetch_request&, std::int16_t) -> bytes;
auto decode_fetch(std::span<const std::byte>, std::int16_t) -> result<fetch_response>;
auto encode_join_group(const join_group_request&, std::int16_t) -> bytes;
auto encode_heartbeat(const group_identity&, std::int16_t) -> bytes;
auto encode_offset_commit(const group_identity&,
    const std::map<topic_partition, std::int64_t>&, std::int16_t) -> bytes;
```

### `broker_connection` — Broker 传输连接

**签名**:
```cpp
class broker_connection {
    broker_connection(io_context&, broker_endpoint, client_options);
    auto connect() -> task<result<void>>;
    auto request(protocol::api_key, std::int16_t, std::span<const std::byte>) -> task<result<bytes>>;
    void close() noexcept;
    auto is_open() const noexcept -> bool;
    void add_observer(std::weak_ptr<connection_observer>);
};
```

### `sasl_authenticator` — SASL 认证

**签名**:
```cpp
class sasl_authenticator {
    virtual auto mechanism_name() const noexcept -> std::string_view = 0;
    virtual auto initial_response() -> result<bytes> = 0;
    virtual auto challenge(std::span<const std::byte>) -> result<bytes> = 0;
    virtual auto complete() const noexcept -> bool = 0;
};
auto make_plain_authenticator(std::string, std::string) -> std::unique_ptr<sasl_authenticator>;
auto make_scram_authenticator(sasl_mechanism, std::string, std::string,
    std::shared_ptr<scram_crypto_provider>) -> result<std::unique_ptr<sasl_authenticator>>;
```

### `metadata_cache` — 元数据缓存

**签名**:
```cpp
class metadata_cache {
    void update(protocol::metadata_response);
    auto leader(const topic_partition&) const -> result<broker_endpoint>;
    auto partitions(std::string_view) const -> std::vector<std::int32_t>;
    auto broker(std::int32_t) const -> std::optional<broker_endpoint>;
    void add_observer(std::weak_ptr<metadata_observer>);
};
```

### `partitioner` — 分区策略

**签名**:
```cpp
class partitioner {
    virtual auto select(std::string_view, std::span<const std::byte>,
        std::span<const std::int32_t>) -> result<std::int32_t> = 0;
};
class murmur2_partitioner final : public partitioner { /* ... */ };
class uniform_sticky_partitioner final : public partitioner { /* ... */ };
```

### `producer` — 异步生产者

**签名**:
```cpp
struct producer_options {
    acknowledgement acks = acknowledgement::all;
    compression compression_type = compression::none;
    std::size_t batch_bytes = 1024 * 1024;
    std::chrono::milliseconds linger{5};
    bool idempotent = true;
    std::optional<std::string> transactional_id;
};
class producer {
    auto send(std::string topic, record) -> task<result<record_metadata>>;
    auto send(std::string topic, record, cancel_token&) -> task<result<record_metadata>>;
    auto flush() -> task<result<void>>;
    auto begin_transaction(cancel_token* = nullptr) -> task<result<void>>;
    auto commit_transaction(cancel_token* = nullptr) -> task<result<void>>;
    auto abort_transaction(cancel_token* = nullptr) -> task<result<void>>;
    void close() noexcept;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.kafka;

auto produce(client_facade& client) -> task<void> {
    auto producer = *client.make_producer({
        .acks = kafka::acknowledgement::all,
        .compression_type = kafka::compression::gzip,
        .idempotent = true
    });
    kafka::record rec;
    rec.key = bytes_from("order-123");
    rec.value = bytes_from(R"({"orderId":123})");
    auto result = co_await producer.send("orders", std::move(rec));
    co_await producer.flush();
    producer.close();
}
```

### `offset_manager` — 偏移量管理

**签名**:
```cpp
struct offset_and_metadata { std::int64_t offset; std::string metadata; };
class offset_manager {
    explicit offset_manager(std::shared_ptr<offset_backend>);
    void stage(topic_partition, offset_and_metadata);
    auto commit(std::string_view, std::int32_t, std::string_view,
        cancel_token* = nullptr) -> task<result<void>>;
    auto fetch(std::string_view, std::span<const topic_partition>, cancel_token* = nullptr)
        -> task<result<std::map<topic_partition, offset_and_metadata>>>;
};
```

### `group_coordinator` — 消费组协调

**签名**:
```cpp
class rebalance_listener {
    virtual auto on_partitions_revoked(std::span<const topic_partition>) -> task<void> = 0;
    virtual auto on_partitions_assigned(std::span<const topic_partition>) -> task<void> = 0;
};
class range_assignment final : public assignment_strategy { /* ... */ };
class cooperative_sticky_assignment final : public assignment_strategy { /* ... */ };
class group_coordinator {
    group_coordinator(std::string, std::shared_ptr<group_backend>,
        std::unique_ptr<assignment_strategy>, std::optional<std::string> = {});
    auto join(std::span<const std::string>, cancel_token*) -> task<result<group_state>>;
    auto heartbeat(cancel_token*) -> task<result<void>>;
    auto leave(cancel_token*) -> task<result<void>>;
    void set_listener(std::weak_ptr<rebalance_listener>);
};
```

### `consumer` — 消费组消费者

**签名**:
```cpp
struct consumer_options {
    std::string group_id;
    std::size_t max_poll_records = 500;
    consumer_assignment_policy assignment_policy;
    offset_reset_policy auto_offset_reset = offset_reset_policy::earliest;
    bool enable_auto_commit = true;
};
class consumer {
    auto subscribe(std::vector<std::string>, cancel_token*) -> task<result<void>>;
    auto poll(cancel_token*) -> task<result<std::vector<consumed_record>>>;
    auto commit(const consumed_record&, cancel_token*) -> task<result<void>>;
    auto seek(topic_partition, std::int64_t, cancel_token*) -> task<result<void>>;
    auto close(cancel_token*) -> task<result<void>>;
};
```

**示例**:
```cpp
auto consume(client_facade& client) -> task<void> {
    auto consumer = *client.make_consumer({
        .group_id = "order-processors",
        .enable_auto_commit = false,
        .assignment_policy = kafka::consumer_assignment_policy::cooperative_sticky
    });
    co_await consumer.subscribe({"orders"});
    while (true) {
        auto batch = co_await consumer.poll();
        for (const auto& rec : *batch)
            co_await consumer.commit(rec); // 业务处理后提交
    }
}
```

### `client_facade` — 客户端门面

**签名**:
```cpp
class client_facade {
public:
    client_facade(io_context&, client_options);
    auto connect(cancel_token* = nullptr) -> task<result<void>>;
    auto refresh_metadata(std::vector<std::string> = {}, cancel_token* = nullptr)
        -> task<result<void>>;
    auto metadata() const -> std::shared_ptr<metadata_cache>;
    auto make_producer(producer_options = {}, std::unique_ptr<partitioner> = {})
        -> result<producer>;
    auto make_consumer(consumer_options) -> result<consumer>;
    void close() noexcept;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.kafka;

auto main() -> int {
    namespace cn = cnetmod;
    cn::net_init network;
    auto context = cn::make_io_context();
    kafka::client_options opts;
    opts.bootstrap_servers.push_back({.host = "127.0.0.1", .port = 9092});
    kafka::client_facade client(*context, std::move(opts));
    cn::spawn(*context, [&]() -> cn::task<void> {
        co_await client.connect();
        auto producer = *client.make_producer();
        auto consumer = *client.make_consumer({.group_id = "demo"});
        client.close();
        context->stop();
    }());
    context->run();
}
```

## Do's & Don'ts

| Do | Don't |
|----|-------|
| 使用 `client_facade` 作为入口创建生产者和消费者 | 直接手动构建 `broker_connection` 发送协议帧 |
| 消费时手动 `commit` 以确保 at-least-once 语义 | 在高吞吐场景对每条消息都同步 commit |
| 配置 `idempotent = true` 实现精确一次投递 | 假设 `send` 立即发送——内部有 linger 批处理 |
| 使用 `cancel_token` 控制长操作生命周期 | 在 `close()` 后继续使用 producer/consumer |
| 提供 `scram_crypto_provider` 实现 SCRAM 认证 | 直接实例化 `sasl_authenticator`——使用工厂函数 |

## 连接池与多核部署

> **注意**：Kafka 模块为纯客户端实现，不提供 `server_context` 多核模式。生产级部署建议如下。

### Producer 批量发送（内置）

`producer` 内置 linger + batch 机制，无需外部连接池。`producer_options` 中的 `batch_bytes` 和 `linger` 控制批量行为：

```cpp
kafka::producer_options opts;
opts.acks = kafka::acknowledgement::all;
opts.compression_type = kafka::compression::gzip;
opts.batch_bytes = 2 * 1024 * 1024;  // 2MB 批次
opts.linger = std::chrono::milliseconds(10);  // 等待 10ms 凑批
opts.idempotent = true;
opts.max_in_flight = 5;  // 最大并行请求数
```

### 多 Worker 消费者部署

每个 `client_facade` 实例绑定一个 `io_context`，可在多个线程上各自创建独立的消费者实例：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.kafka;

namespace cn = cnetmod;
namespace kafka = cnetmod::kafka;

auto consumer_worker(cn::io_context& ctx, const std::string& worker_id) -> cn::task<void> {
    kafka::client_options opts;
    opts.bootstrap_servers.push_back({.host = "kafka-broker", .port = 9092});
    opts.client_id = worker_id;

    kafka::client_facade client(ctx, std::move(opts));
    co_await client.connect();

    auto consumer = *client.make_consumer({
        .group_id = "order-processors",  // 同 group_id 自动分区消费
        .enable_auto_commit = false,
        .assignment_policy = kafka::consumer_assignment_policy::cooperative_sticky
    });

    co_await consumer.subscribe({"orders"});

    while (true) {
        auto batch = co_await consumer.poll();
        if (!batch) break;
        for (const auto& rec : *batch) {
            std::println("[{}] offset={} key={}", worker_id, rec.offset,
                rec.key ? std::string(rec.key->begin(), rec.key->end()) : "null");
            co_await consumer.commit(rec);
        }
    }

    co_await consumer.close();
    client.close();
}

auto main() -> int {
    cn::net_init net;

    constexpr unsigned NUM_WORKERS = 4;
    std::vector<std::unique_ptr<cn::io_context>> contexts;
    std::vector<std::jthread> threads;

    for (unsigned i = 0; i < NUM_WORKERS; ++i) {
        auto& ctx = contexts.emplace_back(cn::make_io_context());
        auto worker_id = std::format("worker-{}", i);
        cn::spawn(*ctx, consumer_worker(*ctx, worker_id));
        threads.emplace_back([&ctx] { ctx->run(); });
    }

    for (auto& t : threads) t.join();
    return 0;
}
```

### Do's & Don'ts（多实例部署）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 每个 worker 独立 `client_facade` + 独立 `io_context` | 多线程共享同一个 `client_facade` |
| 同 `group_id` 多 worker 自动分区消费 | 在不同 group 中重复消费同一 topic |
| 利用 `linger` + `batch_bytes` 自动批量发送 | 每条消息都立即 flush |

---

## 参考示例

- `examples/kafka/kafka_demo.cpp` — 完整的生产者+消费者应用入口
- `examples/kafka/producer_service.hpp` — 并发生产者，支持幂等和压缩
- `examples/kafka/consumer_service.hpp` — 消费组消费者，手动提交偏移量
- `examples/kafka/kafka_application.hpp` — 应用生命周期编排
- `examples/kafka/kafka_config.hpp` — 环境变量配置读取
