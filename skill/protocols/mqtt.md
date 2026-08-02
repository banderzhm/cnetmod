# MQTT

> MQTT v3.1.1 / v5.0 完整实现，包含 Broker、异步/同步客户端、主题过滤、保留消息、共享订阅、会话持久化及安全 ACL。

**import**: `import cnetmod.protocol.mqtt;`
**CMake**: `-DCNETMOD_ENABLE_MQTT=ON`
**依赖**: `cnetmod.io.io_context`、`cnetmod.coro.task`、`cnetmod.coro.channel`、`nlohmann.json`（安全配置）
**源码**: `src/protocol/mqtt/`

## 场景导航

- 我要做 Broker 服务 → [看这里](#场景1broker)
- 我要做异步/同步客户端 → [看这里](#场景2client-与-sync_client)
- 我要做保留消息/共享订阅 → [看这里](#场景3快速示例)

## 核心类型

**`mqtt::protocol_version`** — 协议版本：`v3_1_1(4)`、`v5(5)`

**`mqtt::qos`** — 服务质量：`at_most_once(0)`、`at_least_once(1)`、`exactly_once(2)`

**`mqtt::control_packet_type`** — 控制包类型：`connect(0x10)`、`publish(0x30)`、`subscribe(0x80)`、`pingreq(0xC0)`、`disconnect(0xE0)` 等

**`mqtt::connect_return_code`** (v3.1.1) / **`mqtt::v5::connect_reason_code`** (v5) — 连接应答码

**`mqtt::property_id`** (v5) — 属性 ID：`session_expiry_interval`、`topic_alias`、`user_property`、`message_expiry_interval`、`response_topic` 等

**`mqtt::mqtt_property`** — v5 属性值，支持 `byte_prop`、`u16_prop`、`u32_prop`、`string_prop`、`string_pair_prop` 工厂方法

**`mqtt::will`** — 遗嘱消息：`topic`、`message`、`qos_value`、`retain`、`props`

**`mqtt::subscribe_entry`** — 订阅项：`topic_filter`、`max_qos`、v5 选项 (`no_local`、`retain_as_published`、`subscription_id`)

**`mqtt::publish_message`** — 接收到的发布消息：`topic`、`payload`（`binary_data`）、`qos_value`、`retain`、`dup`、`packet_id`、`props`

**`mqtt::connect_options`** — 连接选项：`host`、`port`、`client_id`、`clean_session`、`keep_alive_sec`、`username`、`password`、`will_msg`、`version`、`props`、TLS 选项

**`mqtt::mqtt_errc`** — 错误码：`malformed_packet`、`not_connected`、`connect_timeout` 等

## API 参考

### 编解码

```cpp
auto encode_connect(const connect_options& options) -> std::string;
auto encode_publish(std::string_view topic, std::string_view payload,
    qos quality_of_service, bool retain, bool duplicate, std::uint16_t packet_id,
    protocol_version version, const properties& properties_to_encode = {}) -> std::string;
auto decode_publish(std::string_view payload, std::uint8_t flags, protocol_version version)
    -> std::expected<publish_message, std::string>;
auto encode_subscribe(std::uint16_t packet_id, const std::vector<subscribe_entry>& entries,
    protocol_version version, const properties& = {}) -> std::string;
auto encode_pingreq() -> std::string;
auto encode_disconnect(protocol_version version, std::uint8_t reason_code = 0, const properties& = {}) -> std::string;
```

### 增量帧解析器

```cpp
class mqtt_parser {
    mqtt_parser();
    void feed(std::string_view data);
    auto next() -> std::optional<mqtt_frame>;  // 返回完整帧或 nullopt
    void reset();
    auto pending() const noexcept -> std::size_t;
};

struct mqtt_frame {
    control_packet_type type; std::uint8_t flags; std::string payload;
};
```

### 主题过滤

```cpp
constexpr auto validate_topic_filter(std::string_view filter) noexcept -> bool;
constexpr auto validate_topic_name(std::string_view name) noexcept -> bool;
auto topic_matches(std::string_view filter, std::string_view name) noexcept -> bool;
constexpr auto has_wildcards(std::string_view filter) noexcept -> bool;
```

### Topic Alias (v5)

```cpp
class topic_alias_send {
    explicit topic_alias_send(std::uint16_t max_alias = 0) noexcept;
    auto allocate(std::string_view topic) -> std::pair<std::uint16_t, bool>; // (alias, is_new)
    auto find_by_alias(std::uint16_t alias) const -> std::string;
};
class topic_alias_recv {
    void insert_or_update(std::string_view topic, std::uint16_t alias);
    auto resolve(std::string_view topic, std::uint16_t alias) -> std::string;
};
```

### `mqtt::client` — 异步客户端

```cpp
explicit client(io_context& ctx) noexcept;
auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>>;
auto publish(std::string_view topic, std::string_view payload,
    qos q = qos::at_most_once, bool retain = false, const properties& props = {})
    -> task<std::expected<void, std::string>>;
auto subscribe(std::vector<subscribe_entry> entries, const properties& props = {})
    -> task<std::expected<std::vector<std::uint8_t>, std::string>>;
auto subscribe(std::string topic_filter, qos max_qos = qos::at_most_once, const properties& props = {})
    -> task<std::expected<std::vector<std::uint8_t>, std::string>>;
auto unsubscribe(std::vector<std::string> topic_filters, const properties& props = {})
    -> task<std::expected<void, std::string>>;
auto disconnect(std::uint8_t reason_code = 0, const properties& props = {})
    -> task<std::expected<void, std::string>>;
void on_message(message_callback cb);
void on_disconnect(disconnect_callback cb);
void set_reconnect(reconnect_options opts);
auto is_connected() const noexcept -> bool;
auto session_present() const noexcept -> bool;
auto version() const noexcept -> protocol_version;
```

**`reconnect_options`**: `enabled`、`max_retries`、`initial_delay`、`max_delay`、`backoff_multiplier`、`restore_subscriptions`

### `mqtt::sync_client` — 同步客户端

```cpp
explicit sync_client();
auto connect_sync(connect_options opts = {}) -> std::expected<void, std::string>;
auto publish_sync(std::string_view topic, std::string_view payload,
    qos q = qos::at_most_once, bool retain = false, const properties& props = {})
    -> std::expected<void, std::string>;
auto subscribe_sync(std::string topic_filter, qos max_qos = qos::at_most_once, const properties& props = {})
    -> std::expected<std::vector<std::uint8_t>, std::string>;
auto unsubscribe_sync(std::vector<std::string> topic_filters, const properties& props = {})
    -> std::expected<void, std::string>;
auto disconnect_sync(std::uint8_t reason_code = 0, const properties& props = {}) -> std::expected<void, std::string>;
void on_message(message_callback cb);
void poll();
```

### `mqtt::broker` — Broker

```cpp
explicit broker(io_context& ctx);
explicit broker(server_context& sctx); // 多核
void set_options(broker_options opts);
void set_security(security_config cfg);
void set_publish_observer(publish_observer observer);
auto listen(std::string_view host, std::uint16_t port, socket_options opts = ...) -> std::expected<void, std::error_code>;
auto run() -> task<void>;
void stop();
auto sessions() noexcept -> session_store&;
auto retained() noexcept -> retained_store&;
auto subscriptions() noexcept -> subscription_map&;
auto metrics() const noexcept -> broker_metrics_snapshot;
```

**`broker_options`**: `port`、`host`、`max_connections`、`topic_alias_maximum`、`receive_maximum`、TLS 配置、`persistence_enabled`

### Retained / Subscription / Shared / Security / Persistence / WS Transport

```cpp
// retained_store — 保留消息存储
void store(const std::string& topic, retained_message msg);
auto match(std::string_view topic_filter) const -> std::vector<retained_message>;

// subscription_map — Trie 订阅匹配
void insert(const std::string& topic_filter, const std::string& client_id, const subscribe_entry& entry);
auto match(std::string_view topic) const -> std::vector<subscription_entry_ref>;

// shared_target_store — 共享订阅轮询
void add_member(std::string_view share_name, std::string_view filter, const std::string& client_id);
auto select_target(std::string_view share_name, std::string_view filter) -> std::string;

// security_config — 认证 + ACL
void add_user(const std::string& username, const std::string& password, std::vector<std::string> groups = {});
void allow_all(const std::string& topic_filter, std::set<std::string> groups = {});
auto authenticate(std::string_view username, std::string_view password) const -> std::optional<std::string>;
auto load_file(const std::string& path) -> std::expected<void, std::string>;

// persistence — 会话 + 保留消息持久化
auto save_sessions(const session_store& store) -> std::expected<void, std::string>;
auto load_sessions() -> std::expected<session_store, std::string>;
auto start_auto_flush(io_context& ctx, session_store& sessions, retained_store& retained) -> task<void>;

// ws_broker — MQTT over WebSocket
class ws_broker {
    explicit ws_broker(io_context& ctx);
    void set_options(ws_broker_options opts); // port=8083, path="/mqtt"
    auto listen(std::string_view host, std::uint16_t port, socket_options opts = ...) -> std::expected<void, std::error_code>;
    auto run() -> task<void>;
};
```

## v3.1.1 vs v5.0

| 特性 | v3.1.1 | v5.0 |
|---|---|---|
| 属性系统 | 无 | ✅ `property_id` + `mqtt_property` |
| Topic Alias | ❌ | ✅ |
| 共享订阅 | ❌ | ✅ `$share/group/filter` |
| 消息过期 | ❌ | ✅ `message_expiry_interval` |
| 连接应答码 | `connect_return_code` | `v5::connect_reason_code` |

## 场景 1：broker

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mqtt;

namespace mqtt = cnetmod::mqtt;

int main() {
    auto ctx = cnetmod::make_io_context();
    mqtt::broker brk(*ctx);
    brk.set_options({.port = 1883, .host = "0.0.0.0", .topic_alias_maximum = 10});
    auto& sec = brk.security();
    sec.add_user("admin", "pass", {"admin"});
    sec.allow_all("#", {"admin"});
    brk.listen("0.0.0.0", 1883);
    cnetmod::spawn(*ctx, brk.run());
    ctx->run();
}
```

## 场景 2：client / sync_client

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mqtt;

namespace mqtt = cnetmod::mqtt;

auto async_demo(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    mqtt::client cli(ctx);
    cli.on_message([](const mqtt::publish_message& msg) {
        std::println("{}: {}", msg.topic, msg.payload.str());
    });
    co_await cli.connect({.host = "127.0.0.1", .client_id = "c1",
        .username = "admin", .password = "pass", .version = mqtt::protocol_version::v5});
    co_await cli.subscribe("sensor/+/data", mqtt::qos::at_least_once);
    co_await cli.publish("sensor/temp/data", "22.5°C", mqtt::qos::at_least_once);
    co_await cli.disconnect();
}

void sync_demo() {
    mqtt::sync_client sc;
    sc.connect_sync({.host = "127.0.0.1", .client_id = "sync-1", .username = "admin", .password = "pass"});
    sc.on_message([](const mqtt::publish_message& msg) {
        std::println("recv: {} = {}", msg.topic, msg.payload.str());
    });
    sc.subscribe_sync("test/#", mqtt::qos::at_least_once);
    sc.publish_sync("test/hello", "world", mqtt::qos::at_least_once);
    for (int i = 0; i < 50; ++i) { sc.poll(); }
    sc.disconnect_sync();
}
```

## 场景 3：快速示例（Retained + Shared）

```cpp
import std;
import cnetmod.protocol.mqtt;

namespace mqtt = cnetmod::mqtt;

// Retained 消息
auto retained_demo(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    mqtt::client pub(ctx);
    co_await pub.connect({.host = "127.0.0.1", .client_id = "pub", .username = "admin", .password = "pass"});
    co_await pub.publish("status/server", "online", mqtt::qos::at_least_once, true); // retain=true
    // 删除：空 payload + retain
    co_await pub.publish("status/server", "", mqtt::qos::at_most_once, true);
    co_await pub.disconnect();
}

// 共享订阅 + ACL
void setup_security(mqtt::broker& brk) {
    auto& sec = brk.security();
    sec.add_user("alice", "pass123", {"admin"});
    sec.allow_all("#", {"admin"});
}

auto shared_sub(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    mqtt::client s1(ctx), s2(ctx);
    auto opts = mqtt::connect_options{.host = "127.0.0.1", .username = "alice", .password = "pass123",
        .version = mqtt::protocol_version::v5};
    opts.client_id = "worker-1"; co_await s1.connect(opts);
    opts.client_id = "worker-2"; co_await s2.connect(opts);
    co_await s1.subscribe("$share/workers/job/+", mqtt::qos::at_least_once);
    co_await s2.subscribe("$share/workers/job/+", mqtt::qos::at_least_once);
}
```

## Do's & Don'ts

| Do | Don't |
|---|---|
| 生产环境启用 ACL 认证 | 不要允许匿名访问 |
| 使用 `clean_session=false` 实现离线队列 | 不要在高频场景用 QoS 2（开销大） |
| 保留消息适合状态发布 | 不要用 retain 传输临时数据 |
| v5 使用 `message_expiry_interval` 防过期数据 | 不要假设 retain 消息立即到达 |
| 共享订阅分摊负载 | 不要在 v3.1.1 使用 `$share/` |

## 多核 Broker 部署（生产级用法）

### `broker(server_context&)` — 多核模式

MQTT broker 支持 `server_context` 构造，自动使用多 worker 线程处理客户端连接。

**架构**：
| 线程 | 角色 | 说明 |
|------|------|------|
| Thread 0（main） | `accept_io()` | 专用 accept 循环 |
| Thread 1..N | `next_worker_io()` | 每个 worker 独立处理 MQTT 客户端 I/O |

### 多核 Broker 完整示例

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.mqtt;

namespace cn = cnetmod;
namespace mqtt = cnetmod::mqtt;

auto main() -> int {
    cn::net_init net;

    // 创建多核上下文
    constexpr unsigned WORKERS = 4;
    cn::server_context sctx(WORKERS, WORKERS);

    // 构造多核 broker
    mqtt::broker brk(sctx);

    mqtt::broker_options opts;
    opts.port = 1883;
    opts.host = "0.0.0.0";
    opts.max_connections = 50000;
    opts.topic_alias_maximum = 30;
    opts.receive_maximum = 65535;
    opts.persistence_enabled = true;
    opts.persistence = {.data_dir = "/var/lib/mqtt", .flush_interval = std::chrono::seconds(30)};
    brk.set_options(opts);

    // 配置安全 ACL
    auto& sec = brk.security();
    sec.add_user("admin", "pass", {"admin"});
    sec.add_user("device", "dev-pass", {"devices"});
    sec.allow_all("#", {"admin"});
    sec.allow_all("sensor/+/data", {"devices"});
    sec.allow_all("cmd/+/exec", {"devices"});

    // 监听
    auto lr = brk.listen("0.0.0.0", 1883);
    if (!lr) {
        std::println("Listen failed: {}", lr.error().message());
        return 1;
    }

    // 在 accept_io 上启动 broker
    cn::spawn(sctx.accept_io(), brk.run());

    std::println("Multi-core MQTT broker on 0.0.0.0:1883 ({} workers)", WORKERS);

    // 阻塞运行
    sctx.run();
    return 0;
}
```

---

## 持久化配置（生产级用法）

### `persistence` — 会话与保留消息持久化

**签名**（源码 `persistence_store.cppm`）：
```cpp
struct persistence_options {
    std::string data_dir = "./mqtt_data";
    std::chrono::seconds flush_interval{30};
};

class persistence {
    explicit persistence(persistence_options opts = {});
    auto save_sessions(const session_store& store) -> std::expected<void, std::string>;
    auto load_sessions() -> std::expected<session_store, std::string>;
    auto save_retained(const retained_store& store) -> std::expected<void, std::string>;
    auto load_retained() -> std::expected<retained_store, std::string>;
    auto start_auto_flush(io_context& ctx, session_store& sessions,
        retained_store& retained) -> task<void>;
    [[nodiscard]] auto options() const noexcept -> const persistence_options&;
};
```

### 通过 `broker_options` 启用持久化

在 `broker_options` 中设置 `persistence_enabled = true` 和 `persistence` 字段，broker 自动在启动时加载已保存的会话和保留消息，并定期自动 flush。

```cpp
mqtt::broker_options opts;
opts.persistence_enabled = true;
opts.persistence = {
    .data_dir = "/var/lib/mqtt",        // 持久化数据目录
    .flush_interval = std::chrono::seconds(30) // 自动刷盘间隔
};
```

### Do's & Don'ts（多核 + 持久化）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用 `server_context` 构造 broker 启用多核 | 在单线程 `io_context` 上处理大量并发连接 |
| 配置 `persistence_enabled` 防止重启丢失会话 | 依赖内存会话不做持久化 |
| 设置合理的 `flush_interval` 平衡性能和数据安全 | flush 过于频繁影响写入性能 |
| 配合 `clean_session=false` 使用离线消息队列 | 客户端都设 `clean_session=true` 导致离线消息丢失 |

---

## 参考示例

- `examples/mqtt/mqtt_demo.cpp` — Broker + Client：QoS/Retained/Will/Sync/Reconnect/ACL/Shared/v5 Properties
- `testing/bench/bench_mqtt.cpp` — 多核 broker 基准测试（server_context 多 worker）
