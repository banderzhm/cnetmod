# WebSocket

> 全双工 WebSocket 客户端与服务端，支持路由注册、多核分发、TLS 及心跳保活。

**import**: `import cnetmod.protocol.websocket;`
**CMake**: `-DCNETMOD_ENABLE_WEBSOCKET=ON`
**依赖**: `cnetmod.protocol.http`、`cnetmod.io.io_context`、`cnetmod.coro.task`
**源码**: `src/protocol/websocket/`

## 场景导航

- 我要做 echo 服务 → [看这里](#场景1ws_server-路由注册)
- 我要让 HTTP/1、h2c/gRPC、WebSocket 共用端口 → [看这里](#场景http-router-内升级)
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
void append_frame_into(std::vector<std::byte>& frames, opcode op,
    std::span<const std::byte> payload, bool mask, bool fin = true);
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
auto validate_upgrade_request(const http::request_context& req) -> std::expected<std::string, std::error_code>;
auto build_upgrade_response(std::string_view accept_key, std::string_view subprotocol = {}) -> http::response;
```

### HTTP router 内升级

```cpp
auto accept_upgrade(http::request_context& request,
    ws_handler_fn handler, std::string_view subprotocol = {})
    -> std::expected<void, std::error_code>;
```

`accept_upgrade()` 是业务路由应使用的标准入口。它校验 RFC 6455 握手、准备
`101 Switching Protocols`，并让 HTTP server 在 handler 返回后安全移交连接。
移交前会停止 HTTP 的持续接收，再把 HTTP parser 和 io_uring 已预读的尾部字节
一并交给 `ws::connection`，因此客户端把第一帧和握手放在同一个 TCP 包中也不会丢帧。

该入口用于明文 HTTP/1.1 listener；同一 listener 仍可自动处理 h2c/gRPC。
TLS/WSS 使用 `ws::server::set_ssl_context()` 的独立 listener。应用不要自行调用
`request_context::raw_socket()`、手写 101 或在 handler 中移动底层 socket。

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

`connection::attach(socket, bool, std::span<const std::byte>)` 可接管已升级的
传输并保留 HTTP 解析器多读的后续帧字节；传入的 span 在调用期间被复制。
框架自带的 WS/WSS 握手也保留这些尾部字节。HTTP 的 request/response parser
提供 `take_buffered_data()` 转移已完成消息后的字节，未完成时返回空且不消费输入。

多 worker 的 Linux server 使用每 worker 的 SO_REUSEPORT listener，连接在接受它的
事件循环处理；`stop()` 在对应事件循环取消 accept，并等待全部 listener 退出。
默认启用 TCP_NODELAY，显式传入 `socket_options` 时遵循调用方选项。

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

### 同步消息回调与批量写出

同步业务可在 `ws_context::raw_connection()` 上使用消息循环，避免为每条消息
创建接收、转发、发送协程。普通 `recv()/send_text()/send_binary()` API 保持可用，
业务需要 `co_await` 数据库等异步操作时继续使用普通 API。

```cpp
using message_handler_fn = std::function<std::expected<void, std::error_code>(
    opcode, std::span<const std::byte>, message_writer&)>;

auto connection::async_process_messages(message_handler_fn handler,
    std::size_t max_message_size = 16 * 1024 * 1024)
    -> task<std::expected<void, std::error_code>>;

auto message_writer::send(opcode op, std::span<const std::byte> payload)
    -> std::expected<void, std::error_code>;
```

| 参数/对象 | 契约 |
|---|---|
| `handler` | 同步处理一条完整 text/binary 消息；返回错误终止循环 |
| `payload` | 借用视图，仅在本次回调返回前有效；需要保存时自行复制 |
| `message_writer` | 回调中发送 text/binary，允许发送零条或多条；每条仍编码为独立帧 |
| `max_message_size` | 完整消息和分片累计上限，默认 16 MiB；超限发送 1009 关闭帧 |

循环仅合并已经接收的完整消息产生的输出，不等待凑满一批，也不增加定时延迟。
下一帧头或 payload 尚不完整时，先写完已有响应再等待输入。发送缓冲属于连接，
所有写操作完成后才复用；回调不得保存 writer、借用 payload，也不得并发调用该连接
的其他接收或发送操作。回调中的重 CPU/阻塞操作仍须遵守 executor/bridge 规范。

此路径验证掩码方向、保留位/操作码、分片顺序、控制帧、关闭状态码、文本及关闭原因
的 UTF-8；ping/pong/close 由循环处理。它不协商扩展或压缩。

```cpp
auto echo(ws::ws_context& ctx) -> cn::task<void>
{
    (void)co_await ctx.raw_connection().async_process_messages(
        [](ws::opcode op, std::span<const std::byte> payload, ws::message_writer& output)
            -> std::expected<void, std::error_code>
        {
            return output.send(op, payload);
        });
}
```

### `ws::server` — 服务端

```cpp
explicit server(io_context& ctx);         // 单线程
explicit server(server_context& sctx);    // 多核
auto listen(std::string_view host, std::uint16_t port, socket_options opts = {.reuse_address = true, .no_delay = true})
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

## 场景：HTTP router 内升级

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;

namespace http = cnetmod::http;
namespace ws = cnetmod::ws;

http::router routes;
routes.get("/ws", [](http::request_context& request) -> cnetmod::task<void>
{
    auto accepted = ws::accept_upgrade(request,
        [](ws::ws_context& connection) -> cnetmod::task<void>
        {
            (void)co_await connection.raw_connection().async_process_messages(
                [](ws::opcode op, std::span<const std::byte> payload,
                    ws::message_writer& output)
                    -> std::expected<void, std::error_code>
                {
                    return output.send(op, payload);
                });
        });
    if (!accepted)
        request.text(http::status::bad_request, "Bad WebSocket handshake");
    co_return;
});

cnetmod::server_context runtime(4, 4);
http::server server(runtime);
server.set_router(std::move(routes));
if (auto listening = server.listen("0.0.0.0", 8080); !listening)
    throw std::system_error(listening.error());
cnetmod::spawn(runtime.accept_io(), server.run());
runtime.run();
```

路由中间件在升级前照常执行，所以认证、授权、限流应声明在 `/ws` 路由上。
升级成功后 HTTP handler 已结束，长连接生命周期由 `ws_handler_fn` 管理。

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
    socket_options opts = {.reuse_address = true, .no_delay = true})
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
| 共用 8080 时使用 `ws::accept_upgrade()` | 不要解析第二遍 Bearer/HTTP 头或手写 101 |
| 控制帧由 `connection` 自动响应 | 不要手动构造 pong 帧回复 |
| 广播使用互斥保护的连接注册表 | 不要无锁遍历连接集合 |

## 参考示例

- `examples/websocket/ws_demo.cpp` — 底层 connection + cnetmod task/spawn 并发
- `examples/websocket/hight_ws.cpp` — 高层 server 路由注册 + client
- `examples/websocket/multicore_ws.cpp` — server_context 多核分发
