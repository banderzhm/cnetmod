# AMQP 1.0 协议模块

> 完整的 AMQP 1.0 协议客户端，支持会话、发送/接收链路、事务控制与 SASL 认证。

**import**: `import cnetmod.protocol.amqp10;`
**CMake**: `-DCNETMOD_ENABLE_AMQP10=ON`
**源码**: `src/protocol/amqp10/`

## 场景导航

| 场景 | 关键类型 |
|------|---------|
| 连接 Broker | `client`, `client_options`, `client_configuration` |
| 会话管理 | `session`, `session_options` |
| 发送/接收消息 | `sender_link`, `receiver_link`, `message` |
| 事务控制 | `transaction_controller` |
| SASL 认证 | `sasl_negotiator`, `credentials` |
| 传输层 | `socket_transport`, `transport_frame_codec` |
| 重连与恢复 | `reconnect_policy`, `recovery_observer` |
| 状态/错误 | `connection_state`, `error`, `errc` |

## API 参考

### `client_configuration` — 连接配置

**签名**:
```cpp
namespace cnetmod::amqp10 {
enum class authentication_mechanism {
    anonymous, plain, external, scram_sha_256, scram_sha_512, oauth_bearer
};
struct credentials {
    authentication_mechanism mechanism = authentication_mechanism::plain;
    std::string username, password, token;
};
struct endpoint {
    std::string host = "127.0.0.1"; std::uint16_t port = 5672;
    std::chrono::milliseconds connect_timeout{10000}; tls_options tls;
};
}
```

**示例**:
```cpp
import std;
import cnetmod.protocol.amqp10;

amqp10::credentials cred;
cred.mechanism = amqp10::authentication_mechanism::plain;
cred.username = "admin"; cred.password = "secret";
```

### `client` — AMQP 客户端

**签名**:
```cpp
struct client_options {
    endpoint endpoint; credentials credentials;
    std::string container_id, hostname;
    std::uint32_t max_frame_size = 262144; std::uint16_t channel_max = 65535;
    std::chrono::milliseconds idle_timeout{60000};
    std::shared_ptr<const reconnect_policy> reconnect;
    bool recover_sessions = true;
};
class client {
    explicit client(io_context&);
    auto connect(client_options, cancel_token&) -> task<std::expected<void, error>>;
    auto reconnect(cancel_token&) -> task<std::expected<void, error>>;
    auto make_session(session_options = {}) -> std::expected<session, error>;
    auto close(cancel_token&) -> task<std::expected<void, error>>;
    void on_state_change(state_handler);
    void on_disconnect(disconnect_handler);
    auto state() const noexcept -> connection_state;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.amqp10;

auto main() -> int {
    namespace cn = cnetmod;
    cn::net_init network;
    auto context = cn::make_io_context();
    amqp10::client client(*context);
    cn::cancel_token token;
    cn::spawn(*context, [&]() -> cn::task<void> {
        amqp10::client_options opts;
        opts.endpoint = {.host = "127.0.0.1"};
        opts.credentials = {.username = "guest", .password = "guest"};
        opts.container_id = "my-app";
        co_await client.connect(std::move(opts), token);
        auto session = *client.make_session();
        co_await session.begin(token);
        co_await client.close(token);
        context->stop();
    }());
    context->run();
}
```

### `session` — AMQP 会话

**签名**:
```cpp
struct session_options {
    std::uint32_t incoming_window = 2048, outgoing_window = 2048, handle_max = 65535;
};
struct sender_options {
    std::string name; target target_terminus;
    sender_settle_mode sender_settlement = sender_settle_mode::mixed;
};
struct receiver_options {
    std::string name; source source_terminus;
    sender_settle_mode sender_settlement = sender_settle_mode::mixed;
};
class session {
    auto begin(cancel_token&) -> task<std::expected<void, error>>;
    auto make_sender(sender_options) -> std::expected<sender_link, error>;
    auto make_receiver(receiver_options) -> std::expected<receiver_link, error>;
    auto make_transaction_controller() -> std::expected<transaction_controller, error>;
    auto end(cancel_token&) -> task<std::expected<void, error>>;
    auto state() const noexcept -> session_state;
};
```

### `sender_link` — 发送链路

**签名**:
```cpp
struct send_options { bool settled; bool batchable; std::optional<binary> transaction_id; };
struct send_result { std::uint32_t delivery_id; delivery_outcome outcome; };
class sender_link {
    auto attach(cancel_token&) -> task<std::expected<void, error>>;
    auto begin_send(const message&, send_options, cancel_token&)
        -> task<std::expected<std::uint32_t, error>>;
    auto await_outcome(std::uint32_t, cancel_token&)
        -> task<std::expected<send_result, error>>;
    auto send(const message&, send_options, cancel_token&)
        -> task<std::expected<send_result, error>>;
    auto detach(bool close_link, cancel_token&) -> task<std::expected<void, error>>;
    auto credit() const noexcept -> std::uint32_t;
    auto pending_unsettled_count() const noexcept -> std::size_t;
};
```

**示例**:
```cpp
auto send_orders(session& sess, cancel_token& token) -> task<void> {
    auto link = *sess.make_sender({.name = "sender", .target_terminus = {.address = "orders"}});
    co_await link.attach(token);
    amqp10::message msg;
    msg.properties.emplace();
    msg.properties->content_type = "application/json";
    msg.body = amqp10::value{std::string(R"({"orderId":1})")};
    auto result = co_await link.send(msg, {.settled = false}, token);
    if (result->outcome.kind == amqp10::outcome_kind::accepted)
        std::println("Accepted");
    co_await link.detach(true, token);
}
```

### `receiver_link` — 接收链路

**签名**:
```cpp
struct received_message {
    std::uint32_t delivery_id; binary delivery_tag;
    message payload; bool settled, resumed;
};
class receiver_link {
    auto attach(std::uint32_t initial_credit, cancel_token&) -> task<std::expected<void, error>>;
    auto receive(cancel_token&) -> task<std::expected<received_message, error>>;
    auto add_credit(std::uint32_t credit, bool drain, cancel_token&) -> task<std::expected<void, error>>;
    auto settle(std::uint32_t delivery_id, delivery_outcome, cancel_token&)
        -> task<std::expected<void, error>>;
    auto detach(bool close_link, cancel_token&) -> task<std::expected<void, error>>;
    auto credit() const noexcept -> std::uint32_t;
};
```

**示例**:
```cpp
auto receive_orders(session& sess, cancel_token& token) -> task<void> {
    auto link = *sess.make_receiver({.name = "recv", .source_terminus = {.address = "orders"}});
    co_await link.attach(256, token);
    while (!token.is_cancelled()) {
        auto d = co_await link.receive(token);
        co_await link.settle(d->delivery_id,
            {.kind = amqp10::outcome_kind::accepted}, token);
        if (link.credit() < 128)
            co_await link.add_credit(256, false, token);
    }
}
```

### `message_section` — 消息结构

**签名**:
```cpp
struct header_section { bool durable; std::uint8_t priority = 4; std::optional<std::chrono::milliseconds> ttl; };
struct properties_section {
    std::optional<value> message_id, correlation_id;
    std::string to, subject, reply_to, content_type, group_id;
};
using annotations = std::map<symbol, value, std::less<>>;
using application_properties = std::map<std::string, value, std::less<>>;
using message_body = std::variant<binary, value, std::vector<list>>;
struct message {
    std::optional<header_section> header;
    annotations delivery_annotations, message_annotations;
    std::optional<properties_section> properties;
    application_properties application;
    message_body body = binary{};
    annotations footer;
};
auto encode_message(const message&) -> binary;
auto decode_message(std::span<const std::byte>) -> std::expected<message, std::error_code>;
```

### `described_value` / `primitive_value` — 类型系统

**签名**:
```cpp
using binary = std::vector<std::byte>;
using timestamp = std::chrono::milliseconds;
struct symbol { std::string text; /* 隐式转换构造 */ };
struct descriptor { std::variant<std::uint64_t, symbol> value; };
struct described_value { descriptor type; std::shared_ptr<value> body; };
struct value {
    using storage = std::variant<std::monostate, bool, std::uint8_t, ...,
        std::string, symbol, std::shared_ptr<list>, std::shared_ptr<map>,
        std::shared_ptr<described_value>>;
    storage data;
    static auto make_list(list) -> value;
    static auto make_map(map) -> value;
    static auto described(descriptor, value) -> value;
};
```

### `delivery_state` — 投递状态与终结点

**签名**:
```cpp
enum class sender_settle_mode : std::uint8_t { unsettled = 0, settled = 1, mixed = 2 };
enum class receiver_settle_mode : std::uint8_t { first = 0, second = 1 };
struct source { std::string address; terminus_durability durable; expiry_policy expiry; };
struct target { std::string address; terminus_durability durable; expiry_policy expiry; };
enum class outcome_kind { accepted, rejected, released, modified, transactional };
struct delivery_outcome {
    outcome_kind kind = outcome_kind::accepted;
    std::optional<error_condition> error;
    bool delivery_failed, undeliverable_here;
};
```

### `transaction_controller` — 事务控制器

**签名**:
```cpp
class transaction_controller {
    auto declare(cancel_token&) -> task<std::expected<binary, error>>;
    auto discharge(std::span<const std::byte> transaction_id, bool fail, cancel_token&)
        -> task<std::expected<void, error>>;
};
```

### `sasl_negotiator` — SASL 认证

**签名**:
```cpp
enum class sasl_code : std::uint8_t { ok = 0, auth = 1, sys = 2, sys_permanent = 3, sys_temporary = 4 };
class sasl_negotiator {
    explicit sasl_negotiator(credentials);
    auto select(std::span<const symbol> offered, std::string_view hostname)
        -> std::expected<sasl_init, error>;
    auto respond(std::span<const std::byte> challenge) -> std::expected<sasl_response, error>;
    auto finish(const sasl_outcome&) -> std::expected<void, error>;
};
auto encode_sasl_performative(const sasl_performative&) -> binary;
auto decode_sasl_performative(std::span<const std::byte>) -> std::expected<sasl_performative, std::error_code>;
```

### `socket_transport` / `transport_frame_codec` — 传输层

**签名**:
```cpp
class socket_transport {
    explicit socket_transport(io_context&);
    auto connect(const endpoint&, cancel_token&) -> task<std::expected<void, error>>;
    auto write_frame(const frame&, cancel_token&) -> task<std::expected<void, error>>;
    auto read_frame(std::uint32_t maximum_size, cancel_token&) -> task<std::expected<frame, error>>;
    void close() noexcept;
};
struct frame { frame_type type; std::uint16_t channel; binary body; };
auto encode_frame(const frame&) -> binary;
auto decode_frame(std::span<const std::byte>, std::uint32_t) -> std::expected<frame, std::error_code>;
```

### `reconnect_policy` / `recovery_observer` — 重连与恢复

**签名**:
```cpp
class reconnect_policy {
    virtual auto next_delay(const reconnect_context&) const -> std::optional<std::chrono::milliseconds> = 0;
};
class exponential_backoff final : public reconnect_policy {
    explicit exponential_backoff(std::chrono::milliseconds initial = std::chrono::seconds(1),
        std::chrono::milliseconds maximum = std::chrono::seconds(60),
        double multiplier = 2.0, std::size_t maximum_attempts = 0) noexcept;
};
class recovery_observer {
    virtual auto recovery_order() const noexcept -> std::uint8_t = 0;
    virtual auto recover(cancel_token&) -> task<std::expected<void, error>> = 0;
};
```

### 状态枚举 / 错误类型

**签名**:
```cpp
enum class connection_state { idle, connecting, sasl, opening, opened, closing, closed, failed };
enum class session_state { unmapped, begin_sent, mapped, end_sent, ended };
enum class link_state { detached, attach_sent, attached, detach_sent, closed };
enum class error_stage { configuration, transport, authentication, protocol, transaction, cancelled, ... };
struct error { error_stage stage; std::error_code code; std::string message; bool retryable; };
enum class errc { invalid_field, malformed_frame, idle_timeout, delivery_rejected, cancelled, ... };
auto make_error(error_stage, errc, std::string, bool retryable = false) -> error;
```

### `performative_codec` / `performative_channel` / `amqp_value_codec`

**签名**:
```cpp
auto encode_performative(const performative&) -> binary;
auto decode_performative(std::span<const std::byte>) -> std::expected<performative, std::error_code>;
class performative_channel {
    virtual auto send(std::uint16_t, const performative&, cancel_token&)
        -> task<std::expected<void, error>> = 0;
    virtual auto receive(std::uint16_t, cancel_token&) -> task<std::expected<performative, error>> = 0;
};
class encoder {
    void write_value(const value&); auto release() -> binary;
};
class decoder {
    explicit decoder(std::span<const std::byte>) noexcept;
    auto read_value() -> std::expected<value, std::error_code>;
    auto remaining() const noexcept -> std::size_t;
};
```

## Do's & Don'ts

| Do | Don't |
|----|-------|
| 通过 `client::make_session` 创建会话 | 直接构造 `session` 对象 |
| 使用 `send()` 一次性等待投递结果 | 忘记在高吞吐场景补充 `credit` |
| 配置 `reconnect_policy` 实现自动重连 | 在 `close` 后继续使用 link/session |
| 使用 `transaction_controller` 管理分布式事务 | 假设链路 attach 后立即有 credit |
| 通过 `recovery_observer` 实现链路级恢复 | 忽略 `delivery_outcome` 中的错误条件 |

## 连接/Session 复用与多 Worker 部署

> **注意**：AMQP 1.0 模块为纯客户端实现，不提供 `server_context` 多核模式或内置连接池。生产级部署建议如下。

### Session / Link 复用（单连接多会话）

AMQP 1.0 协议原生支持单连接多 session，每个 session 可开设多个 sender/receiver link：

```cpp
import std;
import cnetmod.protocol.amqp10;

auto multi_session_demo(amqp10::client& client, cn::cancel_token& token) -> task<void> {
    // Session 1: 发送链路
    auto sess1 = *client.make_session();
    co_await sess1.begin(token);
    auto sender = *sess1.make_sender({
        .name = "order-sender",
        .target_terminus = {.address = "orders"}
    });
    co_await sender.attach(token);

    // Session 2: 接收链路（独立窗口）
    auto sess2 = *client.make_session({.incoming_window = 4096, .outgoing_window = 4096});
    co_await sess2.begin(token);
    auto receiver = *sess2.make_receiver({
        .name = "order-receiver",
        .source_terminus = {.address = "orders"}
    });
    co_await receiver.attach(256, token);

    // 发送
    amqp10::message msg;
    msg.body = amqp10::value{std::string(R"({"orderId":1})")};
    co_await sender.send(msg, {.settled = false}, token);

    // 接收
    auto d = co_await receiver.receive(token);
    co_await receiver.settle(d->delivery_id,
        {.kind = amqp10::outcome_kind::accepted}, token);
}
```

### 多 Worker 部署

每个 worker 创建独立的 `amqp10::client` + `io_context`，用于并行消费或发送：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.amqp10;

namespace cn = cnetmod;

auto amqp_worker(cn::io_context& ctx, const std::string& worker_id) -> cn::task<void> {
    amqp10::client client(ctx);
    cn::cancel_token token;

    amqp10::client_options opts;
    opts.endpoint = {.host = "amqp-broker"};
    opts.credentials = {.username = "admin", .password = "secret"};
    opts.container_id = worker_id;
    opts.recover_sessions = true;
    opts.reconnect = std::make_shared<amqp10::exponential_backoff>(
        std::chrono::seconds(1), std::chrono::seconds(30), 2.0, 10);

    co_await client.connect(std::move(opts), token);

    auto sess = *client.make_session();
    co_await sess.begin(token);

    auto receiver = *sess.make_receiver({
        .name = std::format("{}-recv", worker_id),
        .source_terminus = {.address = "jobs"}
    });
    co_await receiver.attach(128, token);

    while (!token.is_cancelled()) {
        auto d = co_await receiver.receive(token);
        if (!d) break;
        std::println("[{}] received delivery_id={}", worker_id, d->delivery_id);
        co_await receiver.settle(d->delivery_id,
            {.kind = amqp10::outcome_kind::accepted}, token);
        if (receiver.credit() < 64)
            co_await receiver.add_credit(128, false, token);
    }

    co_await client.close(token);
}

auto main() -> int {
    cn::net_init net;
    constexpr unsigned NUM_WORKERS = 4;
    std::vector<std::unique_ptr<cn::io_context>> contexts;
    std::vector<std::jthread> threads;
    for (unsigned i = 0; i < NUM_WORKERS; ++i) {
        auto& ctx = contexts.emplace_back(cn::make_io_context());
        auto id = std::format("worker-{}", i);
        cn::spawn(*ctx, amqp_worker(*ctx, id));
        threads.emplace_back([&ctx] { ctx->run(); });
    }
    for (auto& t : threads) t.join();
    return 0;
}
```

### Do's & Don'ts（连接复用）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 单连接开设多 session 分别用于发送和接收 | 所有操作挤在同一个 session |
| 每个 worker 独立 `client` + 独立 `io_context` | 多线程共享同一个 `amqp10::client` |
| 配置 `reconnect_policy` 实现自动重连 | 不做重连导致网络抖动后永久断连 |
| 定期补充 `credit` 维持接收流控 | 忽略 credit 耗尽导致接收停止 |

---

## 参考示例

- `examples/amqp10/amqp10_demo.cpp` — 完整的发送+接收应用入口
- `examples/amqp10/sender_service.hpp` — 并发发送者，session/link 生命周期管理
- `examples/amqp10/receiver_container.hpp` — 接收者容器，信用流控+手动 settle
- `examples/amqp10/amqp10_application.hpp` — 应用生命周期编排
