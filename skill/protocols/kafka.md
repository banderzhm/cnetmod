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

带取消令牌的连接和请求路径将令牌传入 Happy Eyeballs、握手及响应前缀/正文读写。
Application 回归通过本地不响应 TCP 对端验证请求等待的超时和主动取消；这不代表真实
TLS、认证、请求锁等待和所有重连竞争已完成验证。

连接事件按观察者分别隔离异常，单个观察者抛出异常不会跳过后续观察者或中断传输清理。
单线程回调中新增观察者不会使当前遍历失效：本轮通知范围在开始时固定，新增登记从后续
事件开始接收通知。当前观察者在回调期间由强引用保活，不复制整份登记表。
嵌套事件派发仅在最外层通知结束后清理失效登记，避免使外层索引越界；本地 TCP 回归
覆盖连接回调中嵌套连接通知。该保障不代表支持回调中销毁连接对象或跨线程共享连接。

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

元数据更新与观察者通知分离：无有效观察者时不复制通知快照；通知准备分配失败不会撤销
已提交的缓存更新，当前通知会跳过，后续更新可继续通知。单个观察者异常不会传播给调用方
或跳过其他观察者；回调在缓存锁外执行。观察者不应作为必须成功的业务处理入口。

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

消费者必须在所属 executor 上串行访问。`close()` 任务开始执行后，
`subscribe/assign/poll/commit/seek` 返回 `configuration` 错误，不再调用后端；
关闭前创建但尚未执行的任务也遵守此规则。清理失败可再次调用 `close()`，
但不会重新开放业务操作。多个 `close()` 任务串行清理，首次成功后不再重复调用后端。
调用方仍须等待已经运行的操作结束；等待清理的 `close()` 任务必须保留并等待完成，
不能直接销毁挂起的任务。

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

`close()` 在关闭传输前封闭当前运行时，拒绝该运行时上的工厂创建和元数据重连。
随后显式调用 `connect()` 会创建新的运行时；旧句柄不会自动绑定到新运行时。
同步 `close()` 不会等待消费者维护任务或在途 I/O，不能替代消费者的异步清理。
生命周期操作必须在所属 executor 上执行。

`co_await client.async_close(token)` 禁止新建消费者/生产者，取消并等待已登记的
消费者维护任务，再清理消费组、fetch session 与连接；失败时保留登记以便重试。
仅维护任务失败但资源已全部清理时，首次关闭仍返回任务错误，同时释放登记和传输；
后续关闭幂等成功。资源清理本身失败时才保留待清理登记。
登记使用弱消费者引用和独立维护任务所有权，因此提前释放消费者也不会跳过任务等待。
`requires_async_close()` 表示是否还有消费者登记需要清理；Application Kafka 服务据此
选择空登记的同步关闭或带 deadline 的异步关闭。调用方仍须自行取消并等待已经运行的
业务操作。创建消费者或查询 `requires_async_close()` 时会回收已成功完成且消费者
已清理/释放的登记；仍在运行或失败的登记不会被丢弃，关闭迭代期间不回收。
维护失败的自动恢复及取消等待中的锁尚需进一步验证。

`background_error()` 无分配地读取首个已结束的消费者维护任务错误，不清除错误。
Application 的 Kafka probe 在元数据请求前后检查此结果，失败时报告 `down`，
而不是用元数据成功覆盖后台任务失败。`restart_failed_maintenance()` 只替换仍开放的
消费者上已失败的维护任务；先登记替代任务的所有权，再派发，分配异常保留原失败登记。
Application 对已启动服务的恢复调用会使用此入口，重试节奏与预算仍由生命周期监管器负责。
本地协议对端已验证维护任务分配失败、健康 down、恢复分配失败保留原错误，
以及生命周期监管器触发恢复后，连续两次成功探测才恢复 readiness，再完成停机。
真实 Broker 中断与监管器多次退避/预算耗尽的联合验证仍未完成。
维护循环内部尚在重试的协议错误不属于
“任务已结束”错误。

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
