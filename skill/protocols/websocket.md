# WebSocket

> 全双工 WebSocket 客户端与服务端，支持路由注册、多核分发、TLS 及心跳保活。

**import**: `import cnetmod.protocol.websocket;`
**CMake**: `-DCNETMOD_ENABLE_WEBSOCKET=ON`
**依赖**: `cnetmod.protocol.http`、`cnetmod.io.io_context`、`cnetmod.coro.task`
**源码**: `src/protocol/websocket/`

## 场景导航

- 我要做 echo 服务 → [看这里](#场景1ws_server-路由注册)
- 我要做 WebSocket 客户端 → [看这里](#场景2ws_client)
- 我要做 frame 编解码 → [看这里](#场景3frame-编解码)
- 我要做多核 WebSocket → [看这里](#场景4多核-server_context)

## 核心类型

**`ws::opcode`** — 帧操作码：`continuation(0x0)`、`text(0x1)`、`binary(0x2)`、`close(0x8)`、`ping(0x9)`、`pong(0xA)`

```cpp
auto is_control(opcode op) noexcept -> bool;
auto opcode_to_string(opcode op) noexcept -> std::string_view;
```

**`ws::close_code`** — 关闭码常量：`normal(1000)`、`going_away(1001)`、`protocol_error(1002)`、`message_too_big(1009)` 等

**`ws::ws_message`** — 接收到的消息：
```cpp
struct ws_message {
    opcode op; std::vector<std::byte> payload;
    auto as_string() const noexcept -> std::string_view;
};
```

**`ws::ws_errc`** — 错误码：`success`、`invalid_frame`、`handshake_failed`、`not_connected`、`protocol_error` 等

**`ws::frame_header`** — 帧头：`fin`、`rsv1-3`、`op`、`masked`、`payload_length`、`masking_key`

## API 参考

### frame 编解码

```cpp
auto parse_frame_header(std::span<const std::byte> data)
    -> std::expected<std::pair<frame_header, std::size_t>, std::error_code>;
auto build_frame(opcode op, std::span<const std::byte> payload, bool mask, bool fin = true) -> std::vector<std::byte>;
auto build_close_frame(std::uint16_t code, std::string_view reason, bool mask) -> std::vector<std::byte>;
void apply_mask(std::span<std::byte> data, std::uint32_t key) noexcept;
```

### 升级握手

```cpp
auto generate_sec_key() -> std::string;
auto compute_accept_key(std::string_view sec_key) -> std::string;
auto build_upgrade_request(std::string_view host, std::string_view path,
    std::string_view sec_key, std::string_view subprotocol = {}, std::string_view origin = {}) -> http::request;
auto validate_upgrade_response(const http::response_parser& resp, std::string_view expected_accept)
    -> std::expected<void, std::error_code>;
auto validate_upgrade_request(const http::request_parser& req) -> std::expected<std::string, std::error_code>;
auto build_upgrade_response(std::string_view accept_key, std::string_view subprotocol = {}) -> http::response;
```

### `ws::connection` — 底层连接

```cpp
auto async_connect(std::string_view url, const connect_options& opts = {}) -> task<std::expected<void, std::error_code>>;
auto async_accept(socket client_sock) -> task<std::expected<void, std::error_code>>;
auto async_send_text(std::string_view text) -> task<std::expected<void, std::error_code>>;
auto async_send_binary(std::span<const std::byte> data) -> task<std::expected<void, std::error_code>>;
auto async_ping(std::span<const std::byte> payload = {}) -> task<std::expected<void, std::error_code>>;
auto async_recv() -> task<std::expected<ws_message, std::error_code>>;
auto async_close(std::uint16_t code = close_code::normal, std::string_view reason = "") -> task<std::expected<void, std::error_code>>;
auto is_open() const noexcept -> bool;
auto handshake_path() const noexcept -> std::string_view;
auto handshake_query() const noexcept -> std::string_view;
auto handshake_headers() const noexcept -> const http::header_map&;
```

### `ws::client` — 高层客户端

```cpp
explicit client(io_context& ctx) noexcept;
auto connect(std::string_view url, const client_options& opts = {}) -> task<std::expected<void, std::error_code>>;
auto send_text(std::string_view text) -> task<std::expected<void, std::error_code>>;
auto send_binary(std::span<const std::byte> data) -> task<std::expected<void, std::error_code>>;
auto ping(std::span<const std::byte> payload = {}) -> task<std::expected<void, std::error_code>>;
auto recv() -> task<std::expected<ws_message, std::error_code>>;
auto close(std::uint16_t code = close_code::normal, std::string_view reason = "") -> task<std::expected<void, std::error_code>>;
auto run_heartbeat() -> task<std::expected<void, std::error_code>>;
auto is_open() const noexcept -> bool;
```

**`client_options`**: `connect`（子协议、Origin、TLS）、`heartbeat_interval`、`heartbeat_payload`

### `ws::server` — 服务端

```cpp
explicit server(io_context& ctx);         // 单线程
explicit server(server_context& sctx);    // 多核
auto listen(std::string_view host, std::uint16_t port, socket_options opts = {.reuse_address = true})
    -> std::expected<void, std::error_code>;
void on(std::string_view pattern, ws_handler_fn handler); // 支持 /echo, /chat/:room, /api/*
auto run() -> task<void>;
void stop();
```

### `ws_context` — 路由上下文

```cpp
auto path() const noexcept -> std::string_view;
auto query_string() const noexcept -> std::string_view;
auto get_header(std::string_view key) const -> std::string_view;
auto param(std::string_view name) const noexcept -> std::string_view; // 路由参数
auto send_text(std::string_view text) -> task<std::expected<void, std::error_code>>;
auto send_binary(std::span<const std::byte> data) -> task<std::expected<void, std::error_code>>;
auto recv() -> task<std::expected<ws_message, std::error_code>>;
auto close(std::uint16_t code = close_code::normal, std::string_view reason = "") -> task<std::expected<void, std::error_code>>;
auto is_open() const noexcept -> bool;
auto raw_connection() noexcept -> connection&;
```

**`ws_handler_fn`**: `std::function<task<void>(ws_context&)>`

## 场景 1：ws::server 路由注册

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

auto echo_handler(ws::ws_context& ctx) -> cnetmod::task<void> {
    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg || msg->op == ws::opcode::close) break;
        co_await ctx.send_text(std::format("[echo] {}", msg->as_string()));
    }
}

int main() {
    auto ctx = cnetmod::make_io_context();
    ws::server srv(*ctx);
    srv.listen("127.0.0.1", 18080);
    srv.on("/echo", echo_handler);
    srv.on("/chat/:room", [](ws::ws_context& ctx) -> cnetmod::task<void> {
        auto room = ctx.param("room");
        while (ctx.is_open()) {
            auto msg = co_await ctx.recv();
            if (!msg || msg->op == ws::opcode::close) break;
            co_await ctx.send_text(std::format("[{}] {}", room, msg->as_string()));
        }
    });
    cnetmod::spawn(*ctx, srv.run());
    ctx->run();
}
```

## 场景 2：ws::client

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

auto run(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    ws::client cli(ctx);
    co_await cli.connect("ws://127.0.0.1:18080/echo", {
        .connect = {.subprotocol = "chat"},
        .heartbeat_interval = std::chrono::seconds(30),
    });
    co_await cli.send_text("Hello WebSocket!");
    auto msg = co_await cli.recv();
    if (msg && msg->op == ws::opcode::text)
        std::println("recv: {}", msg->as_string());
    co_await cli.close();
}
```

## 场景 3：frame 编解码

```cpp
import std;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

void frame_demo() {
    auto frame = ws::build_frame(ws::opcode::text,
        std::as_bytes(std::span{"hello"sv}), true);
    auto [header, hdr_len] = ws::parse_frame_header(frame).value();
    std::println("op={} len={}", ws::opcode_to_string(header.op), header.payload_length);
    auto close = ws::build_close_frame(ws::close_code::normal, "bye", true);
}
```

## 场景 4：多核 server_context

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

int main() {
    cnetmod::server_context sctx(4, 4);
    ws::server srv(sctx);
    srv.listen("0.0.0.0", 18080);
    srv.on("/echo", [](ws::ws_context& ctx) -> cnetmod::task<void> {
        while (ctx.is_open()) {
            auto msg = co_await ctx.recv();
            if (!msg || msg->op == ws::opcode::close) break;
            co_await ctx.send_text(std::format("[echo@{}] {}",
                std::this_thread::get_id(), msg->as_string()));
        }
    });
    cnetmod::spawn(sctx.accept_io(), srv.run());
    sctx.run();
}
```

## 连接池（生产级用法）

### 替代方案：连接跟踪

WebSocket 是长连接有状态协议，**不使用连接池**。生产环境推荐通过 `connection_registry` 跟踪活跃连接，实现广播、房间管理等能力。

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.websocket;

namespace ws = cnetmod::ws;

// 线程安全的连接注册表
class connection_registry {
    std::mutex mtx_;
    std::unordered_map<std::string, std::vector<ws::connection*>> rooms_;

public:
    void join(const std::string& room, ws::connection* conn) {
        std::lock_guard lock(mtx_);
        rooms_[room].push_back(conn);
    }

    void leave(const std::string& room, ws::connection* conn) {
        std::lock_guard lock(mtx_);
        auto& vec = rooms_[room];
        std::erase(vec, conn);
        if (vec.empty()) rooms_.erase(room);
    }

    // 广播消息到指定房间的所有连接
    auto broadcast(const std::string& room, std::string_view text)
        -> cnetmod::task<void>
    {
        std::vector<ws::connection*> targets;
        {
            std::lock_guard lock(mtx_);
            if (auto it = rooms_.find(room); it != rooms_.end())
                targets = it->second;
        }
        for (auto* conn : targets) {
            if (conn->is_open())
                (void)co_await conn->async_send_text(text);
        }
    }
};
```

## 多核服务器部署

### server_context 模式

`ws::server` 原生支持 `server_context` 多核模式。accept 线程负责接受连接，新连接通过 round-robin 分发到 worker `io_context`。

**API 签名**（来自 `websocket_server.cppm`）：

```cpp
// 单线程模式
explicit server(io_context& ctx);
// 多核模式：accept 在 sctx.accept_io()，连接分发至 worker io_context
explicit server(server_context& sctx);
auto listen(std::string_view host, std::uint16_t port,
    socket_options opts = {.reuse_address = true})
    -> std::expected<void, std::error_code>;
void on(std::string_view pattern, ws_handler_fn handler);
auto run() -> task<void>;
void stop();
```

**`server_context` API**（来自 `pool.cppm`）：

```cpp
explicit server_context(unsigned workers, unsigned pool_threads);
auto accept_io() noexcept -> io_context&;          // accept 专用
auto next_worker_io() noexcept -> io_context&;     // round-robin 选取
auto worker_count() const noexcept -> unsigned;
auto worker_ios() -> std::vector<io_context*>;
void run();   // 阻塞，直到 stop()
void stop();
```

**生产级多核示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.websocket;
import cnetmod.protocol.http.middleware.access_log;

namespace cn = cnetmod;
namespace ws = cnetmod::ws;

int main() {
    cn::net_init net;

    // 4 worker 线程 + 4 线程池
    cn::server_context sctx(4, 4);

    ws::server srv(sctx);
    auto lr = srv.listen("0.0.0.0", 18080);
    if (!lr) {
        std::println("listen failed: {}", lr.error().message());
        return 1;
    }

    // 路由注册 — 连接自动分发到不同 worker
    srv.on("/echo", [](ws::ws_context& ctx) -> cn::task<void> {
        while (ctx.is_open()) {
            auto msg = co_await ctx.recv();
            if (!msg || msg->op == ws::opcode::close) break;
            co_await ctx.send_text(
                std::format("[thread:{}] {}", std::this_thread::get_id(),
                            msg->as_string()));
        }
    });

    srv.on("/chat/:room", [](ws::ws_context& ctx) -> cn::task<void> {
        auto room = ctx.param("room");
        while (ctx.is_open()) {
            auto msg = co_await ctx.recv();
            if (!msg || msg->op == ws::opcode::close) break;
            co_await ctx.send_text(
                std::format("[{}] {}", room, msg->as_string()));
        }
    });

    // 在 accept_io 上启动 server
    cn::spawn(sctx.accept_io(), srv.run());

    // 阻塞主线程
    sctx.run();
}
```

**多核广播模式**：

由于连接分布在不同 worker 线程上，跨线程广播需使用互斥保护的注册表：

```cpp
connection_registry registry; // 全局

auto chat_handler(ws::ws_context& ctx) -> cn::task<void> {
    auto room = std::string(ctx.param("room"));
    auto& conn = ctx.raw_connection();
    registry.join(room, &conn);

    while (ctx.is_open()) {
        auto msg = co_await ctx.recv();
        if (!msg || msg->op == ws::opcode::close) break;
        auto text = std::format("[{}] {}", room, msg->as_string());
        co_await registry.broadcast(room, text);
    }
    registry.leave(room, &conn);
}
```

> **注意**：handler 内勿在 handler 外保存 `ws_context` 引用；仅保存 `connection*` 用于广播。

## Do's & Don'ts

| Do | Don't |
|---|---|
| 客户端 `build_frame` 时 `mask=true` | 不要在服务端发出的帧上设置 mask |
| 长连接启用 `heartbeat_interval` | 不要忽略 close 帧的读取 |
| 使用 `ws_context::param` 获取路由参数 | 不要在 handler 外保存 `ws_context` 引用 |
| 多核模式使用 `server_context` | 不要在单线程 server 上调用 `sctx.run()` |
| 控制帧由 `connection` 自动响应 | 不要手动构造 pong 帧回复 |
| 广播使用互斥保护的连接注册表 | 不要无锁遍历连接集合 |

## 参考示例

- `examples/websocket/ws_demo.cpp` — 底层 connection + exec::async_scope 并发
- `examples/websocket/hight_ws.cpp` — 高层 server 路由注册 + client
- `examples/websocket/multicore_ws.cpp` — server_context 多核分发
