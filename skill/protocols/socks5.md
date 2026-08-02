# SOCKS5 协议模块

> 完整的 SOCKS5 代理客户端与服务器实现，支持 CONNECT/BIND/UDP_ASSOCIATE 命令、用户名密码认证与 GSSAPI 扩展。

**import**: `import cnetmod.protocol.socks5;`
**CMake**: `-DCNETMOD_ENABLE_SOCKS5=ON`
**源码**: `src/protocol/socks5/`

## 场景导航

| 场景 | 关键类型 |
|------|---------|
| 客户端连接 | `client`, `connect()` |
| 客户端命令 | `connect_target()`, `bind()`, `udp_associate()` |
| 服务端监听 | `server`, `listen()`, `run()` |
| 身份验证 | `auth_method::no_auth`, `auth_method::username_password` |
| 地址解析 | `address_type`, `socks5_address` |
| GSSAPI 扩展 | `gssapi_message`, `gssapi_context` |
| UDP 中继 | `udp_datagram`, `protect_udp_datagram()` |

## API 参考

### `socks5_types` — 基础类型

**签名**:
```cpp
namespace cnetmod::socks5 {
constexpr std::uint8_t SOCKS_VERSION = 0x05;
enum class auth_method : std::uint8_t {
    no_auth = 0x00, gssapi = 0x01, username_password = 0x02, no_acceptable = 0xFF
};
enum class command : std::uint8_t {
    connect = 0x01, bind = 0x02, udp_associate = 0x03
};
enum class address_type : std::uint8_t { ipv4 = 0x01, domain_name = 0x03, ipv6 = 0x04 };
enum class reply : std::uint8_t {
    succeeded = 0x00, general_failure = 0x01, connection_refused = 0x05,
    command_not_supported = 0x07, address_type_not_supported = 0x08
};
struct socks5_address {
    address_type type; std::string host; std::uint16_t port;
    auto serialize() const -> std::vector<std::byte>;
    static auto parse(const std::byte*, std::size_t) -> std::optional<std::pair<socks5_address, std::size_t>>;
};
struct auth_request {
    std::uint8_t version = SOCKS_VERSION; std::vector<auth_method> methods;
    auto serialize() const -> std::vector<std::byte>;
    static auto parse(const std::byte*, std::size_t) -> std::optional<auth_request>;
};
struct auth_response {
    std::uint8_t version = SOCKS_VERSION; auth_method method;
    auto serialize() const -> std::vector<std::byte>;
    static auto parse(const std::byte*, std::size_t) -> std::optional<auth_response>;
};
struct username_password_request {
    std::uint8_t version = 0x01; std::string username, password;
    auto serialize() const -> std::vector<std::byte>;
};
struct username_password_response {
    std::uint8_t version = 0x01; std::uint8_t status; // 0x00 = success
    auto serialize() const -> std::vector<std::byte>;
};
struct socks5_request {
    std::uint8_t version = SOCKS_VERSION; command cmd;
    std::uint8_t reserved = 0x00; socks5_address address;
    auto serialize() const -> std::vector<std::byte>;
};
struct socks5_response {
    std::uint8_t version = SOCKS_VERSION; reply rep;
    std::uint8_t reserved = 0x00; socks5_address bind_address;
    auto serialize() const -> std::vector<std::byte>;
};
struct udp_datagram {
    std::uint16_t reserved = 0x0000; std::uint8_t fragment = 0x00;
    socks5_address address; std::vector<std::byte> payload;
    auto serialize() const -> std::vector<std::byte>;
};
}
```

### 客户端类 `client`

**签名**:
```cpp
export class client {
public:
    explicit client(io_context& ctx);

    /// RFC 1961 GSS-API 配置 (在 connect 前调用)
    void set_gssapi_context(
        gssapi_context context,
        gssapi_protection_level protection = gssapi_protection_level::integrity);

    /// 连接到 SOCKS5 代理服务器
    [[nodiscard]] auto connect(std::string_view proxy_host, std::uint16_t proxy_port)
        -> task<std::expected<void, std::error_code>>;

    /// 用户名密码认证
    [[nodiscard]] auto authenticate(std::string_view username, std::string_view password)
        -> task<std::expected<void, std::error_code>>;

    /// 通过代理连接到目标主机
    [[nodiscard]] auto connect_target(std::string_view target_host, std::uint16_t target_port)
        -> task<std::expected<void, std::error_code>>;

    /// 请求 SOCKS5 BIND 并返回服务端绑定端点
    [[nodiscard]] auto bind(std::string_view target_host, std::uint16_t target_port)
        -> task<std::expected<socks5_address, std::error_code>>;

    /// 等待 BIND 的第二个响应（远程对等体连接）
    [[nodiscard]] auto wait_bind_peer()
        -> task<std::expected<socks5_address, std::error_code>>;

    /// 请求 UDP ASSOCIATE 并返回 UDP 中继端点
    [[nodiscard]] auto udp_associate(std::string_view client_host = "0.0.0.0",
        std::uint16_t client_port = 0)
        -> task<std::expected<socks5_address, std::error_code>>;

    /// 读取/写入代理载荷
    [[nodiscard]] auto async_read(mutable_buffer buffer)
        -> task<std::expected<std::size_t, std::error_code>>;
    [[nodiscard]] auto async_write(const_buffer buffer)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// 保护/解密完整 UDP 数据报
    [[nodiscard]] auto protect_udp_datagram(std::span<const std::byte> datagram)
        -> std::expected<std::vector<std::byte>, std::error_code>;
    [[nodiscard]] auto unprotect_udp_datagram(std::span<const std::byte> protected_datagram)
        -> std::expected<std::vector<std::byte>, std::error_code>;

    /// 获取底层 socket
    [[nodiscard]] auto& socket();
    [[nodiscard]] auto& socket() const;
    [[nodiscard]] auto release_socket() -> cnetmod::socket;
    void close();
};
```

### 服务端类 `server`

**签名**:
```cpp
export using auth_handler = std::function<bool(std::string_view username, std::string_view password)>;
export struct server_config {
    bool allow_no_auth = true;
    bool allow_username_password = false;
    bool allow_gssapi = false;
    bool allow_bind = true; bool allow_udp_associate = true;
    auth_handler authenticator;
    gssapi_context_factory gssapi_factory;
    gssapi_protection_level gssapi_protection = gssapi_protection_level::integrity;
    std::size_t max_connections = 0; // 0 = unlimited
};
export class server {
public:
    /// 单线程模式
    explicit server(io_context& ctx, server_config config = {});
    /// 多核模式
    explicit server(server_context& sctx, server_config config = {});

    /// 监听指定地址和端口
    [[nodiscard]] auto listen(std::string_view host, std::uint16_t port,
        socket_options opts = {.reuse_address = true}) -> std::expected<void, std::error_code>;

    /// 运行服务器 (accept 循环)
    auto run() -> task<void>;

    /// 停止服务器
    void stop();

    /// 获取当前活动连接数
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t;
};
```

## 使用示例

### 客户端 - HTTP 请求

```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.coro.sync_wait;
import cnetmod.executor.async_op;
import cnetmod.protocol.socks5;

auto example_http_through_socks5(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    // 1. 创建 SOCKS5 客户端
    cnetmod::socks5::client socks_client(ctx);

    // 2. 连接到代理服务器
    auto conn_r = co_await socks_client.connect("127.0.0.1", 1080);
    if (!conn_r) throw std::runtime_error(conn_r.error().message());

    // 3. 用户名密码认证
    auto auth_r = co_await socks_client.authenticate("user", "pass");
    if (!auth_r) throw std::runtime_error(auth_r.error().message());

    // 4. 连接到目标
    auto target_r = co_await socks_client.connect_target("httpbin.org", 80);
    if (!target_r) throw std::runtime_error(target_r.error().message());

    // 5. 发送 HTTP GET
    auto& sock = socks_client.socket();
    std::string http_get =
        "GET /ip HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "User-Agent: cnetmod/1.0\r\n"
        "Connection: close\r\n\r\n";
    auto write_r = co_await cnetmod::async_write(ctx, sock,
        cnetmod::const_buffer{http_get.data(), http_get.size()});
    if (!write_r) throw std::runtime_error(write_r.error().message());

    // 6. 接收响应
    std::array<std::byte, 4096> buf;
    auto read_r = co_await cnetmod::async_read(ctx, sock,
        cnetmod::mutable_buffer{buf.data(), buf.size()});
    if (!read_r || *read_r == 0) throw std::runtime_error("empty response");

    socks_client.close();
}
```

### 客户端 - 无需认证

```cpp
cnetmod::socks5::client client(ctx);
auto conn = co_await client.connect("proxy-host", 1080);
if (!conn) /* handle error */;
auto auth = co_await client.authenticate("", ""); // 空凭据或不需要认证
if (!auth) /* handle error */;
auto target = co_await client.connect_target("example.com", 80);
if (!target) /* handle error */;
client.close();
```

### 服务端 - 简单守护进程

```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.socks5;

auto demo_cnetmod_server(cnetmod::server_context& sctx) -> cnetmod::task<void> {
    using namespace cnetmod::socks5;

    server_config config;
    config.allow_no_auth = true;
    config.allow_username_password = true;
    config.authenticator = [](std::string_view user, std::string_view pass) {
        return (user == "admin" && pass == "secret") ||
               (user == "user" && pass == "pass");
    };
    config.max_connections = 1000;

    server server(sctx, std::move(config));
    auto result = co_await server.listen("0.0.0.0", 1080);
    if (!result) throw std::runtime_error(result.error().message());

    std::println("SOCKS5 server running on 0.0.0.0:1080");
    co_await server.run();
}

auto main() -> int {
    try {
        std::size_t workers = std::thread::hardware_concurrency();
        cnetmod::server_context sctx(workers);
        sync_wait(demo_cnetmod_server(sctx));
    } catch (...) { return 1; }
}
```

### 服务端 - 单线程模式

```cpp
cnetmod::io_context io_ctx;
cnetmod::socks5::server_config cfg;
cfg.allow_no_auth = true;
cfg.allow_username_password = true;
cfg.authenticator = [](auto u, auto p) {
    return u == "user" && p == "password";
};
cnetmod::socks5::server server(io_ctx, std::move(cfg));
if (auto r = server.listen("127.0.0.1", 1080); !r) {
    throw std::system_error(r.error());
}
co_await server.run();
```

## Do's & Don'ts

| Do | Don't |
|----|-------|
| 先调用 `connect()` 再调用 `authenticate()` | 在未连接时尝试读写 socket |
| 使用 `async_read/async_write` 进行安全传输 | 忽略 RFC 1961 GSSAPI 加密数据流 |
| 检查 `connect_target` 和 `bind()` 的错误码 | 假设所有请求都成功——可能返回各种 reply 错误 |
| 为每个目标使用独立的 `client` 实例 | 重用已关闭的连接——需要重新 `connect()` |
| 配置 `allow_username_password` 设置验证逻辑 | 同时启用 GSSAPI 和用户密码而不处理冲突 |

## 参考示例

- `examples/socks5/client_demo.cpp` — 多个客户端用例：HTTP 代理、IP 直连、错误处理
- `examples/socks5/server_demo.cpp` — 多核心服务器演示，带统计信息报告
- `examples/socks5/README.md` — 详细的协议说明和使用指南

## 连接池/连接管理（生产级用法）

### 说明

SOCKS5 是 TCP 代理协议，**模块未提供内置 `connection_pool`**。每个客户端连接代表一条从客户端 → 代理 → 目标的 TCP 隧道链路。连接管理策略：

- **服务端**：通过 `server_config::max_connections` 限制并发连接数，`active_connections()` 监控当前负载
- **客户端**：每个目标连接使用独立的 `client` 实例，无法复用（SOCKS5 协议要求每个连接独立握手）

### 服务端连接限制与监控

```cpp
// server_config 关键字段
export struct server_config {
    bool allow_no_auth = true;
    bool allow_username_password = false;
    bool allow_bind = true;
    bool allow_udp_associate = true;
    auth_handler authenticator;
    std::size_t max_connections = 0;  // 0 = 无限制，生产环境务必设置上限
};

// 运行时监控
auto count = server.active_connections();  // 当前活动连接数
```

### 客户端连接管理模式

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.socks5;

namespace cn = cnetmod;
namespace socks = cn::socks5;

// 每个目标使用独立 client 实例
auto proxy_request(cn::io_context& ctx, std::string_view target_host,
    std::uint16_t target_port) -> cn::task<void>
{
    socks::client client(ctx);

    auto r = co_await client.connect("proxy.example.com", 1080);
    if (!r) co_return;

    auto auth_r = co_await client.authenticate("user", "pass");
    if (!auth_r) co_return;

    auto target_r = co_await client.connect_target(target_host, target_port);
    if (!target_r) co_return;

    // 通过代理发送/接收数据
    auto& sock = client.socket();
    std::string http_req = std::format(
        "GET / HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
        target_host);
    co_await cn::async_write(ctx, sock,
        cn::const_buffer{http_req.data(), http_req.size()});

    std::array<std::byte, 4096> buf;
    auto read_r = co_await cn::async_read(ctx, sock,
        cn::mutable_buffer{buf.data(), buf.size()});

    client.close();
}

// 并发访问多个目标
auto multi_target_proxy(cn::io_context& ctx) -> cn::task<void> {
    struct target { std::string host; std::uint16_t port; };
    std::vector<target> targets = {
        {"api.service-a.com", 443},
        {"api.service-b.com", 443},
        {"internal.corp.net", 8080},
    };

    for (auto& t : targets) {
        cn::spawn(ctx, [&ctx, host = t.host, port = t.port]() -> cn::task<void> {
            co_await proxy_request(ctx, host, port);
        });
    }

    co_await cn::async_sleep(ctx, std::chrono::seconds(30));
}
```

## 多核/集群部署

### 部署模式

SOCKS5 `server` **原生支持 `server_context` 多核部署**，提供两种构造方式：

```cpp
export class server {
    /// 单线程模式
    explicit server(io_context& ctx, server_config config = {});
    /// 多核模式 — 使用 server_context 自动多 worker 分发
    explicit server(server_context& sctx, server_config config = {});
};
```

| 线程 | 角色 | 说明 |
|------|------|------|
| Thread 0（main） | `accept_io()` | 专用 accept 循环 |
| Thread 1..N | `next_worker_io()` | 每个 worker 处理代理连接 I/O |

### 生产级 SOCKS5 代理网关

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.socks5;

namespace cn = cnetmod;
namespace socks = cn::socks5;

auto main() -> int {
    cn::net_init net;

    // 4 worker + 4 pool 线程
    cn::server_context sctx(4, 4);

    // 生产级配置
    socks::server_config config;
    config.allow_no_auth = false;           // 强制认证
    config.allow_username_password = true;
    config.allow_bind = false;              // 禁止 BIND（安全）
    config.allow_udp_associate = true;      // 允许 UDP ASSOCIATE
    config.max_connections = 10000;         // 最大并发连接
    config.authenticator = [](std::string_view user, std::string_view pass) -> bool {
        // 生产环境：对接 LDAP/数据库认证
        if (user == "admin" && pass == "strong_secret") return true;
        if (user.starts_with("user_")) return pass.size() >= 8;
        return false;
    };

    // 使用 server_context 多核模式
    socks::server server(sctx, std::move(config));

    auto listen_r = server.listen("0.0.0.0", 1080);
    if (!listen_r) {
        std::println("监听失败: {}", listen_r.error().message());
        return 1;
    }

    // 在 accept_io 上启动监控
    cn::spawn(sctx.accept_io(), [&server, &sctx]() -> cn::task<void> {
        while (server.active_connections() > 0 || true) {
            co_await cn::async_sleep(sctx.accept_io(), std::chrono::seconds(30));
            std::println("[监控] 活动连接: {}", server.active_connections());
        }
    });

    std::println("SOCKS5 代理网关启动: 0.0.0.0:1080 ({} workers)",
        sctx.worker_count());

    // 在 accept_io 上启动 accept 循环
    cn::spawn(sctx.accept_io(), server.run());

    // 阻塞运行（accept + workers）
    sctx.run();
    return 0;
}
```

### 多实例集群（前置负载均衡）

如需跨机器部署多个 SOCKS5 代理实例，前置 L4 负载均衡器（如 LVS/HAProxy）：

```cpp
// 每个机器运行一个独立实例
auto main() -> int {
    cn::net_init net;
    cn::server_context sctx(std::thread::hardware_concurrency());

    socks::server_config config;
    config.allow_username_password = true;
    config.max_connections = 50000;
    config.authenticator = [](auto user, auto pass) {
        // 对接统一认证服务
        return true;
    };

    socks::server server(sctx, std::move(config));
    server.listen("0.0.0.0", 1080);
    cn::spawn(sctx.accept_io(), server.run());
    sctx.run();
    return 0;
}
```
