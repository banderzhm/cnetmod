# AMQP 0-9-1 协议模块

> RabbitMQ 兼容的 AMQP 0-9-1 客户端，支持连接管理、逻辑通道、消息发布确认与拓扑恢复。

**import**: `import cnetmod.protocol.amqp091;`
**CMake**: `-DCNETMOD_ENABLE_AMQP091=ON`
**源码**: `src/protocol/amqp091/`

## 场景导航

| 场景 | 关键类型 |
|------|---------|
| 连接 RabbitMQ | `amqp091_client`, `connection_options` |
| 通道操作 | `logical_channel`, `channel_options` |
| 发布消息 | `message`, `publish_options` |
| 消费消息 | `delivery`, `consume_options`, `delivery_handler` |
| 发布确认 | `publisher_confirm_tracker`, `publisher_confirm_observer` |
| 重连策略 | `exponential_backoff`, `reconnect_policy` |
| 拓扑恢复 | `topology_recorder`, `automatic_recovery_strategy` |
| 帧编解码 | `frame_parser`, `wire_frame_codec`, `field_table_codec` |

## API 参考

### `protocol_constants` — 协议常量与错误类型

**签名**:
```cpp
namespace cnetmod::amqp091 {
inline constexpr std::array<std::byte, 8> protocol_header{
    std::byte{'A'}, std::byte{'M'}, std::byte{'Q'}, std::byte{'P'},
    std::byte{0}, std::byte{0}, std::byte{9}, std::byte{1}};
enum class error_code {
    malformed_frame, frame_too_large, unexpected_frame,
    connection_closed, channel_closed, not_found, timeout, cancelled, ...
};
struct error {
    error_code code; std::string message;
    std::uint16_t reply_code, class_id, method_id; bool retryable;
};
template <typename T> using result = std::expected<T, error>;
enum class frame_type : std::uint8_t { method = 1, header = 2, body = 3, heartbeat = 8 };
enum class connection_state {
    disconnected, connecting, authenticating, opening, open, recovering, closing
};
}
```

### `connection_options` — 连接配置

**签名**:
```cpp
struct tls_options { bool enabled; bool verify_peer; std::string ca_file; ... };
enum class authentication_mechanism { anonymous, plain, external };
struct credentials {
    authentication_mechanism mechanism = authentication_mechanism::plain;
    std::string username, password;
};
struct endpoint {
    std::string host = "127.0.0.1"; std::uint16_t port = 5672;
    std::chrono::milliseconds connect_timeout{10000}; tls_options tls;
};
struct connection_options {
    endpoint endpoint; credentials credentials;
    std::string virtual_host = "/"; std::string locale = "en_US";
    std::string connection_name;
    std::uint16_t channel_max = 0; std::uint32_t frame_max = 131072;
    std::chrono::seconds heartbeat{60};
    bool automatic_recovery = true;
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.amqp091;

amqp091::connection_options opts;
opts.endpoint.host = "rabbitmq-host";
opts.endpoint.port = 5672;
opts.credentials.username = "guest";
opts.credentials.password = "guest";
opts.connection_name = "orders-service";
opts.heartbeat = std::chrono::seconds{15};
opts.automatic_recovery = true;
```

### `channel_options` — 通道声明配置

**签名**:
```cpp
enum class exchange_type { direct, fanout, topic, headers, custom };
struct exchange_declare_options {
    std::string name; exchange_type type = exchange_type::direct;
    bool passive, durable, auto_delete, internal, no_wait;
};
struct queue_declare_options {
    std::string name; bool passive, durable, exclusive, auto_delete, no_wait;
};
struct queue_declare_result {
    std::string name; std::uint32_t message_count, consumer_count;
};
struct binding_options { std::string queue, exchange, routing_key; };
struct publish_options { std::string exchange, routing_key; bool mandatory, immediate; };
struct consume_options {
    std::string queue, consumer_tag;
    bool no_local, no_ack, exclusive, no_wait;
};
struct qos_options {
    std::uint32_t prefetch_size; std::uint16_t prefetch_count; bool global;
};
```

### `message` / `delivery` — 消息与投递

**签名**:
```cpp
struct message {
    std::vector<std::byte> body;
    std::string content_type, content_encoding, message_id, correlation_id, reply_to;
    std::optional<std::chrono::milliseconds> ttl;
    std::map<std::string, std::string, std::less<>> headers;
    bool durable = false;
};
struct delivery {
    message message; std::string consumer_tag, exchange, routing_key;
    std::uint64_t delivery_tag; bool redelivered;
};
struct returned_message {
    message message; std::uint16_t reply_code;
    std::string reply_text, exchange, routing_key;
};
using delivery_handler = std::function<void(const delivery&)>;
using return_handler = std::function<void(const returned_message&)>;
```

### `field_table_codec` — 字段表编解码

**签名**:
```cpp
using field_value = std::variant<std::monostate, bool, std::int8_t, std::uint8_t,
    std::int16_t, std::uint16_t, std::int32_t, std::uint32_t, std::int64_t,
    std::uint64_t, float, double, decimal_value, std::string,
    std::vector<std::byte>, std::shared_ptr<field_array>, std::shared_ptr<field_table>>;
struct field_table { std::map<std::string, field_value, std::less<>> values; };
auto encode_field_table(const field_table&) -> result<std::vector<std::byte>>;
auto decode_field_table(std::span<const std::byte>, std::size_t&) -> result<field_table>;
```

### `wire_frame_codec` — 线帧编解码

**签名**:
```cpp
struct frame { frame_type type; std::uint16_t channel; std::vector<std::byte> payload; };
struct method_frame {
    std::uint16_t channel, class_id, method_id;
    std::vector<std::byte> arguments;
};
struct content_header {
    std::uint16_t channel; std::uint64_t body_size; message properties;
};
class frame_parser {
    explicit frame_parser(std::uint32_t frame_max = 131072) noexcept;
    auto feed(std::span<const std::byte>) -> result<std::vector<frame>>;
    void reset() noexcept;
};
auto encode_frame(const frame&) -> result<std::vector<std::byte>>;
auto encode_method(const method_frame&) -> result<frame>;
auto decode_method(const frame&) -> result<method_frame>;
auto encode_content_header(const content_header&) -> result<frame>;
auto decode_content_header(const frame&) -> result<content_header>;
```

### `publisher_confirm` — 发布者确认

**签名**:
```cpp
struct publisher_confirmation {
    std::uint64_t delivery_tag; bool acknowledged, multiple;
};
class publisher_confirm_observer {
    virtual void on_confirm(const publisher_confirmation&) = 0;
    virtual void on_confirm_failure(const error&) = 0;
};
class publisher_confirm_tracker {
    auto reserve_sequence() -> std::uint64_t;
    void observe(std::weak_ptr<publisher_confirm_observer>);
    void settle(std::uint64_t tag, bool acknowledged, bool multiple);
    void fail_all(const error& reason) noexcept;
    auto pending() const noexcept -> std::size_t;
};
```

发布确认订阅在注册时创建只读快照；通知期间新注册的观察者从下一轮生效。
`settle()` 会完成其他观察者通知后重新抛出首个回调异常；`fail_all()` 清理路径隔离
回调异常且不创建临时通知集合。`reserve_sequence()` 可能抛出分配异常，插入成功后
才递增序号，不再以 `noexcept` 将内存不足转换为进程终止。

### `reconnect_policy` — 重连策略

**签名**:
```cpp
struct reconnect_context { std::size_t attempt; std::chrono::milliseconds previous_delay; };
class reconnect_policy {
    virtual auto next_delay(const reconnect_context&) const
        -> std::optional<std::chrono::milliseconds> = 0;
};
class exponential_backoff final : public reconnect_policy {
    explicit exponential_backoff(
        std::chrono::milliseconds initial = std::chrono::seconds(1),
        std::chrono::milliseconds maximum = std::chrono::seconds(60),
        double multiplier = 2.0, std::size_t maximum_attempts = 0) noexcept;
};
```

### `topology_recovery` — 拓扑恢复

**签名**:
```cpp
struct topology_snapshot {
    std::vector<recorded_exchange> exchanges;
    std::vector<recorded_queue> queues;
    std::vector<recorded_binding> bindings;
    std::vector<recorded_consumer> consumers;
};
class topology_recorder {
    void remember(recorded_exchange/queue/binding/consumer);
    void forget_exchange(std::string_view); void forget_queue(std::string_view);
    void clear();
    auto snapshot() const -> topology_snapshot;
};
class automatic_recovery_strategy final : public recovery_strategy {
    explicit automatic_recovery_strategy(
        std::shared_ptr<reconnect_policy>, bool restore = true);
};
```

### `protocol_connection` — 协议连接

传输层返回 `std::errc::not_enough_memory` 时，协议保留独立的
`error_code::not_enough_memory`，不归类为可重试断连；诊断文本固定且不含请求数据。
此枚举追加在现有值之后，不增加 `error` 的字段。直接协程分配仍可抛出 bad_alloc，
调用方应同时处理异常和 result；会话故障测试覆盖这两种出口。

同一连接遵循单执行器使用约定。`async_connect()` 仅在 disconnected/recovering 状态且
没有活动读取所有者或写锁持有者时接受新握手；否则返回 `command_invalid`，不替换
现有 socket/TLS。先取消并等待旧帧泵及活动操作收尾，再重连；不要以再次 connect
作为强制中止当前会话的方式。

独立 `async_recover()` 拒绝尚未关闭或读取任务尚未结束的会话；提前取消直接返回
`cancelled`。重连退避使用取消感知定时器，取消后不继续等完退避时长，也不发起连接。
恢复操作以作用域所有权覆盖退避与握手；期间外部 connect 和另一轮 recover 返回
`command_invalid`，不会改写正在恢复的配置。内部恢复握手使用私有编译期分支，
恢复期间公开 async_open_channel 同样返回 command_invalid，且不分配通道编号；
内部恢复建通道使用独立编译期分支。非 open 会话建通道返回 connection_closed。
通道上限检查在编号递增前执行，拒绝不会推动计数器回绕到活动编号。
新连接先清理旧 RPC/确认/投递处理器及接收状态，再从通道 1 分配；旧通道仍由会话代数拒绝，
不能因新连接重用编号而重新有效。拓扑记录独立保留供恢复重建。
普通 connect 直接返回任务，不额外增加包装协程。约定仍是单执行器访问。
`async_close()` 会向当前恢复令牌请求取消，并通过协程等待组等待恢复作用域退出；
30 秒退避阶段的关闭取消已有回归。恢复内部启动的 reader 使用共享完成票据登记，
close 同时等待该 reader 释放连接引用；调用方仍须保留并等待自己启动的恢复任务。
reader 在主动取消前返回的协议失败被保留，close 返回原始协议错误；运行异常则重新抛出。
新连接清除上一会话的完成记录，避免旧错误污染新会话。此约定不替代等待用户自行启动
的 async_run 任务。握手成功到 reader 派发成功之间由作用域回滚守卫接管传输；
派发前分配或同步 posting 失败会关闭传输并传播原始异常，而不返回恢复成功。
握手完成后的 7 个分配位置已做失败注入；这不覆盖 reader 执行期间的全部异常，
也不覆盖真实 TLS 和拓扑阶段的关闭竞争。
拓扑恢复期间调用者取消会传递给内部 reader，RPC 因取消失败时返回 cancelled。
首个拓扑 RPC 等待 reader 显式发布读取所有权，不依赖定时轮询启动。
静默 Channel.Open 对端已有回归。恢复使用临时记录器，失败、异常或取消时以无分配的
共享所有权回滚保留原始记录，全部恢复成功才保留新记录；这不是 broker 端事务回滚。
Channel.Open、交换机、队列、绑定、消费者五个阶段取消后，再次恢复的完整请求顺序
已有脚本 TCP 线帧回归，包括部分成功后的取消。服务端队列名逐会话变化时，绑定和消费者
重映射，以及交换机名/类型、路由键、消费者标签保留已有断言；完整字段表及标志位、
真实 broker 和恢复期间外部并发修改仍待验收。脚本对端在五个恢复阶段返回 Channel.Close
406 时，原始 reply_code/文本/class/method 保留且后续完整恢复已有回归；这不是实际 RabbitMQ 验收。
上述取消/拒绝矩阵的最终恢复还接收脚本 Basic.Deliver + 内容头 + 两段 body，验证原消费者
回调仅触发一次，投递标签/编号、重投标志、交换机、路由、content type 和完整内容保留。

**签名**:
```cpp
class connection_observer {
    virtual void on_state_changed(connection_state) = 0;
    virtual void on_connection_error(const error&) = 0;
};
class protocol_connection : public std::enable_shared_from_this<protocol_connection> {
    explicit protocol_connection(io_context&);
    auto async_connect(connection_options) -> task<result<void>>;
    auto async_connect(connection_options, cancel_token&) -> task<result<void>>;
    auto async_run(cancel_token&) -> task<result<void>>;
    auto async_run_session(cancel_token&, std::function<void()> on_ready) -> task<result<void>>;
    auto async_recover(cancel_token&) -> task<result<void>>;
    auto async_close(std::string reply_text = "client shutdown") -> task<result<void>>;
    auto state() const noexcept -> connection_state;
    auto async_open_channel() -> task<result<std::shared_ptr<logical_channel>>>;
    void observe(std::weak_ptr<connection_observer>);
    void set_return_handler(return_handler);
    void set_recovery_strategy(std::shared_ptr<recovery_strategy>);
};
```

### `logical_channel` — 逻辑通道

`async_consume_acknowledged(consume_options, acknowledged_delivery_handler, field_table = {})`
为显式手动确认入口，拒绝 no_ack 配置及空回调。回调接收 `(const delivery&,
delivery_acknowledgement)`，可保存确认对象并由受监管协程等待 `ack(multiple=false)`
或 `nack(requeue=true, multiple=false)`。任务按值保存身份，不借用临时确认对象。
拓扑重放重新绑定弱连接、会话代数和通道；确认写锁内复核代数。旧消费入口直接调用
原 handler，不修改 delivery 布局。恢复记录新增上下文回调存储，订阅阶段开销仍待测量。
该入口尚未完成单通道关闭失效、NACK 线帧及新分配故障验收，不视为生产闭环完成。
后续脚本回归已覆盖恢复后的 NACK flags=0 和 flags=3，以及整个生命周期停机后
保存对象的 ACK/NACK 被拒绝；不代表单通道关闭或真实 broker 重新入队行为已验证。

`protocol_connection::async_run_session()` 在已连接会话上拥有 reader 和拓扑重放，
通过 when_all 等待两者，不执行连接重试。重放成功后调用 on_ready，取消转发给 reader。
调用方必须等待返回任务；它不是 detached 启动入口。Application 通过 supervisor 拥有该任务，
start 等待重放完成，probe 检查会话 ready。生命周期交换机恢复及延迟确认已有脚本 TCP 回归，
完整订阅、消息确认和真实 broker 的 Application 恢复验收仍未完成。

发布方法帧和内容头在首次写入前编码并校验协商帧上限，成功后才分配确认序号。
消息发送期间持有连接写锁；发送失败中断传输，拒绝后续发布，由帧泵等待活动写操作
结束后释放传输资源并清理待确认记录。应用监管的停机路径会等待帧泵退出；
单独调用 `async_close()` 不能替代等待用户自行启动的 `async_run()` 任务。

**签名**:
```cpp
class logical_channel final {
    auto number() const noexcept -> std::uint16_t;
    auto is_open() const noexcept -> bool;
    auto async_close(std::string) -> task<result<void>>;
    auto async_declare_exchange(exchange_declare_options, field_table = {}) -> task<result<void>>;
    auto async_delete_exchange(std::string, bool if_unused = false, bool no_wait = false) -> task<result<void>>;
    auto async_declare_queue(queue_declare_options, field_table = {}) -> task<result<queue_declare_result>>;
    auto async_bind_queue(binding_options, field_table = {}) -> task<result<void>>;
    auto async_set_qos(qos_options) -> task<result<void>>;
    auto async_publish(publish_options, message) -> task<result<std::uint64_t>>;
    auto async_consume(consume_options, delivery_handler, field_table = {}) -> task<result<std::string>>;
    auto async_ack(std::uint64_t delivery_tag, bool multiple = false) -> task<result<void>>;
    auto async_nack(std::uint64_t, bool multiple = false, bool requeue = true) -> task<result<void>>;
    auto async_enable_confirms(bool no_wait = false) -> task<result<void>>;
    void observe_confirms(std::weak_ptr<publisher_confirm_observer>);
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.amqp091;

auto publish(amqp091_client& client) -> task<void> {
    auto channel = *co_await client.async_open_channel();
    co_await channel->async_enable_confirms();
    amqp091::message msg;
    msg.body = body_from(R"({"orderId":123})");
    msg.content_type = "application/json";
    msg.durable = true;
    auto tag = co_await channel->async_publish(
        {.exchange = "orders.events", .routing_key = "orders.created"},
        std::move(msg));
    co_await channel->async_close();
}
```

### `amqp091_client` — RabbitMQ 兼容客户端

**签名**:
```cpp
class amqp091_client final {
    explicit amqp091_client(io_context&);
    auto async_connect(connection_options) -> task<result<void>>;
    auto async_connect(connection_options, cancel_token&) -> task<result<void>>;
    auto async_open_channel() -> task<result<std::shared_ptr<logical_channel>>>;
    auto async_run(cancel_token&) -> task<result<void>>;
    auto async_recover(cancel_token&) -> task<result<void>>;
    auto async_close() -> task<result<void>>;
    auto state() const noexcept -> connection_state;
    auto connection() const noexcept -> std::shared_ptr<protocol_connection>;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.amqp091;

auto main() -> int {
    namespace cn = cnetmod;
    cn::net_init network;
    auto context = cn::make_io_context();
    amqp091::amqp091_client client(*context);
    cn::spawn(*context, [&]() -> cn::task<void> {
        amqp091::connection_options opts;
        opts.endpoint.host = "127.0.0.1";
        opts.credentials = {.username = "guest", .password = "guest"};
        co_await client.async_connect(std::move(opts));
        auto channel = *co_await client.async_open_channel();
        // 声明交换机、队列、绑定...
        co_await client.async_close();
        context->stop();
    }());
    context->run();
}
```

## Do's & Don'ts

| Do | Don't |
|----|-------|
| 使用 `amqp091_client` 作为入口管理连接和通道 | 直接操作 `protocol_connection` 发送帧 |
| 开启 `async_enable_confirms` 保证消息可靠投递 | 假设 `async_publish` 立即生效——需等待 confirm |
| 配置 `automatic_recovery_strategy` 实现断线自动恢复 | 在通道关闭后继续使用其引用 |
| 为每个消费者设置独立的 `consumer_tag` | 在同一个通道上混用多个消费者而不区分 tag |
| 使用 `async_set_qos` 控制预取数量 | 一次性消费全部消息而不做流控 |

## 连接复用与多 Worker 部署

> **注意**：AMQP 0-9-1 模块为纯客户端实现，不提供 `server_context` 多核模式或内置连接池。生产级部署建议如下。

### Channel 复用（单连接多通道）

AMQP 0-9-1 协议原生支持单连接多通道（multiplexing）。一个 `amqp091_client` 连接可开设多个 `logical_channel`，每个 channel 独立用于发布或消费：

```cpp
import std;
import cnetmod.protocol.amqp091;

auto multi_channel_demo(amqp091::amqp091_client& client) -> task<void> {
    // 发布通道
    auto pub_ch = *co_await client.async_open_channel();
    co_await pub_ch->async_enable_confirms();

    // 消费通道（独立 QoS）
    auto cons_ch = *co_await client.async_open_channel();
    co_await cons_ch->async_set_qos({.prefetch_count = 50});

    // 在 pub_ch 上发布
    amqp091::message msg;
    msg.body = std::vector<std::byte>(std::as_bytes(std::span("hello")));
    co_await pub_ch->async_publish(
        {.exchange = "orders", .routing_key = "new"}, std::move(msg));

    // 在 cons_ch 上消费
    co_await cons_ch->async_consume(
        {.queue = "order_queue", .consumer_tag = "worker-1"},
        [](const amqp091::delivery& d) {
            std::println("Received: {} bytes", d.message.body.size());
        });
}
```

### 多 Worker 消费者部署

每个 worker 创建独立的 `amqp091_client` + `io_context`，在同一消费组（queue）上并行消费：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.amqp091;

namespace cn = cnetmod;

auto consumer_worker(cn::io_context& ctx, const std::string& tag) -> cn::task<void> {
    amqp091::amqp091_client client(ctx);
    amqp091::connection_options opts;
    opts.endpoint.host = "rabbitmq-host";
    opts.credentials = {.username = "guest", .password = "guest"};
    opts.connection_name = tag;
    co_await client.async_connect(std::move(opts));

    auto ch = *co_await client.async_open_channel();
    co_await ch->async_set_qos({.prefetch_count = 20});

    co_await ch->async_consume(
        {.queue = "order_queue", .consumer_tag = tag},
        [&ch](const amqp091::delivery& d) {
            std::println("[{}] tag={} body_size={}", d.consumer_tag,
                d.delivery_tag, d.message.body.size());
            ch->async_ack(d.delivery_tag);
        });

    co_await client.async_run(cn::cancel_token{});
}

auto main() -> int {
    cn::net_init net;
    constexpr unsigned NUM_WORKERS = 4;
    std::vector<std::unique_ptr<cn::io_context>> contexts;
    std::vector<std::jthread> threads;
    for (unsigned i = 0; i < NUM_WORKERS; ++i) {
        auto& ctx = contexts.emplace_back(cn::make_io_context());
        auto tag = std::format("worker-{}", i);
        cn::spawn(*ctx, consumer_worker(*ctx, tag));
        threads.emplace_back([&ctx] { ctx->run(); });
    }
    for (auto& t : threads) t.join();
    return 0;
}
```

### Do's & Don'ts（连接复用）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 单连接开设多 channel 分别用于发布和消费 | 所有操作都挤在同一个 channel |
| 每个 worker 独立 `amqp091_client` + 独立 `io_context` | 多线程共享同一个 `amqp091_client` |
| 配置 `prefetch_count` 做流控 | 不设 QoS 导致一次性拉取全部消息 |
| 开启 `async_enable_confirms` 确保发布可靠 | 假设 `async_publish` 立即生效 |

---

## 参考示例

- `examples/amqp091/amqp091_demo.cpp` — 完整的发布+消费应用入口
- `examples/amqp091/publisher_service.hpp` — 并发发布者，支持 publisher confirm
- `examples/amqp091/listener_container.hpp` — 消费者容器，QoS 预取+手动 ACK
- `examples/amqp091/amqp091_application.hpp` — 拓扑声明与应用生命周期
- `examples/amqp091/amqp091_config.hpp` — 环境变量配置
