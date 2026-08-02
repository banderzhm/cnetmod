# Network I/O

> 提供异步网络操作基础设施：io_context 事件循环、协程异步读写函数及可取消操作支持。

**import**: `import cnetmod.io;` + `import cnetmod.executor;`
**源码**: `src/io/io_context.cppm`, `src/io/io_operation.cppm`, `src/executor/async_op.cppm`

## 场景导航
- 我要创建并运行事件循环 → [看这里](#场景创建事件循环)
- 我要异步接受 TCP 连接 → [看这里](#场景async_accept)
- 我要异步发起 TCP 连接 → [看这里](#场景async_connect)
- 我要异步读写 TCP 数据 → [看这里](#场景async_read--async_write)
- 我要按分隔符读取数据 → [看这里](#场景async_read_until)
- 我要发送/接收 UDP 数据报 → [看这里](#场景async_recvfrom--async_sendto)
- 我要取消进行中的异步操作 → [看这里](#场景取消操作)
- 我要在事件循环线程执行协程 → [看这里](#场景post_awaitable)

## API 参考

### `io_context`
**签名**: `export class io_context`

I/O 执行上下文抽象基类，封装平台特定 I/O 多路复用机制（IOCP/io_uring/epoll/kqueue）。不可拷贝、不可移动。

| 方法 | 签名 | 说明 |
|------|------|------|
| `run` | `virtual void run() = 0` | 阻塞运行事件循环直到停止 |
| `run_one` | `virtual auto run_one() -> std::size_t = 0` | 运行一次事件循环，返回处理事件数 |
| `poll` | `virtual auto poll() -> std::size_t = 0` | 非阻塞轮询就绪事件 |
| `stop` | `virtual void stop() = 0` | 停止事件循环 |
| `stopped` | `virtual auto stopped() const noexcept -> bool = 0` | 是否已停止 |
| `restart` | `virtual void restart() = 0` | 重置上下文（停止后可重新运行） |
| `post` (协程) | `void post(std::coroutine_handle<> h)` | 投递协程到事件循环（线程安全，无锁） |
| `post` (回调) | `void post(void (*fn)(void*), void* arg, void (*cleanup)(void*) = nullptr)` | 投递回调（零协程开销） |

### `make_io_context`
**签名**: `export auto make_io_context() -> std::unique_ptr<io_context>`

创建平台默认的 io_context 实例。

### `post_awaitable`
**签名**: `export struct post_awaitable`

```cpp
co_await post_awaitable{ctx};
// 当前协程切换到 io_context 事件循环线程执行
```

### `post_node`
**签名**: `export struct post_node`

底层投递队列节点，支持两种模式：
1. 协程模式：`coroutine` 字段
2. 回调模式：`callback(callback_arg)` 函数指针

| 字段 | 类型 | 说明 |
|------|------|------|
| `coroutine` | `std::coroutine_handle<>` | 协程句柄 |
| `callback` | `void (*)(void*)` | 回调函数 |
| `callback_arg` | `void*` | 回调参数 |
| `callback_cleanup` | `void (*)(void*)` | 未投递时的清理函数 |
| `heap_owned` | `bool` | 投递后是否自动 delete |

### `io_op_type` 枚举
**签名**: `export enum class io_op_type`

| 值 | 说明 |
|---|------|
| `accept` | 接受连接 |
| `connect` | 发起连接 |
| `read` | 网络读取 |
| `write` | 网络写入 |
| `close` | 关闭 |
| `file_read` | 文件读取 |
| `file_write` | 文件写入 |
| `file_flush` | 文件刷新 |

### `io_result`
**签名**: `export struct io_result`

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | `std::error_code` | 错误码 |
| `bytes_transferred` | `std::size_t` | 传输字节数 |
| `success` | `auto success() const noexcept -> bool` | 是否成功 |

### 异步网络操作函数

所有异步操作均返回 `task<expected<T, error_code>>`，在协程中使用 `co_await` 调用。每个操作都有可选的 `cancel_token` 取消版本。

#### `async_accept()`
**签名**:
```cpp
export auto async_accept(io_context& ctx, socket& listener)
    -> task<expected<socket, error_code>>;
export auto async_accept(io_context& ctx, socket& listener, cancel_token& token)
    -> task<expected<socket, error_code>>;
```

#### `async_connect()`
**签名**:
```cpp
export auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<expected<void, error_code>>;
export auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
    cancel_token& token) -> task<expected<void, error_code>>;
```

#### `async_read()` / `async_write()`
**签名**:
```cpp
export auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<expected<size_t, error_code>>;
export auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
    cancel_token& token) -> task<expected<size_t, error_code>>;

export auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<expected<size_t, error_code>>;
export auto async_write(io_context& ctx, socket& sock, const_buffer buf,
    cancel_token& token) -> task<expected<size_t, error_code>>;
```

#### `async_write_all()`
**签名**:
```cpp
export auto async_write_all(io_context& ctx, socket& sock, const_buffer buf)
    -> task<expected<void, error_code>>;
export auto async_write_all(io_context& ctx, socket& sock, const_buffer buf,
    cancel_token& token) -> task<expected<void, error_code>>;
```

#### `async_read_until()`
**签名**（字符串分隔符）:
```cpp
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    std::string_view delimiter,
    size_t max_bytes = numeric_limits<size_t>::max(),
    size_t read_chunk_size = 4096)
    -> task<expected<size_t, error_code>>;
```
也有 `char delimiter` 版本和 `cancel_token` 版本。返回从缓冲区起始到分隔符的字节数，不消费数据。

#### `async_recvfrom()` / `async_sendto()`
**签名**:
```cpp
export auto async_recvfrom(io_context& ctx, socket& sock,
    mutable_buffer buf, endpoint& peer)
    -> task<expected<size_t, error_code>>;

export auto async_sendto(io_context& ctx, socket& sock,
    const_buffer buf, const endpoint& peer)
    -> task<expected<size_t, error_code>>;
```
均有 `cancel_token` 取消版本。

## 场景：创建事件循环

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

namespace cn = cnetmod;

auto run_app(cn::io_context& ctx) -> cn::task<void>
{
    // 应用逻辑...
    ctx.stop();
}

auto main() -> int {
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run_app(*ctx));
    ctx->run(); // 阻塞直到 ctx.stop()
    return 0;
}
```

## 场景：async_accept

```cpp
auto accept_loop(cn::io_context& ctx, cn::socket& listener) -> cn::task<void>
{
    for (;;) {
        auto r = co_await cn::async_accept(ctx, listener);
        if (!r) break;
        // *r 是新连接的 socket
        cn::spawn(ctx, handle_client(ctx, std::move(*r)));
    }
}
```

## 场景：async_connect

```cpp
auto sock = *cn::socket::create(cn::address_family::ipv4, cn::socket_type::stream);
auto ep = cn::endpoint{cn::ipv4_address::loopback(), 8080};
auto cr = co_await cn::async_connect(ctx, sock, ep);
if (!cr) { /* 连接失败 */ co_return; }
```

## 场景：async_read / async_write

```cpp
// 读取
std::array<std::byte, 1024> buf{};
auto n = co_await cn::async_read(ctx, sock, cn::buffer(buf));
if (n) std::println("read {} bytes", *n);

// 写入
auto msg = std::string_view{"Hello"};
auto w = co_await cn::async_write(ctx, sock, cn::buffer(msg));
```

## 场景：async_read_until

```cpp
cn::dynamic_buffer dyn;
// 读到 "\r\n" 为止
auto n = co_await cn::async_read_until(ctx, sock, dyn, std::string_view{"\r\n"});
if (n) {
    auto data = dyn.data(); // 包含分隔符的完整行
    dyn.consume(*n);         // 消费已处理的数据
}
```

## 场景：async_recvfrom / async_sendto

```cpp
std::array<std::byte, 1024> buf{};
cn::endpoint peer;
auto n = co_await cn::async_recvfrom(ctx, sock, cn::buffer(buf), peer);
if (n) std::println("from {}: {} bytes", peer.to_string(), *n);

co_await cn::async_sendto(ctx, sock, cn::buffer(std::string_view{"ACK"}), peer);
```

## 场景：取消操作

```cpp
cn::cancel_token token;
// 在另一个协程中取消
token.cancel();
// async_read 会返回 cancelled 错误
auto r = co_await cn::async_read(ctx, sock, buf, token);
```

## 场景：post_awaitable

```cpp
auto worker(cn::io_context& ctx) -> cn::task<void>
{
    // 切换到 io_context 线程执行
    co_await cn::post_awaitable{ctx};
    // 此处代码在事件循环线程运行
}
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `make_io_context()` 创建平台默认实例 | 直接 new 具体实现类 |
| 所有 async 函数统一用 `co_await` 调用 | 混用同步/异步操作 |
| 检查返回值 `expected<T, error_code>` | 忽略错误码假设操作成功 |
| 用 `cancel_token` 优雅取消操作 | 直接关闭 socket 来中断等待 |
| `async_read_until` 后调用 `consume` | 忘记消费已处理的数据导致内存增长 |
| `async_write_all` 确保全部写完 | 用 `async_write` 假设一次写完 |

## 参考源码
- `src/io/io_context.cppm` — io_context 事件循环、post_awaitable、make_io_context
- `src/io/io_operation.cppm` — io_op_type、io_result、io_operation 基类
- `src/executor/async_op.cppm` — 所有异步网络/文件/串口/定时器操作函数
- `examples/core/echo_server.cpp` — TCP Echo 完整示例
