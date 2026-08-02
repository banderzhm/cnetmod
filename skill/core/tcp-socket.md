# TCP Socket

> 提供跨平台 socket 封装、IP 地址/端点类型以及 TCP/UDP 协议层抽象。

**import**: `import cnetmod.core;` + `import cnetmod.protocol.tcp;` / `import cnetmod.protocol.udp;`
**源码**: `src/core/socket.cppm`, `src/core/address.cppm`, `src/protocol/tcp.cppm`, `src/protocol/udp.cppm`

## 场景导航
- 我要创建 TCP 服务端（监听+接受连接） → [看这里](#场景tcp-服务端)
- 我要创建 TCP 客户端（发起连接） → [看这里](#场景tcp-客户端)
- 我要发送/接收 UDP 数据报 → [看这里](#场景udp-数据报)
- 我要解析或构造 IP 地址 → [看这里](#场景ip-地址操作)
- 我要配置 socket 选项（复用地址、TCP_NODELAY 等） → [看这里](#场景socket-选项)

## API 参考

### `address_family` 枚举
**签名**: `export enum class address_family`
| 值 | 说明 |
|---|------|
| `ipv4` | IPv4 地址族 |
| `ipv6` | IPv6 地址族 |
| `unspecified` | 未指定 |

### `socket_type` 枚举
**签名**: `export enum class socket_type`
| 值 | 说明 |
|---|------|
| `stream` | TCP 流式套接字 |
| `datagram` | UDP 数据报套接字 |

### `ipv4_address`
**签名**: `export class ipv4_address`

| 方法 | 签名 | 说明 |
|------|------|------|
| 默认构造 | `constexpr ipv4_address() noexcept` | 0.0.0.0 |
| 四字节构造 | `constexpr ipv4_address(uint8_t a, b, c, d) noexcept` | 从 4 字节构造 |
| `from_string` | `static auto from_string(string_view str) -> expected<ipv4_address, error_code>` | 解析字符串 |
| `to_string` | `auto to_string() const -> string` | 转为字符串 |
| `is_loopback` | `constexpr auto is_loopback() const noexcept -> bool` | 是否 127.0.0.0/8 |
| `is_any` | `constexpr auto is_any() const noexcept -> bool` | 是否 0.0.0.0 |
| `loopback` | `static constexpr auto loopback() noexcept -> ipv4_address` | 127.0.0.1 |
| `any` | `static constexpr auto any() noexcept -> ipv4_address` | 0.0.0.0 |
| `native` | `auto native() const noexcept -> const in_addr&` | 底层原生地址 |

### `ipv6_address`
**签名**: `export class ipv6_address`

| 方法 | 签名 | 说明 |
|------|------|------|
| 默认构造 | `constexpr ipv6_address() noexcept` | :: |
| `from_string` | `static auto from_string(string_view str) -> expected<ipv6_address, error_code>` | 解析字符串 |
| `to_string` | `auto to_string() const -> string` | 转为字符串 |
| `is_loopback` | `auto is_loopback() const noexcept -> bool` | 是否 ::1 |
| `loopback` | `static auto loopback() noexcept -> ipv6_address` | ::1 |
| `any` | `static constexpr auto any() noexcept -> ipv6_address` | :: |
| `from_native` | `static auto from_native(const in6_addr&) noexcept -> ipv6_address` | 从原生地址构造 |
| `native` | `auto native() const noexcept -> const in6_addr&` | 底层原生地址 |

### `ip_address`
**签名**: `export class ip_address`

通用 IP 地址（IPv4 或 IPv6 联合体）。

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `ip_address(ipv4_address addr) noexcept` | 从 IPv4 构造 |
| 构造 | `ip_address(ipv6_address addr) noexcept` | 从 IPv6 构造 |
| `from_string` | `static auto from_string(string_view str) -> expected<ip_address, error_code>` | 自动识别 IPv4/IPv6 |
| `to_string` | `auto to_string() const -> string` | 转为字符串 |
| `family` | `auto family() const noexcept -> address_family` | 地址族 |
| `is_v4` / `is_v6` | `auto is_v4() const noexcept -> bool` | 类型判断 |
| `to_v4` / `to_v6` | `auto to_v4() const -> const ipv4_address&` | 获取具体类型 |

### `endpoint`
**签名**: `export class endpoint`

网络端点 = IP 地址 + 端口号。

**构造**:
- `endpoint() noexcept` — 默认端点
- `endpoint(ip_address addr, std::uint16_t port) noexcept` — 指定地址和端口

| 方法 | 签名 | 说明 |
|------|------|------|
| `address` | `auto address() const noexcept -> const ip_address&` | 获取地址 |
| `port` | `auto port() const noexcept -> uint16_t` | 获取端口 |
| `set_address` | `void set_address(ip_address addr) noexcept` | 设置地址 |
| `set_port` | `void set_port(uint16_t p) noexcept` | 设置端口 |
| `to_string` | `auto to_string() const -> string` | 转为字符串 |

### `socket_options`
**签名**: `export struct socket_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `reuse_address` | `bool` | `false` | SO_REUSEADDR |
| `reuse_port` | `bool` | `false` | SO_REUSEPORT |
| `non_blocking` | `bool` | `true` | 非阻塞模式 |
| `no_delay` | `bool` | `false` | TCP_NODELAY |
| `ipv6_only` | `optional<bool>` | `nullopt` | IPV6_V6ONLY |
| `recv_buffer_size` | `int` | `0` | 接收缓冲区（0=系统默认） |
| `send_buffer_size` | `int` | `0` | 发送缓冲区（0=系统默认） |

### `socket`
**签名**: `export class socket`

跨平台 socket 封装（RAII），不可拷贝，仅可移动。

| 方法 | 签名 | 说明 |
|------|------|------|
| `create` | `static auto create(address_family, socket_type) -> expected<socket, error_code>` | 创建 socket |
| `from_native` | `static auto from_native(native_handle_t) noexcept -> socket` | 从原生句柄接管 |
| `bind` | `auto bind(const endpoint&) -> expected<void, error_code>` | 绑定地址 |
| `listen` | `auto listen(int backlog = 128) -> expected<void, error_code>` | 监听 |
| `set_non_blocking` | `auto set_non_blocking(bool) -> expected<void, error_code>` | 设置非阻塞 |
| `apply_options` | `auto apply_options(const socket_options&) -> expected<void, error_code>` | 应用选项 |
| `local_endpoint` | `auto local_endpoint() const -> expected<endpoint, error_code>` | 本地端点 |
| `remote_endpoint` | `auto remote_endpoint() const -> expected<endpoint, error_code>` | 远端端点 |
| `join_multicast_group` | `auto join_multicast_group(const ip_address&, ...) -> expected<void, error_code>` | 加入组播组 |
| `leave_multicast_group` | `auto leave_multicast_group(const ip_address&, ...) -> expected<void, error_code>` | 离开组播组 |
| `set_multicast_hops` | `auto set_multicast_hops(address_family, int) -> expected<void, error_code>` | 组播 TTL |
| `set_multicast_loopback` | `auto set_multicast_loopback(address_family, bool) -> expected<void, error_code>` | 组播回环 |
| `close` | `void close() noexcept` | 关闭 socket |
| `shutdown_send` | `void shutdown_send() noexcept` | 关闭发送方向 |
| `shutdown_both` | `void shutdown_both() noexcept` | 关闭双向 |
| `native_handle` | `auto native_handle() const noexcept -> native_handle_t` | 获取原生句柄 |
| `family` | `auto family() const noexcept -> address_family` | 获取地址族 |
| `release` | `auto release() noexcept -> native_handle_t` | 释放所有权（不关闭） |
| `is_open` | `auto is_open() const noexcept -> bool` | 是否有效 |

### `tcp::acceptor`
**签名**: `export class acceptor` (命名空间 `cnetmod::tcp`)

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit acceptor(io_context& ctx)` | 绑定 io_context |
| `open` | `auto open(const endpoint&, const socket_options& = {}) -> expected<void, error_code>` | 打开并绑定监听 |
| `close` | `void close() noexcept` | 关闭 |
| `is_open` | `auto is_open() const noexcept -> bool` | 是否打开 |
| `native_socket` | `auto native_socket() noexcept -> socket&` | 获取底层 socket |
| `context` | `auto context() noexcept -> io_context&` | 获取关联 io_context |

### `tcp::connection`
**签名**: `export class connection` (命名空间 `cnetmod::tcp`)

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit connection(io_context& ctx)` | 仅绑定 io_context |
| 构造 | `connection(io_context& ctx, socket sock)` | 从已有 socket 构造 |
| `remote_endpoint` | `auto remote_endpoint() const -> expected<endpoint, error_code>` | 远端端点 |
| `local_endpoint` | `auto local_endpoint() const -> expected<endpoint, error_code>` | 本地端点 |
| `close` | `void close() noexcept` | 关闭连接 |
| `is_open` | `auto is_open() const noexcept -> bool` | 是否打开 |
| `native_socket` | `auto native_socket() noexcept -> socket&` | 获取底层 socket |
| `context` | `auto context() noexcept -> io_context&` | 获取关联 io_context |

### `udp::udp_socket`
**签名**: `export class udp_socket` (命名空间 `cnetmod::udp`)

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit udp_socket(io_context& ctx)` | 绑定 io_context |
| `open` | `auto open(const endpoint&, const socket_options& = {}) -> expected<void, error_code>` | 打开并绑定 |
| `open` | `auto open(address_family = ipv4) -> expected<void, error_code>` | 仅打开（用于发送） |
| `close` | `void close() noexcept` | 关闭 |
| `is_open` | `auto is_open() const noexcept -> bool` | 是否打开 |
| `native_socket` | `auto native_socket() noexcept -> socket&` | 获取底层 socket |
| `context` | `auto context() noexcept -> io_context&` | 获取关联 io_context |

## 场景：TCP 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;

namespace cn = cnetmod;

auto accept_loop(cn::io_context& ctx, cn::tcp::acceptor& acc) -> cn::task<void>
{
    for (;;) {
        auto r = co_await cn::async_accept(ctx, acc.native_socket());
        if (!r) break;
        cn::spawn(ctx, handle_client(ctx, std::move(*r)));
    }
}

auto main() -> int {
    cn::net_init net;
    auto ctx = cn::make_io_context();

    cn::tcp::acceptor acc(*ctx);
    auto ep = cn::endpoint{cn::ipv4_address::loopback(), 8080};
    acc.open(ep, cn::socket_options{.reuse_address = true});

    cn::spawn(*ctx, accept_loop(*ctx, acc));
    ctx->run();
}
```

## 场景：TCP 客户端

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

namespace cn = cnetmod;

auto run_client(cn::io_context& ctx) -> cn::task<void>
{
    auto sock = *cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
    auto ep = cn::endpoint{cn::ipv4_address::loopback(), 8080};

    auto cr = co_await cn::async_connect(ctx, sock, ep);
    if (!cr) co_return;

    auto msg = std::string_view{"Hello"};
    co_await cn::async_write(ctx, sock, cn::buffer(msg));

    std::array<std::byte, 256> buf{};
    auto rr = co_await cn::async_read(ctx, sock, cn::buffer(buf));
    if (rr) std::println("recv {} bytes", *rr);

    sock.close();
}
```

## 场景：UDP 数据报

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.udp;

namespace cn = cnetmod;

auto run_udp(cn::io_context& ctx) -> cn::task<void>
{
    cn::udp::udp_socket udp(ctx);
    auto ep = cn::endpoint{cn::ipv4_address::any(), 9000};
    udp.open(ep);

    std::array<std::byte, 1024> buf{};
    cn::endpoint peer;
    auto n = co_await cn::async_recvfrom(ctx, udp.native_socket(),
        cn::mutable_buffer{buf.data(), buf.size()}, peer);
    if (n) std::println("recv {} bytes from {}", *n, peer.to_string());

    auto reply = std::string_view{"ACK"};
    co_await cn::async_sendto(ctx, udp.native_socket(),
        cn::buffer(reply), peer);
}
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `tcp::acceptor` 封装监听 socket | 直接用原生 `socket` 忘记设非阻塞 |
| 使用 `socket_options` 结构统一配置 | 手动调 `setsockopt` 平台 API |
| 用 `ip_address::from_string` 自动识别 v4/v6 | 假设地址一定是 IPv4 |
| 用 `udp::udp_socket` 做数据报通信 | 用 `socket_type::stream` 创建 UDP socket |
| 检查 `is_open()` 再操作 | 关闭后继续使用 socket |

## 参考源码
- `src/core/socket.cppm` — socket 类、socket_type、socket_options
- `src/core/address.cppm` — ipv4_address、ipv6_address、ip_address、endpoint、address_family
- `src/protocol/tcp.cppm` — tcp::acceptor、tcp::connection
- `src/protocol/udp.cppm` — udp::udp_socket
- `examples/core/echo_server.cpp` — TCP Echo 完整示例
