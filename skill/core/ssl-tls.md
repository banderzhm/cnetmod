# SSL/TLS

> 提供基于 OpenSSL 的 TLS/DTLS 加密通信支持，包含上下文管理、异步流式读写及 ALPN/SNI/kTLS。

**import**: `import cnetmod.core.ssl;` (TLS) / `import cnetmod.core.dtls;` (DTLS)
**源码**: `src/core/ssl.cppm`, `src/core/dtls.cppm`

> ⚠️ 所有 SSL/TLS 功能受条件编译宏 `CNETMOD_HAS_SSL` 保护。构建时需启用 `-DCNETMOD_ENABLE_SSL=ON` 并链接 OpenSSL。

## 场景导航
- 我要创建 TLS 服务端 → [看这里](#场景tls-服务端)
- 我要创建 TLS 客户端 → [看这里](#场景tls-客户端)
- 我要配置 ALPN 协议协商 → [看这里](#场景alpn-配置)
- 我要使用 DTLS（数据报 TLS） → [看这里](#场景dtls-数据报-tls)
- 我要启用 kTLS 内核加速 → [看这里](#场景ktls-内核加速)

## API 参考

### `ssl_context`
**签名**: `export class ssl_context`

SSL 上下文（RAII 封装 `SSL_CTX*`），不可拷贝，仅可移动。

**工厂方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `client` | `static auto client() -> expected<ssl_context, error_code>` | 创建 TLS 客户端上下文 |
| `server` | `static auto server() -> expected<ssl_context, error_code>` | 创建 TLS 服务端上下文 |
| `dtls_client` | `static auto dtls_client() -> expected<ssl_context, error_code>` | 创建 DTLS 客户端上下文 |
| `dtls_server` | `static auto dtls_server() -> expected<ssl_context, error_code>` | 创建 DTLS 服务端上下文 |

**证书与密钥**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `load_cert_file` | `auto load_cert_file(string_view path) -> expected<void, error_code>` | 加载 PEM 证书 |
| `load_key_file` | `auto load_key_file(string_view path) -> expected<void, error_code>` | 加载 PEM 私钥 |
| `load_ca_file` | `auto load_ca_file(string_view path) -> expected<void, error_code>` | 加载 CA 证书 |
| `set_default_ca` | `auto set_default_ca() -> expected<void, error_code>` | 使用系统默认 CA |

**配置**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `set_verify_peer` | `void set_verify_peer(bool verify) noexcept` | 设置是否验证对端证书 |
| `set_require_peer_certificate` | `void set_require_peer_certificate(bool require) noexcept` | 要求客户端证书（mTLS） |
| `set_kernel_tls` | `void set_kernel_tls(bool enabled) noexcept` | 启用 kTLS（Linux） |
| `kernel_tls_enabled` | `auto kernel_tls_enabled() const noexcept -> bool` | 查询 kTLS 状态 |

**ALPN**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `configure_alpn_server` | `void configure_alpn_server(initializer_list<string_view> protos)` | 服务端 ALPN 协议列表 |
| `configure_alpn_client` | `void configure_alpn_client(initializer_list<string_view> protos)` | 客户端 ALPN 协议列表 |

**其他**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `native` | `auto native() const noexcept -> SSL_CTX*` | 获取原生 SSL_CTX 指针 |

### `ssl_stream`
**签名**: `export class ssl_stream`

异步 TLS 流（基于 Memory BIO），不可拷贝，仅可移动。

**构造**: `ssl_stream(ssl_context& ssl_ctx, io_context& io_ctx, socket& sock)`

Socket 必须已通过 `async_connect`（客户端）或 `async_accept`（服务端）建立连接。

| 方法 | 签名 | 说明 |
|------|------|------|
| `set_hostname` | `void set_hostname(string_view hostname)` | 设置 SNI 主机名（握手前调用） |
| `set_connect_state` | `void set_connect_state() noexcept` | 设为客户端模式 |
| `set_accept_state` | `void set_accept_state() noexcept` | 设为服务端模式 |
| `async_handshake` | `auto async_handshake() -> task<expected<void, error_code>>` | 异步 TLS 握手 |
| `async_read` | `auto async_read(mutable_buffer buf) -> task<expected<size_t, error_code>>` | 异步读取解密明文 |
| `async_write` | `auto async_write(const_buffer buf) -> task<expected<size_t, error_code>>` | 异步写入（加密后发送） |
| `async_write_all` | `auto async_write_all(const_buffer buf) -> task<expected<void, error_code>>` | 异步写完所有字节 |
| `async_shutdown` | `auto async_shutdown() -> task<expected<void, error_code>>` | 异步 TLS 关闭 |
| `get_alpn_selected` | `auto get_alpn_selected() const noexcept -> string_view` | 获取 ALPN 协商结果 |
| `kernel_tls_active` | `auto kernel_tls_active() const noexcept -> bool` | kTLS 是否激活 |
| `native` | `auto native() const noexcept -> SSL*` | 获取原生 SSL 指针 |

### 错误处理
**签名**:
```cpp
export auto make_ssl_error() -> std::error_code;
export auto make_ssl_error(int ssl_err) -> std::error_code;
```

### `dtls_role` 枚举
**签名**: `export enum class dtls_role`
| 值 | 说明 |
|---|------|
| `client` | DTLS 客户端 |
| `server` | DTLS 服务端 |

### `dtls_datagram_options`
**签名**: `export struct dtls_datagram_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `mtu` | `size_t` | `1400` | 最大传输单元 |
| `recv_buffer_size` | `size_t` | `65536` | 接收缓冲区大小 |

### `dtls_datagram_session`
**签名**: `export class dtls_datagram_session`

DTLS 数据报会话，不可拷贝，仅可移动。

**构造**:
```cpp
dtls_datagram_session(ssl_context& ssl_ctx, io_context& io_ctx,
    socket& sock, endpoint peer, dtls_role role,
    dtls_datagram_options options = {});
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `set_hostname` | `void set_hostname(string_view hostname)` | 设置 SNI 主机名 |
| `queue_datagram` | `void queue_datagram(const_buffer datagram)` | 预排队首个数据报 |
| `set_receive_handler` | `void set_receive_handler(receive_handler handler)` | 设置接收回调 |
| `peer` | `auto peer() const noexcept -> const endpoint&` | 获取对端地址 |
| `native` | `auto native() const noexcept -> void*` | 获取原生 SSL 指针 |
| `async_handshake` | `auto async_handshake() -> task<expected<void, error_code>>` | DTLS 握手 |
| `async_read` | `auto async_read(mutable_buffer buf) -> task<expected<size_t, error_code>>` | 读取解密数据 |
| `async_write` | `auto async_write(const_buffer buf) -> task<expected<size_t, error_code>>` | 写入加密数据 |
| `async_shutdown` | `auto async_shutdown() -> task<expected<void, error_code>>` | DTLS 关闭 |

其中 `receive_handler` 类型：
```cpp
using receive_handler = std::function<task<expected<vector<byte>, error_code>>()>;
```

## 场景：TLS 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

#ifdef CNETMOD_HAS_SSL

auto handle_client(cnetmod::io_context& ctx, cnetmod::socket sock,
                   cnetmod::ssl_context& ssl_ctx) -> cnetmod::task<void>
{
    cnetmod::ssl_stream stream(ssl_ctx, ctx, sock);
    stream.set_accept_state();

    auto hs = co_await stream.async_handshake();
    if (!hs) co_return;

    std::array<std::byte, 4096> buf{};
    for (;;) {
        auto rd = co_await stream.async_read(
            cnetmod::mutable_buffer{buf.data(), buf.size()});
        if (!rd || *rd == 0) break;
        co_await stream.async_write(cnetmod::const_buffer{buf.data(), *rd});
    }
    (void)co_await stream.async_shutdown();
    sock.close();
}

#endif
```

## 场景：TLS 客户端

```cpp
import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

#ifdef CNETMOD_HAS_SSL

auto run_client(cnetmod::io_context& ctx, cnetmod::ssl_context& ssl_ctx)
    -> cnetmod::task<void>
{
    auto sock = *cnetmod::socket::create(
        cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    auto ep = cnetmod::endpoint{cnetmod::ipv4_address::loopback(), 8443};
    co_await cnetmod::async_connect(ctx, sock, ep);

    cnetmod::ssl_stream stream(ssl_ctx, ctx, sock);
    stream.set_hostname("localhost");
    stream.set_connect_state();

    auto hs = co_await stream.async_handshake();
    if (!hs) co_return;

    auto msg = std::string_view{"Hello TLS"};
    co_await stream.async_write(cnetmod::buffer(msg));

    (void)co_await stream.async_shutdown();
    sock.close();
}

#endif
```

## 场景：ALPN 配置

```cpp
// 服务端：按优先级列出协议
ssl_ctx.configure_alpn_server({"h2", "http/1.1"});

// 客户端：声明支持的协议
ssl_ctx.configure_alpn_client({"h2", "http/1.1"});

// 握手后获取协商结果
auto selected = stream.get_alpn_selected();
// selected == "h2" 或 "http/1.1"
```

## 场景：DTLS 数据报 TLS

```cpp
import std;
import cnetmod.core;
import cnetmod.core.ssl;
import cnetmod.core.dtls;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

#ifdef CNETMOD_HAS_SSL

auto run_dtls_server(cnetmod::io_context& ctx, cnetmod::ssl_context& ssl_ctx,
                     std::uint16_t port) -> cnetmod::task<void>
{
    namespace cn = cnetmod;
    auto sock = *cn::socket::create(cn::address_family::ipv4, cn::socket_type::datagram);
    auto addr = cn::ip_address::from_string("127.0.0.1");
    (void)sock.bind(cn::endpoint{*addr, port});

    // 接收首个数据报以获取对端地址
    std::array<std::byte, 65536> first{};
    cn::endpoint peer;
    auto n = co_await cn::async_recvfrom(
        ctx, sock, cn::mutable_buffer{first.data(), first.size()}, peer);

    // 创建 DTLS 会话
    cn::dtls_datagram_session session{
        ssl_ctx, ctx, sock, peer, cn::dtls_role::server};
    session.queue_datagram(cn::const_buffer{first.data(), *n});

    co_await session.async_handshake();

    std::array<std::byte, 4096> plain{};
    auto rd = co_await session.async_read(cn::mutable_buffer{plain.data(), plain.size()});
    if (rd) co_await session.async_write(cn::const_buffer{plain.data(), *rd});

    co_await session.async_shutdown();
}

#endif
```

## 场景：kTLS 内核加速

```cpp
// 启用 kTLS（仅 Linux，需要内核和 OpenSSL 版本支持）
auto ssl_ctx = *cnetmod::ssl_context::server();
ssl_ctx.set_kernel_tls(true);

// 握手后检查是否实际激活
if (stream.kernel_tls_active()) {
    // 内核态 TLS TX 已激活，零拷贝 sendfile 可用
}
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `#ifdef CNETMOD_HAS_SSL` 保护 SSL 代码 | 无条件使用 SSL API（编译可能失败） |
| 客户端握手前调用 `set_hostname` | 握手后才设置 SNI |
| 先 `set_connect_state` / `set_accept_state` 再握手 | 跳过状态设置直接握手 |
| 用 `async_shutdown` 优雅关闭 TLS | 直接 `close` socket 跳过 TLS close_notify |
| 服务端用 `configure_alpn_server` 按优先级排列 | 客户端和服务端都用相同的 ALPN 调用 |

## 参考源码
- `src/core/ssl.cppm` — ssl_context、ssl_stream、kTLS、ALPN
- `src/core/dtls.cppm` — dtls_datagram_session、dtls_role
- `examples/core/ssl_echo_server.cpp` — TLS Echo 服务端示例
- `examples/core/dtls_echo_server.cpp` — DTLS Echo 服务端示例
