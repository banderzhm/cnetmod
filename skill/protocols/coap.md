# CoAP

> RFC 7252 CoAP 协议实现，支持 UDP 单播/多播、Observe 观察模式、Block 分块传输及 DTLS 安全通信。

**import**: `import cnetmod.protocol.coap;`
**CMake**: `-DCNETMOD_ENABLE_COAP=ON`
**源码**: `src/protocol/coap/`

## 场景导航

- 我要发送 CoAP GET/POST 请求 → [看这里](#场景客户端请求)
- 我要搭建 CoAP 服务端 → [看这里](#场景服务端路由)
- 我要观察资源变化（Observe） → [看这里](#场景observe-观察模式)
- 我要传输大数据（Block Transfer） → [看这里](#场景block-分块传输)
- 我要使用多播发现设备 → [看这里](#场景多播)
- 我要启用 DTLS 加密 → [看这里](#场景coaps-dtls-安全通信)

## API 参考

### CoAP 类型

**签名**: `export enum class message_type : std::uint8_t`

| 值 | 说明 |
|---|------|
| `confirmable` | 需要确认（CON） |
| `non_confirmable` | 无需确认（NON） |
| `acknowledgement` | 确认（ACK） |
| `reset` | 重置（RST） |

**签名**: `export enum class method : std::uint8_t` — `get`, `post`, `put`, `delete_`, `fetch`, `patch`, `ipatch`

**签名**: `export enum class response_code : std::uint8_t` — `created`(2.01), `content`(2.05), `bad_request`(4.00), `not_found`(4.04) 等

**签名**: `export enum class option_number : std::uint16_t` — `uri_path`(11), `content_format`(12), `observe`(6), `block1`(27), `block2`(23) 等

**签名**: `export enum class content_format : std::uint16_t` — `text_plain`(0), `json`(50), `cbor`(60), `octet_stream`(42) 等

### `message` — CoAP 消息

**签名**: `export struct message`

| 方法 | 签名 | 说明 |
|------|------|------|
| `is_request` | `auto is_request() const noexcept -> bool` | 是否为请求 |
| `is_response` | `auto is_response() const noexcept -> bool` | 是否为响应 |
| `set_method` | `void set_method(method m) noexcept` | 设置请求方法 |
| `set_response` | `void set_response(response_code c) noexcept` | 设置响应码 |
| `add_option` | `void add_option(option_number, std::span<const std::byte>)` | 添加选项 |
| `add_string_option` | `void add_string_option(option_number, std::string_view)` | 添加字符串选项 |
| `add_uint_option` | `void add_uint_option(option_number, std::uint32_t)` | 添加整数选项 |
| `find_options` | `auto find_options(option_number) const -> std::vector<option>` | 查找选项 |

### `codec` — 消息编解码

**签名**: `auto parse_message(std::span<const std::byte>) -> std::expected<message, std::error_code>`
**签名**: `auto serialize_message(const message&) -> std::expected<std::vector<std::byte>, std::error_code>`
**签名**: `auto make_request(request_options opts) -> message`
**签名**: `auto extract_path(const message&) -> std::string`
**签名**: `auto extract_query(const message&) -> std::string`

### `udp_client` — CoAP 客户端

**签名**: `export class udp_client`（facade 别名 `client`）

```cpp
struct client_config {
    std::chrono::milliseconds ack_timeout{2000};
    double ack_random_factor = 1.5;
    std::uint8_t max_retransmit = 4;
    std::size_t max_datagram_size = 1152;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `resolve_endpoint` | `auto resolve_endpoint(string_view host, uint16_t port) -> task<std::expected<endpoint, std::error_code>>` | 解析端点 |
| `get` | `auto get(const endpoint&, std::string path, std::string query = {}) -> task<std::expected<message, std::error_code>>` | GET 请求 |
| `post` | `auto post(const endpoint&, std::string path, std::vector<std::byte> payload, content_format) -> task<...>` | POST 请求 |
| `put` | `auto put(const endpoint&, std::string path, std::vector<std::byte>, content_format) -> task<...>` | PUT 请求 |
| `delete_` | `auto delete_(const endpoint&, std::string path) -> task<...>` | DELETE 请求 |
| `get_blockwise` | `auto get_blockwise(const endpoint&, std::string path, uint8_t size_exp = 6) -> task<...>` | Block2 分块 GET |
| `post_blockwise` | `auto post_blockwise(const endpoint&, std::string path, std::vector<std::byte>, content_format, uint8_t size_exp) -> task<...>` | Block1 分块 POST |
| `put_blockwise` | `auto put_blockwise(const endpoint&, std::string path, std::vector<std::byte>, content_format, uint8_t size_exp) -> task<...>` | Block1 分块 PUT |
| `observe` | `auto observe(const endpoint&, std::string path, observe_handler, std::chrono::milliseconds lifetime) -> task<std::expected<void, std::error_code>>` | 注册观察 |
| `cancel_observe` | `auto cancel_observe(const endpoint&, std::string path) -> task<std::expected<message, std::error_code>>` | 取消观察 |

### `udp_server` — CoAP 服务端

**签名**: `export class udp_server`（facade 别名 `server`）

```cpp
struct server_config {
    std::size_t max_datagram_size = 1152;
    bool enable_observe = true;
    bool enable_resource_discovery = true;
    bool enable_blockwise = true;
    bool enable_proxy = true;
    std::chrono::seconds observe_max_age{60};
    std::uint8_t blockwise_size_exponent = 6;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `listen` | `auto listen(string_view host, uint16_t port, socket_options) -> std::expected<void, std::error_code>` | 监听端口 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop() noexcept` | 停止服务 |
| `route` | `void route(method, std::string path, request_handler)` | 注册路由 |
| `set_handler` | `void set_handler(request_handler)` | 设置全局处理器 |
| `set_etag_provider` | `void set_etag_provider(etag_provider)` | 设置 ETag 生成器 |
| `register_resource` | `void register_resource(resource_description)` | 注册资源（用于发现） |
| `notify_observers` | `auto notify_observers(std::string path, message) -> task<std::expected<std::size_t, std::error_code>>` | 推送观察通知 |
| `join_multicast_group` | `auto join_multicast_group(const ip_address&, ...) -> std::expected<void, std::error_code>` | 加入多播组 |

### `multicast_client` — 多播客户端

**签名**: `export class multicast_client`

```cpp
struct multicast_client_config {
    client_config coap;
    endpoint local_endpoint;
    bool loopback = true;
    int hops = 1;
    std::size_t max_responses = 16;
    std::chrono::milliseconds response_timeout{1500};
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `request` | `auto request(const endpoint& group, message) -> task<std::expected<std::vector<multicast_response>, std::error_code>>` | 多播请求 |
| `get` | `auto get(const endpoint& group, std::string path, std::string query = {}) -> task<...>` | 多播 GET |

辅助函数：
```cpp
auto all_coap_nodes_ipv4(uint16_t port = default_port) -> endpoint;
auto all_coap_nodes_ipv6_link_local(uint16_t port = default_port) -> endpoint;
```

### CoAPS（DTLS 安全通信）

> 需要 `-DCNETMOD_ENABLE_SSL=ON`，源码以 `#ifdef CNETMOD_HAS_SSL` 保护。

**签名**: `export class secure_client` / `export class secure_server`

```cpp
struct secure_client_config {
    client_config coap;
    std::size_t dtls_mtu = 1400;
    coaps_security_config security;
    std::chrono::seconds handshake_timeout{10};
};

struct coaps_security_config {
    coaps_peer_verification verify_peer = coaps_peer_verification::context_default;
    std::string peer_name;
    std::string ca_file;
    bool use_default_ca = false;

    static auto insecure_for_testing() -> coaps_security_config;
    static auto verified_peer(std::string peer_name = {}) -> coaps_security_config;
};
```

服务端 API 与 `udp_server` 一致（`listen` / `run` / `route` / `stop`），默认监听 `default_secure_port`(5684)。

### facade 便捷别名

```cpp
export using client = udp_client;
export using server = udp_server;
export using route = request_handler;
export using resource = resource_description;
export using subscription = observe_subscription;
export using multicast = multicast_client;

auto to_bytes(std::string_view text) -> std::vector<std::byte>;
auto payload_text(const message& msg) -> std::string;
void set_payload(message& msg, std::span<const std::byte> body);
auto text_response(const message& req, std::string_view body, response_code code = response_code::content) -> message;
auto json_response(const message& req, std::string_view body, response_code code = response_code::content) -> message;
```

## Do's & Don'ts

- **Do**: 使用 `resolve_endpoint` 解析地址后再调用 `get`/`post`，它会自动处理 DNS
- **Do**: 对大 payload 使用 `get_blockwise` / `post_blockwise`，让库自动处理 Block 分块
- **Do**: Observe 回调中处理通知时，使用 `cancel_observe` 主动取消订阅
- **Do**: 服务端用 `register_resource` 注册资源以支持 `/.well-known/core` 发现
- **Don't**: 不要在 `request_handler` 中阻塞，它是协程上下文，应 `co_return` 响应
- **Don't**: UDP 多播响应不可靠，设置合理的 `max_responses` 和 `response_timeout`

## 场景：客户端请求

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::coap::client client(ctx);
    auto remote = co_await client.resolve_endpoint("127.0.0.1", 5683);
    if (!remote) co_return;

    // GET
    auto resp = co_await client.get(*remote, "/sensors/temp");
    if (resp) {
        std::println("Temperature: {}", cn::coap::payload_text(*resp));
    }

    // POST
    auto body = cn::coap::to_bytes("{\"cmd\":\"on\"}");
    auto post_resp = co_await client.post(*remote, "/actuator", body, cn::coap::content_format::json);
    if (post_resp) {
        std::println("POST result: {}", cn::coap::payload_text(*post_resp));
    }

    ctx.stop();
}

auto main() -> int {
    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run(*ctx));
    ctx->run();
}
```

## 场景：服务端路由

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run_server(cn::io_context& ctx) -> cn::task<void> {
    cn::coap::server server(ctx);
    server.listen("0.0.0.0", 5683);

    server.route(cn::coap::method::get, "/sensors/temp",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            co_return cn::coap::text_response(req.request, "22.5");
        });

    server.register_resource(cn::coap::resource_description{
        .path = "/sensors/temp",
        .rt = "temperature-c",
        .if_ = "sensor",
        .observable = true,
    });

    co_await server.run();
}
```

## 场景：Observe 观察模式

```cpp
// 客户端：注册观察
auto observe_result = co_await client.observe(*remote, "/sensors/temp",
    [](const cn::coap::message& notification) {
        std::println("Notification: {}", cn::coap::payload_text(notification));
    });

// 取消观察
co_await client.cancel_observe(*remote, "/sensors/temp");

// 服务端：推送通知
cn::coap::message notification;
notification.set_response(cn::coap::response_code::content);
auto body = std::string("25.0");
notification.payload.assign(
    reinterpret_cast<const std::byte*>(body.data()),
    reinterpret_cast<const std::byte*>(body.data() + body.size()));
auto sent = co_await server.notify_observers("/sensors/temp", std::move(notification));
```

## 场景：Block 分块传输

```cpp
// 客户端 Block2 GET（大资源下载）
auto resp = co_await client.get_blockwise(*remote, "/large", 4); // block size = 2^(4+4) = 256 bytes

// 客户端 Block1 POST（大 payload 上传）
auto payload = cn::coap::to_bytes(large_string);
auto upload_resp = co_await client.post_blockwise(*remote, "/upload",
    payload, cn::coap::content_format::text_plain, 4);
```

服务端自动处理分块，需在 `server_config` 中设置 `enable_blockwise = true`（默认开启）。

## 场景：多播

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run_multicast(cn::io_context& ctx) -> cn::task<void> {
    cn::coap::multicast_client mc(ctx);
    auto group = cn::coap::all_coap_nodes_ipv4(5683);

    auto responses = co_await mc.get(group, "/.well-known/core");
    if (responses) {
        for (auto& [peer, msg] : *responses) {
            std::println("From {}: {}", peer.to_string(), cn::coap::payload_text(msg));
        }
    }
    ctx.stop();
}
```

## 场景：CoAPS DTLS 安全通信

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.protocol.coap;

namespace cn = cnetmod;

auto run_secure(cn::io_context& ctx) -> cn::task<void> {
    cn::ssl_context ssl_ctx;

    // 客户端
    cn::coap::secure_client client(ctx, ssl_ctx, {
        .security = cn::coap::coaps_security_config::insecure_for_testing()
    });
    auto remote = co_await client.resolve_endpoint("127.0.0.1", 5684);
    auto resp = co_await client.get(*remote, "/secure/resource");

    // 服务端
    cn::coap::secure_server server(ctx, ssl_ctx);
    server.route(cn::coap::method::get, "/secure/resource",
        [](const cn::coap::inbound_request& req, const cn::endpoint&) -> cn::task<cn::coap::message> {
            co_return cn::coap::text_response(req.request, "secure data");
        });
    server.listen("0.0.0.0", 5684);
    co_await server.run();
}
```

## 参考示例

- `examples/coap/coap_interop_server.cpp` — CoAP 服务端（路由 + Observe + Block）
- `examples/coap/coap_interop_client.cpp` — CoAP 客户端（GET + Block + POST）
- `examples/coap/coap_multicast_server.cpp` — 多播服务端
- `examples/coap/coap_multicast_client.cpp` — 多播客户端发现
- `examples/coap/coaps_interop_server.cpp` — CoAPS DTLS 服务端
- `examples/coap/coaps_interop_client.cpp` — CoAPS DTLS 客户端

## 连接池/连接管理（生产级用法）

### 说明

CoAP 基于 UDP 无连接协议，**模块未提供内置 `connection_pool`**。`udp_client` 内部维护单个 `udp_socket`，通过 Token 和 Message ID 匹配请求/响应。生产级连接管理方案：

1. **每 worker 独立 `udp_client`** — 多核场景下每个 worker 线程持有独立的客户端实例，避免锁竞争
2. **客户端对象池** — 如需管理多个远程端点，为每个端点维护独立的 `udp_client`

### 客户端复用模式

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.coap;

namespace cn = cnetmod;
namespace coap = cn::coap;

// 每个远程端点一个客户端实例，避免并发冲突
struct endpoint_client {
    coap::endpoint remote;
    std::unique_ptr<coap::udp_client> client;
};

auto manage_clients(cn::io_context& ctx) -> cn::task<void> {
    // 管理多个 CoAP 服务端点的客户端
    std::vector<endpoint_client> clients;

    // 为每个远程传感器节点创建独立客户端
    for (auto& host : {"192.168.1.10", "192.168.1.11", "192.168.1.12"}) {
        auto client = std::make_unique<coap::udp_client>(ctx);
        auto ep = co_await client->resolve_endpoint(host, 5683);
        if (!ep) continue;
        clients.push_back({*ep, std::move(client)});
    }

    // 并发轮询所有节点
    for (auto& ec : clients) {
        cn::spawn(ctx, [&ec]() -> cn::task<void> {
            auto resp = co_await ec.client->get(ec.remote, "/sensors/temp");
            if (resp) {
                std::println("Node {}: {}", ec.remote.to_string(),
                    coap::payload_text(*resp));
            }
        });
    }

    co_await cn::async_sleep(ctx, std::chrono::seconds(10));
}
```

## 多核/集群部署

### 部署模式

CoAP `udp_server` 仅接受单个 `io_context&`，**没有内置 `server_context` 构造函数**。由于 UDP 无连接特性，多核部署需手动实现：

1. **多实例 + 负载均衡** — 每个 worker 运行独立的 `udp_server`（不同端口），前置 UDP 负载均衡
2. **SO_REUSEPORT** — 多个 worker 绑定同一端口，内核自动分发（需操作系统支持）

### 多核 CoAP 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.coap;

namespace cn = cnetmod;
namespace coap = cn::coap;

constexpr unsigned WORKER_THREADS = 4;

auto main() -> int {
    cn::net_init net;
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 每个 worker 运行独立的 CoAP 服务端
    for (auto* io_ptr : sctx.worker_ios()) {
        cn::spawn(*io_ptr, [io_ptr]() -> cn::task<void> {
            coap::server server(*io_ptr, coap::server_config{
                .enable_observe = true,
                .enable_blockwise = true,
                .enable_proxy = false,    // 生产环境按需开启
            });

            // SO_REUSEPORT 允许同一端口多实例绑定
            auto listen_r = server.listen("0.0.0.0", 5683,
                cn::socket_options{.reuse_address = true, .non_blocking = true});
            if (!listen_r) {
                std::println("监听失败: {}", listen_r.error().message());
                co_return;
            }

            server.route(coap::method::get, "/sensors/temp",
                [](const coap::inbound_request& req,
                   const cn::endpoint&) -> cn::task<coap::message> {
                co_return coap::text_response(req.request, "22.5");
            });

            server.route(coap::method::post, "/actuators/cmd",
                [](const coap::inbound_request& req,
                   const cn::endpoint&) -> cn::task<coap::message> {
                auto body = coap::payload_text(req.request);
                std::println("收到命令: {}", body);
                co_return coap::text_response(req.request, "OK",
                    coap::response_code::changed);
            });

            server.register_resource(coap::resource_description{
                .path = "/sensors/temp",
                .rt = "temperature-c",
                .if_ = "sensor",
                .observable = true,
            });

            std::println("CoAP worker 启动 thread={}", std::this_thread::get_id());
            co_await server.run();
        });
    }

    sctx.run();
    return 0;
}
```

### 多播服务的生产级配置

多播用于设备发现（`/.well-known/core`）和群组操作，生产环境需精细控制：

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.coap;

namespace cn = cnetmod;
namespace coap = cn::coap;

auto run_multicast_discovery(cn::io_context& ctx) -> cn::task<void> {
    // 生产级多播客户端配置
    coap::multicast_client_config mc_cfg;
    mc_cfg.coap.max_retransmit = 2;
    mc_cfg.coap.ack_timeout = std::chrono::milliseconds(1000);
    mc_cfg.loopback = true;          // 开发环境开启，生产环境按需关闭
    mc_cfg.hops = 3;                 // 多播跳数限制（控制网络范围）
    mc_cfg.max_responses = 64;       // 最大响应数（大型网络调高）
    mc_cfg.response_timeout = std::chrono::milliseconds(3000);  // 等待响应超时

    coap::multicast_client mc(ctx, mc_cfg);

    // IPv4 全节点多播发现
    auto group_v4 = coap::all_coap_nodes_ipv4(5683);
    auto responses = co_await mc.get(group_v4, "/.well-known/core");
    if (responses) {
        std::println("发现 {} 个设备:", responses->size());
        for (auto& [peer, msg] : *responses) {
            std::println("  {} -> {}", peer.to_string(), coap::payload_text(msg));
        }
    }

    // IPv6 链路本地多播（IoT 场景）
    auto group_v6 = coap::all_coap_nodes_ipv6_link_local(5683);
    auto v6_responses = co_await mc.get(group_v6, "/.well-known/core");
    if (v6_responses) {
        for (auto& [peer, msg] : *v6_responses) {
            std::println("  [IPv6] {} -> {}", peer.to_string(),
                coap::payload_text(msg));
        }
    }

    mc.close();
}

// 服务端加入多播组（支持设备发现）
auto setup_multicast_server(coap::udp_server& server) -> void {
    // 加入 "All CoAP Nodes" 多播组 224.0.1.187
    auto group = cn::ip_address{cn::ipv4_address{224, 0, 1, 187}};
    auto result = server.join_multicast_group(group);
    if (!result) {
        std::println("加入多播组失败: {}", result.error().message());
    }

    // 注册资源以支持 /.well-known/core 自动发现
    server.register_resource(coap::resource_description{
        .path = "/sensors/temp",
        .rt = "temperature-c",
        .if_ = "sensor",
        .observable = true,
    });
    server.register_resource(coap::resource_description{
        .path = "/actuators/relay",
        .rt = "relay-switch",
        .if_ = "actuator",
        .observable = false,
    });
}
```
