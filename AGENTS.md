<!-- GENERATED FILE: DO NOT EDIT DIRECTLY. -->
# cnetmod Repository Instructions
This file is generated from every `skill/**/*.md` file. Edit the source files and run `python tools/generate_agents.py`; use `python tools/generate_agents.py --check` to verify it is current.
## Included Sources
- `skill/SKILL.md`
- `skill/core/buffer.md`
- `skill/core/file-io.md`
- `skill/core/logging.md`
- `skill/core/network-io.md`
- `skill/core/process.md`
- `skill/core/ssl-tls.md`
- `skill/core/tcp-socket.md`
- `skill/core/utils-error.md`
- `skill/coro/coroutine.md`
- `skill/coro/executor-bridge.md`
- `skill/coro/timer-retry.md`
- `skill/database/database-orm.md`
- `skill/database/mongodb.md`
- `skill/database/mysql.md`
- `skill/database/postgresql.md`
- `skill/database/redis.md`
- `skill/http/http-client.md`
- `skill/http/http-middleware.md`
- `skill/http/http-server.md`
- `skill/http/http3-quic.md`
- `skill/infra/application.md`
- `skill/infra/architecture.md`
- `skill/infra/code-style.md`
- `skill/infra/module-conventions.md`
- `skill/infra/new-module-guide.md`
- `skill/infra/observability.md`
- `skill/infra/windows-build.md`
- `skill/integration/c-api.md`
- `skill/protocols/amqp091.md`
- `skill/protocols/amqp10.md`
- `skill/protocols/coap.md`
- `skill/protocols/grpc.md`
- `skill/protocols/kafka.md`
- `skill/protocols/modbus.md`
- `skill/protocols/mqtt.md`
- `skill/protocols/openai-mail-dns.md`
- `skill/protocols/raft.md`
- `skill/protocols/socks5.md`
- `skill/protocols/websocket.md`
- `skill/security/security-jwt.md`

<!-- BEGIN SOURCE: skill/SKILL.md -->
# Source: `skill/SKILL.md`

# cnetmod AI Skill 索引

> C++23 异步网络应用开发框架，基于 Modules + 协程 + io_uring/IOCP。

## 核心原则

1. `import std;` — 禁止 `#include` 标准库头
2. `std::expected<T, std::error_code>` — 统一错误处理
3. `task<T>` + `co_await` — 统一异步模型
4. `logger::trace/debug/info/warn/error/critical{"...", args}` — **唯一日志输出方式**，禁止 `std::println`、`std::cout`、`iostream`、`printf` 等任何其他输出方式
5. 优先用 cnetmod 已有组件
6. 协程环境**只准用协程锁**（`async_mutex`, `async_shared_mutex`, `async_semaphore`），**禁止** `std::mutex`、`std::shared_mutex`、`std::condition_variable` 等线程同步原语
7. 标准库符号必须加 `std::` 前缀（如 `std::size_t`、`std::string_view`、`std::vector`），模块编译时省略前缀会导致识别错误
8. 字符集转换必须使用 `cnetmod.utils` 中的转换工具，禁止自行实现编码转换
9. 程序入口**必须**先创建 `cnetmod::net_init net;`（RAII），否则 Windows 平台 socket 不可用
10. 耗时 CPU 操作和兼容其他协程库必须通过 executor（`thread_pool`/`spawn_on`/`io_scheduler`）和 bridge（`blocking_invoke`/`from_awaitable`）接入，**禁止在协程中同步阻塞** — 参见 [executor-bridge.md](coro/executor-bridge.md)
11. C++23 模块的**导入可见性必须显式声明** — 传递 `import` **不会自动继承**可见性，在模块 A 中使用模块 B 的符号前必须直接 `import cnetmod.xxx` — 否则 Clang 报错 `declaration of 'X' must be imported from module 'Y' before it is required` — 参见 [module-conventions.md](infra/module-conventions.md)
12. **接口与实现必须分离，架构必须清晰** — `.cppm` 仅声明并导出公开接口，非模板实现放入对应 `.cpp`；模板及因 C++ 实例化规则必须可见的实现保留在模块接口中。功能设计应从 GoF 23 种设计模式中选择与问题匹配的模式，明确职责、依赖方向和扩展边界；禁止为套用模式而堆叠无效抽象 — 参见 [module-conventions.md](infra/module-conventions.md) 与 [new-module-guide.md](infra/new-module-guide.md)

## 我要做 X → 看哪个文件

### 基础设施

| 我想… | 看这个文件 |
|-------|-----------|
| 了解项目架构、目录结构、模块清单 | [architecture.md](infra/architecture.md) |
| 创建开箱即用的 HTTP/OTEL 应用 | [application.md](infra/application.md) |
| 了解模块/文件命名约定、export 规则 | [module-conventions.md](infra/module-conventions.md) |
| 了解代码风格、clang-format、命名规范 | [code-style.md](infra/code-style.md) |
| 接入 OpenTelemetry、链路追踪和指标 | [observability.md](infra/observability.md) |
| 新增一个模块或协议 | [new-module-guide.md](infra/new-module-guide.md) |

### 核心网络

| 我想… | 看这个文件 |
|-------|-----------|
| 缓冲区、字节序、二进制读写 | [buffer.md](core/buffer.md) |
| TCP/UDP socket 连接、监听、收发 | [tcp-socket.md](core/tcp-socket.md) |
| SSL/TLS/DTLS 加密通信 | [ssl-tls.md](core/ssl-tls.md) |
| 异步 IO 操作（read/write/accept/connect） | [network-io.md](core/network-io.md) |
| 异步文件读写、send_file | [file-io.md](core/file-io.md) |
| 启动子进程并通过标准输入输出通信 | [process.md](core/process.md) |
| 日志初始化、级别、文件输出 | [logging.md](core/logging.md) |
| 错误码、工具函数 | [utils-error.md](core/utils-error.md) |

### 协程与并发

| 我想… | 看这个文件 |
|-------|-----------|
| task/spawn/channel/mutex/semaphore/wait_group | [coroutine.md](coro/coroutine.md) |
| 定时器、超时、重试、断路器、速率限制 | [timer-retry.md](coro/timer-retry.md) |
| 执行器、线程池、阻塞 API/awaitable 桥接 | [executor-bridge.md](coro/executor-bridge.md) |

### HTTP

| 我想… | 看这个文件 |
|-------|-----------|
| HTTP 服务器（路由、SSE、Swagger） | [http-server.md](http/http-server.md) |
| HTTP 客户端（请求、响应、流式） | [http-client.md](http/http-client.md) |
| HTTP 中间件（认证、限流、CORS 等 17 个） | [http-middleware.md](http/http-middleware.md) |
| HTTP/3 / QUIC（H3、QPACK、ALPN） | [http3-quic.md](http/http3-quic.md) |

### 数据库

| 我想… | 看这个文件 |
|-------|-----------|
| ORM 模型定义、CRUD、迁移、查询构建器 | [database-orm.md](database/database-orm.md) |
| MySQL 协议（连接、查询、prepared statement） | [mysql.md](database/mysql.md) |
| PostgreSQL 协议 | [postgresql.md](database/postgresql.md) |
| MongoDB 协议 | [mongodb.md](database/mongodb.md) |
| Redis 协议 | [redis.md](database/redis.md) |

### 消息队列

| 我想… | 看这个文件 |
|-------|-----------|
| MQTT broker + client（v3/v5） | [mqtt.md](protocols/mqtt.md) |
| Kafka producer + consumer | [kafka.md](protocols/kafka.md) |
| AMQP 0-9-1（RabbitMQ） | [amqp091.md](protocols/amqp091.md) |
| AMQP 1.0（Artemis） | [amqp10.md](protocols/amqp10.md) |

### 其他协议

| 我想… | 看这个文件 |
|-------|-----------|
| WebSocket 服务端/客户端 | [websocket.md](protocols/websocket.md) |
| gRPC 服务端/客户端 | [grpc.md](protocols/grpc.md) |
| Modbus 工业协议 | [modbus.md](protocols/modbus.md) |
| CoAP IoT 协议 | [coap.md](protocols/coap.md) |
| Raft 分布式共识 | [raft.md](protocols/raft.md) |
| SOCKS5 代理 | [socks5.md](protocols/socks5.md) |
| OpenAI / Mail / DNS | [openai-mail-dns.md](protocols/openai-mail-dns.md) |

### 安全

| 我想… | 看这个文件 |
|-------|-----------|
| JWT 签发、验证、过期检查 | [security-jwt.md](security/security-jwt.md) |

## CMake 协议开关

| 开关 | 协议 | 依赖 |
|------|------|------|
| `-DCNETMOD_ENABLE_HTTP=ON` | HTTP/1.1 + HTTP/2 | 无 |
| `-DCNETMOD_ENABLE_QUIC=ON` | HTTP/3 / QUIC | HTTP、SSL、BoringSSL QUIC API |
| `-DCNETMOD_ENABLE_WEBSOCKET=ON` | WebSocket | HTTP |
| `-DCNETMOD_ENABLE_GRPC=ON` | gRPC | HTTP |
| `-DCNETMOD_ENABLE_MQTT=ON` | MQTT v3/v5 | HTTP, WebSocket |
| `-DCNETMOD_ENABLE_KAFKA=ON` | Kafka | 无 |
| `-DCNETMOD_ENABLE_REDIS=ON` | Redis | 无 |
| `-DCNETMOD_ENABLE_MYSQL=ON` | MySQL | 无 |
| `-DCNETMOD_ENABLE_POSTGRESQL=ON` | PostgreSQL | 无 |
| `-DCNETMOD_ENABLE_MONGODB=ON` | MongoDB | 无 |
| `-DCNETMOD_ENABLE_AMQP091=ON` | AMQP 0-9-1 | 无 |
| `-DCNETMOD_ENABLE_AMQP10=ON` | AMQP 1.0 | 无 |
| `-DCNETMOD_ENABLE_MODBUS=ON` | Modbus | 无 |
| `-DCNETMOD_ENABLE_COAP=ON` | CoAP | 无 |
| `-DCNETMOD_ENABLE_RAFT=ON` | Raft | 无 |
| `-DCNETMOD_ENABLE_SOCKS5=ON` | SOCKS5 | 无 |
| `-DCNETMOD_ENABLE_OPENAI=ON` | OpenAI | HTTP |
| `-DCNETMOD_ENABLE_MAIL=ON` | Mail | 无 |
| `-DCNETMOD_ENABLE_DNS=ON` | DNS | HTTP |
| `-DCNETMOD_ENABLE_ORM=ON` | ORM | MySQL/PostgreSQL |

> 依赖关系来源: `cmake/Protocols.cmake`。
> 例如启用 MQTT 必须同时启用 HTTP 和 WEBSOCKET；启用 OPENAI 必须同时启用 HTTP。

## Windows 构建

启用 PostgreSQL 且在 Windows 上构建时，查看 [windows-build.md](infra/windows-build.md)。其中说明 bundled ICU 的 Debug/Release 文件名，避免 Debug 链接到 Release ICU。

## 快速上手

```cpp
#include <cnetmod/config.hpp>

import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.http;

auto main() -> int
{
    logger::init("my-app", logger::level::info);

    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();

    auto work = [&]() -> cnetmod::task<void>
    {
        // 你的业务逻辑
        co_return;
    };

    cnetmod::spawn(*ctx, work());
    ctx->run();
    logger::shutdown();
    return 0;
}
```

## 文件编写规范

每个 skill 文件应遵循以下结构:

1. **标题 + 一句话描述** — 说明模块用途
2. **核心原则** — 与本项目编码规范一致
3. **API 签名** — 从 `.cppm` 源码提取，不猜测
4. **参数说明表** — 列出每个参数的含义
5. **可运行示例** — 使用 `import std;`，不用 `#include` 标准库头
6. **CMake 启用方式** — 说明所需的编译开关

## AGENTS.md 同步

根目录的 `AGENTS.md` 是面向 AI 工具的完整规则文件，由全部
`skill/**/*.md` 自动合并生成。修改、新增或删除任何 skill Markdown
文件后，必须重新生成并提交 `AGENTS.md`：

```bash
python tools/generate_agents.py
```

可在提交前检查生成文件是否为最新状态：

```bash
python tools/generate_agents.py --check
```

不要直接编辑生成后的 `AGENTS.md`；所有规则变更必须写入对应的 skill
源文件，避免两套说明发生偏差。
<!-- END SOURCE: skill/SKILL.md -->

<!-- BEGIN SOURCE: skill/core/buffer.md -->
# Source: `skill/core/buffer.md`

# buffer

> 提供零拷贝缓冲区视图、动态可增长缓冲区、二进制序列化读写器及字节序转换工具。

**import**: `import cnetmod.core;` (聚合) 或 `import cnetmod.core.buffer;`
**源码**: `src/core/buffer.cppm`, `src/core/buffer_pool.cppm`

## 场景导航
- 我要传递只读/可写数据给异步 API → [看这里](#场景缓冲区视图)
- 我要接收不定长数据 → [看这里](#场景动态缓冲区)
- 我要解析/构建二进制协议报文 → [看这里](#场景二进制读写器)
- 我要做网络字节序转换 → [看这里](#场景字节序转换)
- 我要 Direct I/O 对齐内存 → [看这里](#场景对齐缓冲区)
- 我要高频复用固定大小缓冲区 → [看这里](#场景缓冲池)

## API 参考

### `const_buffer`
**签名**: `export struct const_buffer`
**成员**:
- `const void* data` — 数据指针（只读）
- `std::size_t size` — 数据大小

**构造**:
- `constexpr const_buffer() noexcept` — 默认空缓冲区
- `constexpr const_buffer(const void* p, std::size_t n) noexcept` — 从指针+大小构造
- `constexpr const_buffer(std::span<const std::byte> s) noexcept` — 从 span 构造

### `mutable_buffer`
**签名**: `export struct mutable_buffer`
**成员**:
- `void* data` — 数据指针（可写）
- `std::size_t size` — 数据大小

**构造**:
- `constexpr mutable_buffer() noexcept` — 默认空缓冲区
- `constexpr mutable_buffer(void* p, std::size_t n) noexcept` — 从指针+大小构造
- `constexpr mutable_buffer(std::span<std::byte> s) noexcept` — 从 span 构造
- `constexpr operator const_buffer() const noexcept` — 隐式转换为只读视图

### `buffer()` 工厂函数
**签名**（多个重载）:
```cpp
export constexpr auto buffer(const void* data, std::size_t size) noexcept -> const_buffer;
export constexpr auto buffer(void* data, std::size_t size) noexcept -> mutable_buffer;
export constexpr auto buffer(std::string_view sv) noexcept -> const_buffer;
export auto buffer(std::vector<std::byte>& v) noexcept -> mutable_buffer;
export auto buffer(const std::vector<std::byte>& v) noexcept -> const_buffer;
export template <std::size_t N>
constexpr auto buffer(std::array<std::byte, N>& a) noexcept -> mutable_buffer;
```

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

// 从 string_view 创建只读缓冲区
auto msg = std::string_view{"Hello, cnetmod!"};
auto buf = cnetmod::buffer(msg);

// 从 array 创建可写缓冲区
std::array<std::byte, 1024> arr{};
auto wbuf = cnetmod::buffer(arr);

// 从 vector 创建
std::vector<std::byte> vec(512);
auto mbuf = cnetmod::buffer(vec);
```

### `dynamic_buffer`
**签名**: `export class dynamic_buffer`

**构造**: `explicit dynamic_buffer(std::size_t initial_capacity = 4096)`

| 方法 | 签名 | 说明 |
|------|------|------|
| `prepare` | `auto prepare(std::size_t n) -> mutable_buffer` | 获取 n 字节可写区域 |
| `commit` | `void commit(std::size_t n) noexcept` | 确认写入 n 字节 |
| `data` | `auto data() const noexcept -> const_buffer` | 获取可读数据视图 |
| `consume` | `void consume(std::size_t n) noexcept` | 消费（丢弃）前 n 字节 |
| `readable_bytes` | `auto readable_bytes() const noexcept -> std::size_t` | 当前可读字节数 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

cnetmod::dynamic_buffer dyn;

// 准备写入区域
auto writable = dyn.prepare(128);
// ... 往 writable 写入数据 ...
dyn.commit(64); // 确认 64 字节

// 读取数据
auto readable = dyn.data();
// ... 处理 readable ...
dyn.consume(64); // 消费 64 字节
```

### `aligned_buffer`
**签名**: `export class aligned_buffer`

**构造**: `explicit aligned_buffer(std::size_t size, std::size_t alignment = 4096)`

| 方法 | 签名 | 说明 |
|------|------|------|
| `data` | `auto data() noexcept -> std::byte*` | 获取可写指针 |
| `data` | `auto data() const noexcept -> const std::byte*` | 获取只读指针 |
| `size` | `auto size() const noexcept -> std::size_t` | 缓冲区大小 |
| `alignment` | `auto alignment() const noexcept -> std::size_t` | 对齐值 |
| `writable` | `auto writable() noexcept -> mutable_buffer` | 转为可写视图 |
| `readable` | `auto readable() const noexcept -> const_buffer` | 转为只读视图 |

不可拷贝，仅可移动。配合 `open_mode::direct` 使用。

### `buffer_reader`
**签名**: `export class buffer_reader`

**构造**:
- `explicit buffer_reader(const_buffer buf) noexcept`
- `explicit buffer_reader(std::span<const std::byte> s) noexcept`

| 方法 | 签名 | 说明 |
|------|------|------|
| `remaining` | `auto remaining() const noexcept -> std::size_t` | 剩余可读字节数 |
| `position` | `auto position() const noexcept -> std::size_t` | 当前偏移 |
| `skip` | `auto skip(std::size_t n) noexcept -> bool` | 跳过 n 字节 |
| `read_bytes` | `auto read_bytes(void* dst, std::size_t n) noexcept -> bool` | 读取原始字节 |
| `read_u8` | `auto read_u8() noexcept -> std::optional<std::uint8_t>` | 读取 1 字节 |
| `read_u16_be` | `auto read_u16_be() noexcept -> std::optional<std::uint16_t>` | 大端读取 2 字节 |
| `read_u32_be` | `auto read_u32_be() noexcept -> std::optional<std::uint32_t>` | 大端读取 4 字节 |
| `read_u64_be` | `auto read_u64_be() noexcept -> std::optional<std::uint64_t>` | 大端读取 8 字节 |
| `read_u16_le` | `auto read_u16_le() noexcept -> std::optional<std::uint16_t>` | 小端读取 2 字节 |
| `read_u32_le` | `auto read_u32_le() noexcept -> std::optional<std::uint32_t>` | 小端读取 4 字节 |
| `read_u64_le` | `auto read_u64_le() noexcept -> std::optional<std::uint64_t>` | 小端读取 8 字节 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

std::array<std::byte, 64> raw{};
// ... 填充数据 ...
cnetmod::buffer_reader reader(cnetmod::buffer(raw));
auto version = reader.read_u8();
auto length = reader.read_u32_be(); // 网络字节序
auto flags = reader.read_u16_le();  // 小端序
```

### `buffer_writer`
**签名**: `export class buffer_writer`

**构造**:
- `explicit buffer_writer(mutable_buffer buf) noexcept`
- `explicit buffer_writer(std::span<std::byte> s) noexcept`

| 方法 | 签名 | 说明 |
|------|------|------|
| `remaining` | `auto remaining() const noexcept -> std::size_t` | 剩余可写字节数 |
| `written` | `auto written() const noexcept -> std::size_t` | 已写入字节数 |
| `write_bytes` | `auto write_bytes(const void* src, std::size_t n) noexcept -> bool` | 写入原始字节 |
| `write_u8` | `auto write_u8(std::uint8_t v) noexcept -> bool` | 写入 1 字节 |
| `write_u16_be` | `auto write_u16_be(std::uint16_t v) noexcept -> bool` | 大端写入 2 字节 |
| `write_u32_be` | `auto write_u32_be(std::uint32_t v) noexcept -> bool` | 大端写入 4 字节 |
| `write_u64_be` | `auto write_u64_be(std::uint64_t v) noexcept -> bool` | 大端写入 8 字节 |
| `write_u16_le` | `auto write_u16_le(std::uint16_t v) noexcept -> bool` | 小端写入 2 字节 |
| `write_u32_le` | `auto write_u32_le(std::uint32_t v) noexcept -> bool` | 小端写入 4 字节 |
| `write_u64_le` | `auto write_u64_le(std::uint64_t v) noexcept -> bool` | 小端写入 8 字节 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer;

std::array<std::byte, 256> raw{};
cnetmod::buffer_writer writer(cnetmod::buffer(raw));
writer.write_u8(0x01);
writer.write_u32_be(1024);     // 网络字节序
writer.write_u16_le(0xABCD);   // 小端序
std::println("written {} bytes", writer.written());
```

### 字节序转换函数
**签名**:
```cpp
// host <-> network (big-endian)
export constexpr auto hton(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto hton(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto hton(std::uint64_t v) noexcept -> std::uint64_t;
export constexpr auto ntoh(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto ntoh(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto ntoh(std::uint64_t v) noexcept -> std::uint64_t;

// host <-> little-endian
export constexpr auto htole(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto htole(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto htole(std::uint64_t v) noexcept -> std::uint64_t;
export constexpr auto letoh(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto letoh(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto letoh(std::uint64_t v) noexcept -> std::uint64_t;

// Generic byte swap
export constexpr auto byte_swap(std::uint16_t v) noexcept -> std::uint16_t;
export constexpr auto byte_swap(std::uint32_t v) noexcept -> std::uint32_t;
export constexpr auto byte_swap(std::uint64_t v) noexcept -> std::uint64_t;
```

### `byte_order` 枚举
**签名**: `export enum class byte_order`
| 值 | 说明 |
|---|------|
| `little_endian` | 小端字节序 |
| `big_endian` | 大端字节序 |
| `native` | 当前平台原生字节序（编译期确定） |

### `pooled_buffer` & `buffer_pool`
**签名**: `export class pooled_buffer` / `export class buffer_pool`

`buffer_pool` 提供线程安全的固定大小块分配：

**构造**: `explicit buffer_pool(std::size_t block_size = 4096, std::size_t max_blocks = 1024) noexcept`

| 方法 | 签名 | 说明 |
|------|------|------|
| `acquire` | `auto acquire() -> pooled_buffer` | 获取一个块 |
| `pool_size` | `auto pool_size() const noexcept -> std::size_t` | 当前池中可用块数 |
| `block_size` | `auto block_size() const noexcept -> std::size_t` | 块大小 |

`pooled_buffer` 是 RAII 句柄，析构时自动归还：

| 方法 | 签名 | 说明 |
|------|------|------|
| `data` | `auto data() noexcept -> void*` | 块指针 |
| `size` | `auto size() const noexcept -> std::size_t` | 块大小 |
| `valid` | `auto valid() const noexcept -> bool` | 是否有效 |
| `release` | `void release() noexcept` | 提前归还 |
| — | `operator mutable_buffer()` / `operator const_buffer()` | 隐式转换为缓冲区视图 |

**示例**:
```cpp
import std;
import cnetmod.core.buffer_pool;

cnetmod::buffer_pool pool(4096, 256);

{
    auto blk = pool.acquire();
    std::println("got block of {} bytes", blk.size());
    // 使用 blk.data() ...
} // 析构时自动归还

std::println("pool has {} free blocks", pool.pool_size());
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用 `buffer(string_view)` 创建只读缓冲区 | 手动构造 `const_buffer` 时忘记设置 size |
| `dynamic_buffer` 先 `prepare` → 写入 → `commit` | 只 `prepare` 不 `commit` 就调用 `data()` |
| 用 `buffer_reader` 解析协议报文 | 手动做指针偏移和字节序转换 |
| `aligned_buffer` 配合 `open_mode::direct` | 用 `new` 分配未对齐内存做 Direct I/O |
| `pooled_buffer` 利用 RAII 自动归还 | 长期持有 `pooled_buffer` 不释放 |

## 参考源码
- `src/core/buffer.cppm` — 缓冲区视图、动态缓冲区、读写器、字节序转换
- `src/core/buffer_pool.cppm` — 线程安全缓冲池
<!-- END SOURCE: skill/core/buffer.md -->

<!-- BEGIN SOURCE: skill/core/file-io.md -->
# Source: `skill/core/file-io.md`

# File I/O

> 提供跨平台异步文件 I/O 支持，包括 RAII 文件句柄、打开模式、Direct I/O 对齐及文件传输策略选择。

**import**: `import cnetmod.core;` + `import cnetmod.executor;`
**源码**: `src/core/file.cppm`, `src/executor/async_op.cppm`

## 场景导航
- 我要同步打开/关闭文件 → [看这里](#场景同步文件操作)
- 我要异步读写文件 → [看这里](#场景异步文件读写)
- 我要一次性读取/写入整个文件 → [看这里](#场景整体读写)
- 我要批量读写多个文件 → [看这里](#场景批量-io)
- 我要流式处理大文件 → [看这里](#场景流式管道)
- 我要将文件直接发送到 socket → [看这里](#场景零拷贝文件传输)
- 我要选择最优传输策略 → [看这里](#场景传输策略选择)
- 我要 Direct I/O 对齐内存 → [看这里](#场景direct-io)

## API 参考

### `open_mode` 枚举
**签名**: `export enum class open_mode : std::uint32_t`

| 值 | 说明 |
|---|------|
| `read` | 只读 |
| `write` | 只写 |
| `read_write` | 读写 |
| `append` | 追加 |
| `create` | 不存在则创建 |
| `truncate` | 存在则截断 |
| `create_new` | 必须不存在 |
| `direct` | 绕过平台页缓存 |

支持 `|` 和 `&` 位运算组合，以及 `has_flag(mode, flag)` 检查。

### `file_strategy` 枚举
**签名**: `export enum class file_strategy`

| 值 | 说明 |
|---|------|
| `buffered` | 缓冲 I/O（默认） |
| `direct` | Direct I/O |
| `zero_copy` | 零拷贝（sendfile/TransmitFile） |

### `file_strategy_options`
**签名**: `export struct file_strategy_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `to_socket` | `bool` | `false` | 是否发送到 socket |
| `encrypted_transport` | `bool` | `false` | 是否加密传输 |
| `requires_processing` | `bool` | `false` | 是否需要处理 |
| `allow_direct` | `bool` | `false` | 是否允许 Direct I/O |
| `direct_threshold` | `uint64_t` | `16MB` | Direct I/O 阈值 |

### `select_file_strategy()`
**签名**:
```cpp
export constexpr auto select_file_strategy(
    std::uint64_t file_size, file_strategy_options options = {}) noexcept
    -> file_strategy;
```
根据文件大小和选项自动选择最优传输策略。

### `file_stat`
**签名**: `export struct file_stat`

| 字段 | 类型 | 说明 |
|------|------|------|
| `size` | `uint64_t` | 文件大小（字节） |
| `is_regular` | `bool` | 是否普通文件 |
| `is_directory` | `bool` | 是否目录 |

### `file`
**签名**: `export class file`

跨平台文件句柄封装（RAII），不可拷贝，仅可移动。

| 方法 | 签名 | 说明 |
|------|------|------|
| `open` | `static auto open(const filesystem::path&, open_mode) -> expected<file, error_code>` | 打开文件 |
| `stat` | `static auto stat(const filesystem::path&) -> expected<file_stat, error_code>` | 获取文件状态 |
| `close` | `void close() noexcept` | 关闭文件 |
| `size` | `auto size() const -> expected<uint64_t, error_code>` | 获取文件大小 |
| `native_handle` | `auto native_handle() const noexcept -> file_handle_t` | 获取原生句柄 |
| `release` | `auto release() noexcept -> file_handle_t` | 释放所有权（不关闭） |
| `is_open` | `auto is_open() const noexcept -> bool` | 是否有效 |

### 异步文件操作函数

所有函数返回 `task<expected<T, error_code>>`，使用 `co_await` 调用。

#### `async_file_open()`
**签名**:
```cpp
export auto async_file_open(io_context& ctx, const filesystem::path& path, open_mode mode)
    -> task<expected<file, error_code>>;
export auto async_file_open(io_context& ctx, const filesystem::path& path, open_mode mode,
    cancel_token& token) -> task<expected<file, error_code>>;
```

#### `async_file_stat()`
**签名**:
```cpp
export auto async_file_stat(io_context& ctx, const filesystem::path& path)
    -> task<expected<file_stat, error_code>>;
```

#### `async_file_remove()`
**签名**:
```cpp
export auto async_file_remove(io_context& ctx, const filesystem::path& path)
    -> task<expected<void, error_code>>;
export auto async_file_remove(io_context& ctx, const filesystem::path& path,
    cancel_token& token) -> task<expected<void, error_code>>;
```

删除普通文件并恢复到传入的事件循环；目标不存在按幂等成功处理。取消只能阻止尚未提交的
操作，不能撤销已经由平台工作线程完成的删除。

#### `async_file_read()`
**签名**:
```cpp
export auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
    std::uint64_t offset = 0) -> task<expected<size_t, error_code>>;
```

#### `async_file_write()`
**签名**:
```cpp
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
    std::uint64_t offset = 0) -> task<expected<size_t, error_code>>;
```

#### `async_file_read_all()` / `async_file_write_all()`
**签名**:
```cpp
export auto async_file_read_all(io_context& ctx, const filesystem::path& path)
    -> task<expected<std::string, error_code>>;
export auto async_file_read_all(io_context& ctx, const filesystem::path& path,
    cancel_token& token) -> task<expected<std::string, error_code>>;
export auto async_file_write_all(io_context& ctx,
    const filesystem::path& path, std::string_view content)
    -> task<expected<void, error_code>>;
export auto async_file_write_all(io_context& ctx,
    const filesystem::path& path, std::string_view content, cancel_token& token)
    -> task<expected<void, error_code>>;
```

#### `async_file_close()`
**签名**:
```cpp
export auto async_file_close(io_context& ctx, file& f)
    -> task<expected<void, error_code>>;
```

#### `async_file_flush()`
**签名**:
```cpp
export auto async_file_flush(io_context& ctx, file& f)
    -> task<expected<void, error_code>>;
```

### 批量 I/O

#### `file_read_request` / `file_write_request`
**签名**:
```cpp
export struct file_read_request {
    file* source = nullptr;
    mutable_buffer destination{};
    std::uint64_t offset = 0;
};
export struct file_write_request {
    file* destination = nullptr;
    const_buffer source{};
    std::uint64_t offset = 0;
};
```

#### `async_file_read_batch()` / `async_file_write_batch()`
**签名**:
```cpp
export auto async_file_read_batch(io_context& ctx,
    std::span<const file_read_request> requests)
    -> task<std::vector<file_io_result>>;
export auto async_file_write_batch(io_context& ctx,
    std::span<const file_write_request> requests)
    -> task<std::vector<file_io_result>>;
```
其中 `file_io_result = std::expected<std::size_t, std::error_code>`。

### 流式管道

#### `file_pipeline_options`
**签名**: `export struct file_pipeline_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `offset` | `uint64_t` | `0` | 起始偏移 |
| `byte_count` | `uint64_t` | `max` | 读取字节数 |
| `chunk_size` | `size_t` | `256KB` | 分块大小 |

#### `async_file_read_pipeline()`
**签名**:
```cpp
export using file_chunk_handler = std::function<
    task<expected<void, error_code>>(const_buffer chunk, uint64_t offset)>;
export auto async_file_read_pipeline(io_context& ctx, file& source,
    file_chunk_handler handler, file_pipeline_options options = {})
    -> task<expected<uint64_t, error_code>>;
```
双缓冲流水线：处理当前块时并发读取下一块。

### 零拷贝传输

#### `async_send_file()`
**签名**:
```cpp
export auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset = 0,
    std::uint64_t byte_count = numeric_limits<uint64_t>::max())
    -> task<expected<uint64_t, error_code>>;
```

## 场景：同步文件操作

```cpp
import std;
import cnetmod.core;

// 打开文件
auto f = cnetmod::file::open("data.bin",
    cnetmod::open_mode::read_write | cnetmod::open_mode::create);
if (!f) { /* 错误处理 */ }

auto sz = f->size();
f->close();
```

## 场景：异步文件读写

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;

auto run(cnetmod::io_context& ctx) -> cnetmod::task<void>
{
    namespace cn = cnetmod;
    auto f = cn::file::open("data.bin",
        cn::open_mode::write | cn::open_mode::create | cn::open_mode::truncate);

    std::string data = "Hello, async file I/O!";
    auto wr = co_await cn::async_file_write(ctx, *f,
        cn::const_buffer{data.data(), data.size()}, 0);
    co_await cn::async_file_flush(ctx, *f);
}
```

## 场景：整体读写

```cpp
// 一次性读取整个文件为 string
auto text = co_await cnetmod::async_file_read_all(ctx, "config.json");
if (text) std::println("content: {}", *text);

// 一次性写入 string
co_await cnetmod::async_file_write_all(ctx, "output.txt", "Hello World");
```

## 场景：批量 I/O

```cpp
std::vector<cnetmod::file_read_request> requests;
// ... 填充多个读请求 ...
auto results = co_await cnetmod::async_file_read_batch(ctx, requests);
for (auto& r : results) {
    if (r) std::println("read {} bytes", *r);
}
```

## 场景：流式管道

```cpp
auto handler = [](cnetmod::const_buffer chunk, std::uint64_t offset)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    // 处理每个数据块
    process_chunk(chunk, offset);
    co_return std::expected<void, std::error_code>{};
};

cnetmod::file_pipeline_options opts{.chunk_size = 512 * 1024};
auto total = co_await cnetmod::async_file_read_pipeline(ctx, file, handler, opts);
```

## 场景：零拷贝文件传输

```cpp
auto f = *cnetmod::file::open("large.bin", cnetmod::open_mode::read);
auto sent = co_await cnetmod::async_send_file(ctx, sock, f, 0);
if (sent) std::println("sent {} bytes via zero-copy", *sent);
```

## 场景：传输策略选择

```cpp
cnetmod::file_strategy_options opts{
    .to_socket = true,
    .encrypted_transport = false,
    .requires_processing = false,
};
auto strategy = cnetmod::select_file_strategy(file_size, opts);
// strategy == file_strategy::zero_copy (未加密 socket 直传)
```

## 场景：Direct I/O

```cpp
import std;
import cnetmod.core;

// 对齐缓冲区（4096 字节对齐）
cnetmod::aligned_buffer buf(65536, 4096);

// Direct I/O 打开文件
auto f = cnetmod::file::open("data.bin",
    cnetmod::open_mode::read | cnetmod::open_mode::direct);

// 读写必须使用对齐的缓冲区和大小
auto n = co_await cnetmod::async_file_read(ctx, *f, buf.writable(), 0);
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 用 `open_mode` 位运算组合模式 | 用字符串 "r+" / "wb" 等 |
| `async_file_write` 后调用 `async_file_flush` | 假设写入立即持久化 |
| Direct I/O 使用 `aligned_buffer` | 用未对齐内存做 Direct I/O |
| 大文件用 `async_send_file` 零拷贝 | 手动 read + write 循环传输 |
| 用 `select_file_strategy` 自动选策略 | 硬编码传输方式 |
| 大文件用 `async_file_read_pipeline` 分块处理 | 用 `async_file_read_all` 加载到内存 |

## 参考源码
- `src/core/file.cppm` — file 类、open_mode、file_stat、file_strategy
- `src/executor/async_op.cppm` — async_file_open/read/write/stat/flush/close/batch/pipeline/send_file
- `src/core/buffer.cppm` — aligned_buffer（Direct I/O 对齐）
- `examples/core/async_file.cpp` — 异步文件 I/O 完整示例
<!-- END SOURCE: skill/core/file-io.md -->

<!-- BEGIN SOURCE: skill/core/logging.md -->
# Source: `skill/core/logging.md`

# Logging

> 提供结构化异步日志系统，支持控制台/文件输出、多级别过滤、文本/JSON 格式及文件轮转。

**import**: `import cnetmod.core.log;`
**源码**: `src/core/log.cppm`, `src/core/log_config.cppm`

## 场景导航
- 我要初始化日志系统 → [看这里](#场景初始化日志)
- 我要输出不同级别的日志 → [看这里](#场景输出日志)
- 我要同时输出到文件和控制台 → [看这里](#场景文件输出)
- 我要切换 JSON 格式输出 → [看这里](#场景json-格式)
- 我要运行时调整日志级别 → [看这里](#场景动态调整级别)
- 我要自定义日志 sink → [看这里](#场景自定义-sink)

## API 参考

### `logger::level` 枚举
**签名**: `export enum class level`（在 `logger` 命名空间中）

| 值 | 说明 |
|---|------|
| `trace` | 最详细的追踪信息 |
| `debug` | 调试信息 |
| `info` | 一般信息 |
| `warn` | 警告 |
| `error` | 错误 |
| `critical` | 严重错误 |
| `off` | 关闭所有日志 |

### `logger::output_format` 枚举
**签名**: `export enum class output_format`

| 值 | 说明 |
|---|------|
| `text` | 纯文本格式 |
| `json` | JSON 格式 |

### `logger::rotation_options`
**签名**: `export struct rotation_options`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_file_size` | `size_t` | `0` | 单文件最大字节数（0=不按大小轮转） |
| `max_files` | `size_t` | `0` | 保留的最大轮转文件数（0=全部保留） |
| `daily` | `bool` | `false` | 是否按天轮转 |

### `logger::sink`
**签名**: `using sink = std::function<void(std::string_view)>`

自定义日志输出回调函数类型。

### 初始化函数

#### `logger::init`
**签名**:
```cpp
void init(const std::string& name = "cnetmod",
    level lv = level::info,
    output_format fmt = output_format::text);
```
**参数**:
- `name` — 日志器名称
- `lv` — 最低输出级别
- `fmt` — 输出格式

#### `logger::init_with_file`
**签名**:
```cpp
void init_with_file(const std::string& name, const std::string& filepath,
    level lv = level::info,
    output_format fmt = output_format::text,
    bool echo_console = true);
```
**参数**:
- `name` — 日志器名称
- `filepath` — 日志文件路径
- `lv` — 最低输出级别
- `fmt` — 输出格式
- `echo_console` — 是否同时输出到控制台

### 配置函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `set_level` | `void set_level(level lv)` | 设置最低日志级别 |
| `set_format` | `void set_format(output_format fmt)` | 设置输出格式 |
| `set_console_enabled` | `void set_console_enabled(bool enabled)` | 启用/禁用控制台输出 |
| `set_file_output` | `auto set_file_output(const std::string& filepath, bool append = true) -> bool` | 配置文件输出 |
| `disable_file_output` | `void disable_file_output()` | 禁用文件输出 |
| `set_async_queue_limit` | `void set_async_queue_limit(std::size_t max_queue)` | 设置异步队列上限 |
| `dropped_messages` | `auto dropped_messages() -> std::uint64_t` | 获取丢弃的消息计数 |
| `flush` | `void flush()` | 刷新缓冲区 |
| `shutdown` | `void shutdown()` | 关闭日志系统 |

### 级别输出

每个级别都是一个结构体，支持 `std::format` 风格的格式化字符串和纯字符串两种调用方式。

**签名**:
```cpp
struct trace { /* level::trace */ };
struct debug { /* level::debug */ };
struct info  { /* level::info  */ };
struct warn  { /* level::warn  */ };
struct error { /* level::error */ };
struct critical { /* level::critical */ };
```

**用法**:
```cpp
// std::format 风格
logger::info("Server started on port {}", port);
logger::error("Connection failed: {}", error.message());

// 纯字符串
logger::debug("entering main loop");
```

所有级别输出自动捕获 `std::source_location`，记录调用位置。

## 场景：初始化日志

```cpp
import std;
import cnetmod.core.log;

// 默认初始化（名称 "cnetmod"，info 级别，纯文本）
logger::init();

// 自定义名称和级别
logger::init("my_app", logger::level::debug);

// 带文件输出
logger::init_with_file("my_app", "app.log",
    logger::level::debug, logger::output_format::text, true);
```

## 场景：输出日志

```cpp
import std;
import cnetmod.core.log;

logger::trace("entering function {}", __func__);
logger::debug("cache hit ratio: {:.2f}", 0.95);
logger::info("listening on {}:{}", host, port);
logger::warn("deprecated API called");
logger::error("failed to open file: {}", path);
logger::critical("out of memory!");
```

## 场景：文件输出

```cpp
import std;
import cnetmod.core.log;

// 初始化时配置文件
logger::init_with_file("server", "/var/log/server.log",
    logger::level::info, logger::output_format::text,
    /* echo_console = */ true);

// 运行时切换文件
bool ok = logger::set_file_output("/var/log/server2.log", /* append = */ true);

// 禁用文件输出
logger::disable_file_output();
```

## 场景：JSON 格式

```cpp
import std;
import cnetmod.core.log;

logger::init("api_server", logger::level::info, logger::output_format::json);
logger::info("request handled in {}ms", elapsed);
// 输出 JSON 格式的日志条目
```

## 场景：动态调整级别

```cpp
import std;
import cnetmod.core.log;

// 运行时提高日志级别（减少输出）
logger::set_level(logger::level::warn);

// 运行时降低日志级别（增加输出）
logger::set_level(logger::level::trace);

// 关闭控制台，只写文件
logger::set_console_enabled(false);
```

## 场景：自定义 sink

```cpp
import std;
import cnetmod.core.log;

// 自定义输出目标（如发送到远程日志服务）
logger::sink my_sink = [](std::string_view msg) {
    // 自定义处理逻辑
    send_to_remote(msg);
};
```

## 场景：异步队列管理

```cpp
import std;
import cnetmod.core.log;

// 限制异步队列大小，防止内存溢出
logger::set_async_queue_limit(10000);

// 监控丢弃的消息数
auto dropped = logger::dropped_messages();
if (dropped > 0) {
    std::println("WARNING: {} log messages dropped", dropped);
}

// 程序退出前刷新
logger::flush();
logger::shutdown();
```

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 程序启动时调用 `logger::init` | 未初始化就输出日志 |
| 使用 `{}` 格式化占位符 | 用字符串拼接构造日志消息 |
| 退出前调用 `logger::shutdown()` | 直接 `exit()` 导致日志丢失 |
| 用 `set_async_queue_limit` 防溢出 | 不设限制导致内存无限增长 |
| 用 `logger::error` 等结构化输出 | 用 `std::cout` 打印错误信息 |
| 利用 source_location 自动捕获位置 | 手动拼写文件名和行号 |

## 参考源码
- `src/core/log.cppm` — 公共日志 API、级别输出结构体
- `src/core/log_config.cppm` — level 枚举、output_format 枚举、rotation_options、sink 类型
<!-- END SOURCE: skill/core/logging.md -->

<!-- BEGIN SOURCE: skill/core/network-io.md -->
# Source: `skill/core/network-io.md`

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
<!-- END SOURCE: skill/core/network-io.md -->

<!-- BEGIN SOURCE: skill/core/process.md -->
# Source: `skill/core/process.md`

# Process

> 提供跨平台、RAII 管理的子进程与标准输入输出管道，供 MCP、编译器工具和其他本地协议适配器复用。

**import**: `import cnetmod.core.process;`
**源码**: `src/core/process.cppm`, `src/core/process.cpp`

## 核心原则

- `child_process` 仅负责进程和阻塞式管道原语；协程调用方必须通过 `blocking_invoke` 与 `thread_pool` 接入。
- 平台实现封装在 `.cpp` 中，协议模块不得直接包含 Windows 或 POSIX 进程 API。
- 对象不可复制、可以移动；析构时关闭管道并终止仍在运行的子进程。
- 命令和参数使用 `std::filesystem::path`，Windows 直接使用原生宽字符路径。

## API 参考

```cpp
export struct process_options {
    std::filesystem::path executable;
    std::vector<std::filesystem::path> arguments;
    std::optional<std::filesystem::path> working_directory;
};

export class child_process {
public:
    static auto launch(process_options)
        -> std::expected<child_process, std::error_code>;
    auto write(std::string_view)
        -> std::expected<void, std::error_code>;
    auto read_line()
        -> std::expected<std::string, std::error_code>;
    void close_input() noexcept;
    void terminate() noexcept;
    auto running() const noexcept -> bool;
};
```

## 使用方式

在协程中不要直接调用阻塞式 `write` 或 `read_line`。使用 executor bridge：

```cpp
auto result = co_await cnetmod::blocking_invoke(pool, context,
    [&process]() -> std::expected<std::string, std::error_code> {
        auto written = process.write("request\n");
        if (!written)
            return std::unexpected(written.error());
        return process.read_line();
    });
```

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 在协议层复用 `child_process` | 在协议文件中直接调用 `CreateProcess` 或 `fork` |
| 通过 `blocking_invoke` 调用管道操作 | 在 I/O 协程中直接执行阻塞读取 |
| 使用 RAII 或显式 `terminate` 回收子进程 | 遗留后台进程和未关闭管道 |
<!-- END SOURCE: skill/core/process.md -->

<!-- BEGIN SOURCE: skill/core/ssl-tls.md -->
# Source: `skill/core/ssl-tls.md`

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
| `set_default_ca` | `auto set_default_ca() -> expected<void, error_code>` | 使用系统默认 CA；Windows 将 ROOT 证书存储导入 BoringSSL |

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
| `async_shutdown` | `auto async_shutdown(cancel_token& token) -> task<expected<void, error_code>>` | 可取消关闭，token 与流必须存活到任务结束 |
| `get_alpn_selected` | `auto get_alpn_selected() const noexcept -> string_view` | 获取 ALPN 协商结果 |
| `kernel_tls_active` | `auto kernel_tls_active() const noexcept -> bool` | kTLS 是否激活 |
| `native` | `auto native() const noexcept -> SSL*` | 获取原生 SSL 指针 |

可取消关闭将 token 传到 BIO 读写和 Linux socket readiness 等待；调用前已取消则直接
返回 operation_canceled，不调用 SSL_shutdown。它不负责关闭 socket，调用方仍拥有传输资源。
普通入口与可取消入口通过编译期分流共享实现，普通入口不检查运行时 token。
Windows 本地回归覆盖预取消，以及已握手连接等待对端 close_notify 时的 30ms 超时取消：
对端只消费密文、不发送关闭确认；客户端返回 timed_out 后关闭 socket，对端观察到连接结束。
该用例由独立 test_ssl_shutdown 目标执行，仅依赖 SSL 与 OpenSSL 证书生成工具，不依赖
Redis 开关；它与 test_redis_tls 共用证书设施。Arch Clang 22 的 SSL-only 配置（HTTP、Redis、
ORM 均关闭）也已运行通过。当前仓库强制使用内置 BoringSSL，其头文件未提供 SSL_OP_ENABLE_KTLS，
因此该配置实际使用 memory BIO；不能将结果当作 direct socket BIO/kTLS 的运行证据。

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
| 公网 TLS 客户端调用 `set_default_ca` 并保持 peer verification | 为绕过证书错误关闭 peer verification |
| 先 `set_connect_state` / `set_accept_state` 再握手 | 跳过状态设置直接握手 |
| 用 `async_shutdown` 优雅关闭 TLS | 直接 `close` socket 跳过 TLS close_notify |
| 服务端用 `configure_alpn_server` 按优先级排列 | 客户端和服务端都用相同的 ALPN 调用 |

## 参考源码
- `src/core/ssl.cppm` — ssl_context、ssl_stream、kTLS、ALPN
- `src/core/dtls.cppm` — dtls_datagram_session、dtls_role
- `examples/core/ssl_echo_server.cpp` — TLS Echo 服务端示例
- `examples/core/dtls_echo_server.cpp` — DTLS Echo 服务端示例
<!-- END SOURCE: skill/core/ssl-tls.md -->

<!-- BEGIN SOURCE: skill/core/tcp-socket.md -->
# Source: `skill/core/tcp-socket.md`

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
<!-- END SOURCE: skill/core/tcp-socket.md -->

<!-- BEGIN SOURCE: skill/core/utils-error.md -->
# Source: `skill/core/utils-error.md`

# 错误处理与工具集

> 错误码体系、网络初始化、崩溃转储、串口，以及字节转换、字符解析、JSON 辅助、哈希等实用工具。

**import**: `import cnetmod.core.error;` / `import cnetmod.core.net_init;` / `import cnetmod.core.crash_dump;` / `import cnetmod.utils;`
**源码**: `src/core/error.cppm`, `src/core/net_init.cppm`, `src/core/crash_dump.cppm`, `src/core/serial_port.cppm`, `src/cnetmod_utils.cppm`, `src/utils/*.cppm`

## 场景导航

- 我要处理网络错误码 → [看这里](#errc-错误码)
- 我要在 Windows 上初始化网络库 → [看这里](#net_init-网络初始化)
- 我要捕获程序崩溃信息 → [看这里](#crash_dump-崩溃转储)
- 我要打开串口通信 → [看这里](#serial_port-串口)
- 我要做字节序/寄存器转换 → [看这里](#utilsconv-字节与寄存器转换)
- 我要解析字符串中的数字 → [看这里](#charconv-字符转换)
- 我要安全读取 JSON 字段 → [看这里](#json_utils-json-辅助)
- 我要计算 HMAC-SHA256 → [看这里](#hmac-sha256--sha256)
- 我要构建统一的应用结果类型 → [看这里](#utilsr-应用结果)

## API 参考

### `errc` 错误码

**签名**:
```cpp
export enum class errc {
    success = 0,
    // 连接
    connection_refused, connection_reset, connection_aborted,
    connection_timed_out, not_connected, already_connected,
    // 地址
    address_in_use, address_not_available, address_family_not_supported,
    // 操作
    operation_aborted, operation_in_progress, operation_not_supported,
    operation_would_block,
    // 资源
    too_many_files_open, no_buffer_space, out_of_memory,
    // 网络
    network_down, network_unreachable, host_unreachable, host_not_found,
    // I/O
    broken_pipe, end_of_file, bad_descriptor,
    // 通用
    permission_denied, invalid_argument, unknown_error,
};
```

**辅助函数**:
```cpp
export class network_error_category : public std::error_category { ... };
export auto network_category() noexcept -> const std::error_category&;
export auto make_error_code(errc e) noexcept -> std::error_code;
export auto from_native_error(int native_error) noexcept -> errc;
```

已注册 `std::is_error_code_enum<cnetmod::errc>`，可直接与 `std::error_code` 互操作。

```cpp
import std;
import cnetmod.core.error;

std::error_code ec = cnetmod::errc::connection_refused;
if (ec == cnetmod::errc::connection_refused) {
    std::println("connection refused: {}", ec.message());
}
```

---

### `net_init` 网络初始化

**签名**: `export class net_init;`（不可拷贝/移动）

RAII 守卫：构造时调用 `WSAStartup`（Windows），析构时调用 `WSACleanup`。Linux/macOS 为 no-op。

```cpp
import std;
import cnetmod.core.net_init;

int main() {
    cnetmod::net_init net;  // 必须在任何网络操作前创建
    // ... 网络操作 ...
}
```

---

### `crash_dump` 崩溃转储

**签名**:
```cpp
export struct crash_info {
    std::string signal_name;
    int signal_code{0};
    std::string timestamp;
    std::string stack_trace;
    std::string dump_file_path;
};

export class crash_dump {
    using callback_fn = std::function<void(const crash_info&)>;
    static void install(std::string dump_dir = "crash");
    static void set_callback(callback_fn fn);
    static void set_app_name(std::string name);
    static void trigger_crash_report(std::string_view reason);
};
```

```cpp
import std;
import cnetmod.core.crash_dump;

int main() {
    cnetmod::crash_dump::install("crash_reports");
    cnetmod::crash_dump::set_app_name("my_server");
    cnetmod::crash_dump::set_callback([](const cnetmod::crash_info& info) {
        std::println("Crash: {} at {}", info.signal_name, info.timestamp);
    });
    // ... 应用逻辑 ...
}
```

---

### `serial_port` 串口

**签名**:
```cpp
export enum class parity : std::uint8_t { none, odd, even, mark, space };
export enum class stop_bits : std::uint8_t { one, one_half, two };
export enum class flow_control : std::uint8_t { none, hardware, software };

export struct serial_config {
    std::uint32_t baud_rate = 9600;
    std::uint8_t data_bits = 8;
    stop_bits stop = stop_bits::one;
    parity par = parity::none;
    flow_control flow = flow_control::none;
    std::uint32_t read_timeout_ms = 1000;
    std::uint32_t write_timeout_ms = 1000;
};

export class serial_port {
    [[nodiscard]] static auto open(std::string_view name,
        const serial_config& config = {}) -> std::expected<serial_port, std::error_code>;
    void close() noexcept;
    [[nodiscard]] auto native_handle() const noexcept -> file_handle_t;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto config() const noexcept -> const serial_config&;
    [[nodiscard]] auto release() noexcept -> file_handle_t;  // 释放所有权
};
```

```cpp
import std;
import cnetmod.core.serial_port;

auto port = cnetmod::serial_port::open("COM3", { .baud_rate = 115200 });
if (port) {
    // 使用 port->native_handle() 进行 I/O
    port->close();
}
```

---

### `utils::conv` 字节与寄存器转换

**import**: `import cnetmod.utils;`（通过 `:converter` 子模块）
**命名空间**: `utils::conv`

| 函数/类 | 说明 |
|---------|------|
| `hton(v)` / `ntoh(v)` | 主机 ↔ 网络（大端）字节序，支持 16/32/64 位 |
| `htole(v)` / `letoh(v)` | 主机 ↔ 小端字节序 |
| `read_be16/32/64(span, offset)` | 从缓冲区读大端值 |
| `write_be16/32/64(span, value, offset)` | 写大端值到缓冲区 |
| `RegisterConverter` | Modbus 寄存器 ↔ int/float/double 转换 |
| `BitOps` | 位操作：get_bit, set_bit, to_bits, from_bits |
| `CRC16` | Modbus RTU CRC16 计算/校验 |
| `Hex` | 十六进制编解码 |

```cpp
import std;
import cnetmod.utils;

// 字节序转换
auto net_val = utils::conv::hton(uint32_t{0x12345678});

// Modbus 寄存器转 float
float f = utils::conv::RegisterConverter::to_float_hilo(reg_high, reg_low);

// Hex 编码
auto hex = utils::conv::Hex::encode(data_span);
```

---

### `charconv` 字符转换

**签名**:
```cpp
export auto from_chars_double(std::string_view sv, double& value) -> std::errc;
export auto from_chars_float(std::string_view sv, float& value) -> std::errc;
export template <std::integral T>
auto from_chars_int(std::string_view sv, T& value, int base = 10) -> std::errc;
export auto to_chars_double(char* first, char* last, double value,
    int precision = std::numeric_limits<double>::max_digits10)
    -> std::to_chars_result;
export auto to_chars_float(char* first, char* last, float value,
    int precision = std::numeric_limits<float>::max_digits10)
    -> std::to_chars_result;
```

跨平台浮点字符转换封装。macOS 的标准库缺少浮点 `std::to_chars` 时，格式化在
封装内部回退到 `std::format_to_n`；调用方仍使用同一组无分配缓冲区接口。解析在
macOS 回退到 `std::stod`/`std::stof`。协议、OTLP 和业务代码不得自行用平台宏复制
这套兼容逻辑。

---

### `json_utils` JSON 辅助

**命名空间**: `cnetmod::json_utils`

```cpp
template <typename JsonT> auto parse_object(std::string_view body) -> JsonT;
template <typename JsonT> auto to_int(const JsonT& j, const char* key, int def = 0) -> int;
template <typename JsonT> auto to_bool(const JsonT& j, const char* key, bool def = false) -> bool;
template <typename JsonT> auto to_string(const JsonT& j, const char* key, std::string def = {}) -> std::string;
template <typename JsonT> auto to_uint16_port(const JsonT& j, const char* key, std::uint16_t def) -> std::uint16_t;
```

安全读取 JSON 字段，缺失或类型不匹配时返回默认值。

---

### HMAC-SHA256 / SHA256

**import**: `import cnetmod.utils.hmac_sha256;` / `import cnetmod.utils.sha256;`

```cpp
// HMAC-SHA256
export auto hmac_sha256(std::string_view key, std::string_view data) -> hmac_sha256_digest;
export auto hmac_sha256_hex(std::string_view key, std::string_view data) -> std::string;
export auto hmac_sha256_base64(std::string_view key, std::string_view data) -> std::string;

// SHA256
export auto sha256(std::string_view input) -> sha256_digest;
export auto sha256_hex(std::string_view input) -> std::string;
```

---

### `utils::R` 应用结果

**签名**: `template <class T, class ErrorCode = std::int32_t> class R;`

统一的成功/失败结果类型，不耦合 HTTP/数据库特定错误码。

```cpp
using namespace cnetmod::utils;
auto ok_result = R<User>::ok(user, "found");
auto err_result = R<User>::error(404, "not found", "user_id=123");

if (result.ok()) { result.data(); }
else { result.failure().code; result.message(); }
```

`R` 仅表达业务层成功/失败；底层 I/O、数据库和协议仍优先返回
`std::expected<T, std::error_code>`。在应用边界通过 `from_error_code()`
将传输错误映射为业务错误码，避免协议细节泄漏到服务接口。

```cpp
using api_result = R<User, app_error>;

auto result = api_result::from_error_code(network_ec,
    [](const std::error_code& ec) { return map_network_error(ec); });
```

`R<void, E>` 用于成功但没有返回实体的操作。由于 C++ 不允许静态
`ok()` 工厂与实例谓词 `result.ok()` 同时使用无参重载，其成功工厂为
`R<void, E>::success()`：

```cpp
auto updated = R<void, app_error>::success()
    .and_then([&] { return save_profile(profile); });
```

支持的组合操作：

| 操作 | 用途 |
|------|------|
| `map(f)` | 仅转换成功值，失败原样透传 |
| `and_then(f)` | 链接返回相同错误码类型 `R<U, E>` 的下一步 |
| `map_error(f)` | 仅转换错误码，保留 message / diagnostic |
| `data() &&` | 从成功结果零额外拷贝地移动取出值 |

HTTP 输出使用 `import cnetmod.protocol.http;` 后的
`http::to_http_response()`。由调用方提供成功/失败序列化器，框架不耦合
具体 JSON 库或错误响应 schema：

```cpp
auto response = cnetmod::http::to_http_response(
    result,
    [](const User& user) { return encode_user_json(user); },
    [](const auto& error) { return encode_error_json(error); },
    {.error_status = cnetmod::http::status::bad_request});
```

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 在 `main()` 开头创建 `net_init` RAII 对象 | 忘记 `net_init`，在 Windows 上直接操作 socket |
| 用 `make_error_code(errc::xxx)` 生成 `std::error_code` | 手动构造 `std::error_code` 并猜测 category |
| 用 `from_native_error()` 转换平台错误码 | 硬编码 `#ifdef` 判断平台错误码值 |
| 用 `json_utils::to_int()` 安全读取可能缺失的字段 | 直接 `j["key"].get<int>()`（键缺失时抛异常） |
| 用 `serial_port::open()` 返回 `std::expected` 处理错误 | 忽略 `std::expected` 的错误路径 |
| 程序启动时调用 `crash_dump::install()` | 在崩溃已经发生后才尝试安装处理器 |

## 参考示例

- `src/core/error.cppm` — 错误码定义与转换
- `src/utils/converter.cppm` — 字节序、寄存器、CRC、Hex 工具
- `src/utils/json.cppm` — JSON 安全读取辅助
- `src/protocol/http/extension/application_result.cppm` — `utils::R` 到 HTTP 响应的无框架耦合适配
<!-- END SOURCE: skill/core/utils-error.md -->

<!-- BEGIN SOURCE: skill/coro/coroutine.md -->
# Source: `skill/coro/coroutine.md`

# Coroutine 并发原语

> C++20 协程原语集合：task、channel、mutex、semaphore、wait_group、cancel_token，全部非阻塞、零堆分配设计。

**import**: `import cnetmod.coro.task;` / `import cnetmod.coro.channel;` / `import cnetmod.coro.mutex;` 等子模块
**源码**: `src/coro/task.cppm`, `spawn.cppm`, `channel.cppm`, `mutex.cppm`, `shared_mutex.cppm`, `semaphore.cppm`, `wait_group.cppm`, `cancel.cppm`

## 场景导航

- 写异步函数并同步等待 → [`task` + `sync_wait`](#taskt--sync_wait)
- 并发执行多个任务 → [`when_all`](#when_all)
- 启动即发即弃后台任务 → [`spawn`](#spawn)
- 生产者/消费者传递数据 → [`channel`](#channel)
- 保护协程共享数据 → [`async_mutex`](#async_mutex)
- 多读单写 → [`async_shared_mutex`](#async_shared_mutex)
- 限制并发数量 → [`async_semaphore`](#async_semaphore)
- 等待一组协程完成 → [`async_wait_group`](#async_wait_group)
- 取消异步操作 → [`cancel_token`](#cancel_token)

## API 参考

### `task<T>` / `sync_wait`

**签名**:
```cpp
export template <typename T> class task;       // 协程返回类型（不可拷贝，可移动）
export template <typename T> auto sync_wait(task<T> t) -> T;  // 阻塞等待，不驱动 io_context
export void sync_wait(task<void> t);
```

```cpp
import std;
import cnetmod.coro.task;
using namespace cnetmod;

auto compute(int x) -> task<int> { co_return x * x; }

int main() {
    auto r = sync_wait(compute(42));  // r = 1764
    std::println("{}", r);
}
```

`sync_wait()` 只在当前线程恢复协程并等待结果，**不会运行 `io_context`**。
因此它只适用于纯协程计算，或所依赖的事件循环已经在其他线程运行的任务。
如果任务会等待由尚未运行的 `io_context` 完成的网络、定时器或文件 I/O，直接
`sync_wait()` 会永久等待。应用入口应使用 `spawn(ctx, task)` 后调用 `ctx.run()`；
不要用 `sync_wait()` 代替事件循环。

`sync_wait()` 也不是第三方协程库或阻塞 API 的桥接器。接入阻塞函数时使用
`thread_pool`、`spawn_on` 或 `blocking_invoke`；接入其他协程库提供的 awaitable
时使用 `from_awaitable`。执行域切换、返回目标 `io_context` 及生命周期要求见
[Executor 与 Bridge](executor-bridge.md)。

---

### `task_group` — 有边界的并发 fan-out

`task_group` 为一组子任务建立生命周期边界：每个子任务拿到独立的 `cancel_token`；第一个失败的子任务会取消同组其余任务；`join()` 始终等待全部已启动任务收束。需要由请求整体等待的工作不要改用 detached `spawn()`。

```cpp
cnetmod::task_group group{ctx, cnetmod::deadline::after(std::chrono::seconds{1})};
group.run([&](cnetmod::cancel_token& token)
    -> task<std::expected<void, std::error_code>> {
    co_return co_await refresh_cache(token);
});
group.run([&](cnetmod::cancel_token& token)
    -> task<std::expected<void, std::error_code>> {
    co_return co_await load_profile(token);
});
if (auto done = co_await group.join(); !done)
    co_return std::unexpected(done.error());
```

`cancel()` 表示调用方取消；构造时给出的 `deadline` 到期时取消整组，`join()` 返回 `std::errc::timed_out`。`run()` 在开始 `join()` 后返回 `false`，因此应先提交全部子任务再等待。

### `when_all`

**签名**:
```cpp
export template <typename T1, typename T2>
auto when_all(task<T1>, task<T2>) -> task<std::tuple<T1, T2>>;

export template <typename T1, typename T2, typename T3>
auto when_all(task<T1>, task<T2>, task<T3>) -> task<std::tuple<T1, T2, T3>>;

export template <typename... Ts> requires(sizeof...(Ts) >= 4)
auto when_all(task<Ts>...) -> task<std::tuple<Ts...>>;

// void + 非void 组合：返回非void结果
export template <typename T2> requires(!std::is_void_v<T2>)
auto when_all(task<void>, task<T2>) -> task<T2>;

export auto when_all(task<void>, task<void>) -> task<void>;
```

所有子任务**真正并发**启动（非顺序 await），全部完成后恢复调用者。
即使子任务在启动阶段同步完成、或从不同工作线程恢复，`when_all` 也会先完成
启动/挂起交接再恢复父协程；不会在 `await_suspend()` 尚未返回时销毁组合器状态。

```cpp
auto [a, b] = co_await when_all(fetch_a(), fetch_b());
```

---

### `spawn`

**签名**: `export void spawn(io_context& ctx, task<void> task_to_run);`

即发即弃：投递到 `io_context` 后立即返回。未捕获异常调用 `std::terminate()`。

需要隔离异常时使用 `spawn_guarded(ctx, task, on_error)`，回调接收
`std::exception_ptr`，回调自身异常也被捕获。固定基础设施回调可使用
`spawn_guarded<on_error>(ctx, task)` 编译期绑定，不在协程帧中保存运行时回调。
包装协程开始前的分配失败仍可能抛给调用者。两种 guarded 接口均不是 join 机制，
不能代替关键任务的生命周期监管和取消。普通 `spawn()` 的终止语义保持不变。

---

### `channel<T>`

有界异步通道，环形缓冲 + 自适应自旋锁 + 直接交接优化。

**签名**: `export template <typename T> class channel;`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit channel(std::size_t capacity = 1)` | 指定缓冲区容量 |
| send | `auto send(T value) -> send_awaitable` | `co_await ch.send(v)` → `bool`（false=已关闭） |
| receive | `auto receive() -> recv_awaitable` | `co_await ch.receive()` → `std::optional<T>` |
| try_send | `auto try_send(T value) -> bool` | 非阻塞发送 |
| try_receive | `auto try_receive() noexcept -> std::optional<T>` | 非阻塞接收 |
| try_receive_many | `auto try_receive_many(std::vector<T>&, std::size_t) -> std::size_t` | 批量接收 |
| close | `void close() noexcept` | 关闭通道，唤醒所有等待者 |
| is_closed | `auto is_closed() const noexcept -> bool` | 查询状态 |

```cpp
import std;
import cnetmod.coro.task;
import cnetmod.coro.channel;
using namespace cnetmod;

auto producer(channel<int>& ch, int n) -> task<void> {
    for (int i = 0; i < n; ++i) co_await ch.send(i);
    ch.close();
}
auto consumer(channel<int>& ch) -> task<void> {
    while (auto val = co_await ch.receive())
        std::println("recv {}", *val);
}
auto run() -> task<void> {
    channel<int> ch(2);
    co_await when_all(producer(ch, 5), consumer(ch));
}
int main() { sync_wait(run()); }
```

---

### `async_mutex`

非阻塞协程互斥锁，竞争时挂起协程而非阻塞线程。

**签名**: `export class async_mutex;`

| 方法 | 签名 | 说明 |
|------|------|------|
| lock | `auto lock() noexcept -> lock_awaitable` | `co_await mtx.lock()` |
| unlock | `void unlock() noexcept` | 释放锁 |
| try_lock | `auto try_lock() noexcept -> bool` | 非阻塞尝试 |

**RAII 守卫** — `export class async_lock_guard;`：
```cpp
co_await mtx.lock();
async_lock_guard guard(mtx, std::adopt_lock);
// ... 临界区 ...（析构自动解锁）
```

---

### `async_shared_mutex`

非阻塞协程读写锁，**写者优先**防止写者饥饿。

**签名**: `export class async_shared_mutex;`

| 方法 | 签名 | 说明 |
|------|------|------|
| lock_shared | `auto lock_shared() noexcept -> lock_shared_awaitable` | 获取共享读锁 |
| unlock_shared | `void unlock_shared() noexcept` | 释放读锁 |
| lock | `auto lock() noexcept -> lock_awaitable` | 获取独占写锁 |
| unlock | `void unlock() noexcept` | 释放写锁 |

**RAII 守卫**: `async_shared_lock_guard`（读）、`async_unique_lock_guard`（写）。

```cpp
async_shared_mutex rw;
co_await rw.lock_shared();
async_shared_lock_guard rg(rw, std::adopt_lock);
// ... 读取 ...

co_await rw.lock();
async_unique_lock_guard wg(rw, std::adopt_lock);
// ... 写入 ...
```

---

### `async_semaphore`

非阻塞协程计数信号量，限制并发数量。

**签名**: `export class async_semaphore;`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit async_semaphore(std::size_t initial_count) noexcept` | 初始许可数 |
| acquire | `auto acquire() noexcept -> acquire_awaitable` | `co_await sem.acquire()` |
| release | `void release() noexcept` / `void release(std::size_t n) noexcept` | 释放许可 |
| try_acquire | `auto try_acquire() noexcept -> bool` | 非阻塞尝试 |
| available | `auto available() const noexcept -> std::size_t` | 可用许可数 |

---

### `async_wait_group`

类似 Go `sync.WaitGroup`，等待一组协程完成。

**签名**: `export class async_wait_group;`

| 方法 | 签名 | 说明 |
|------|------|------|
| add | `void add(int n = 1) noexcept` | 增加计数 |
| done | `void done() noexcept` | 减少计数，到零唤醒等待者 |
| wait | `auto wait() noexcept -> wait_awaitable` | `co_await wg.wait()` |
| count | `auto count() const noexcept -> int` | 当前计数（仅供监控） |

---

### `cancel_token`

异步操作取消令牌。线程安全，不可拷贝/移动（地址稳定性）。

**签名**: `export class cancel_token;`

| 方法 | 签名 | 说明 |
|------|------|------|
| cancel | `void cancel() noexcept` | 请求取消（多次调用仅首次生效） |
| is_cancelled | `auto is_cancelled() const noexcept -> bool` | 是否已取消 |
| reset | `void reset() noexcept` | 重置以供复用（前提：无进行中操作） |

## Do's & Don'ts

| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 仅对纯计算或已有独立事件循环的任务使用 `sync_wait()` | 用 `sync_wait()` 等待尚未运行的 `io_context` I/O |
| 应用入口使用 `spawn(ctx, task)` + `ctx.run()` | 在协程内部调用 `sync_wait()` |
| 用 `async_lock_guard` RAII 管理锁 | 手动 `unlock()` 忘记异常路径释放 |
| `close()` 后仍可 `receive()` 读取缓冲数据 | 假设 `close()` 后 `receive()` 立即返回 `nullopt` |
| `when_all()` 并发执行独立任务 | 用 `when_all()` 执行有依赖的任务 |
| `cancel_token` 通过 `reset()` 复用 | 拷贝或移动 `cancel_token`（已 delete） |
| `spawn()` 启动不关心结果的后台任务 | 用 `spawn()` 启动需要返回值的任务 |

## 参考示例

- `examples/concurrency/channel_demo.cpp` — channel 生产者/消费者模式
- `examples/concurrency/mutex_demo.cpp` — async_mutex 保护共享状态
<!-- END SOURCE: skill/coro/coroutine.md -->

<!-- BEGIN SOURCE: skill/coro/executor-bridge.md -->
# Source: `skill/coro/executor-bridge.md`

# Executor 与 Bridge

> cnetmod 原生协程调度、线程池、多核服务器上下文，以及阻塞 API 和第三方
> awaitable 的桥接方式。

**import**: `import cnetmod.executor.scheduler;` / `import cnetmod.executor.pool;` /
`import cnetmod.coro.bridge;`

## 核心规则

- 公开模块接口只暴露 cnetmod 自有类型，不导出 stdexec/exec 类型。
- stdexec 仅允许作为普通 `.cpp` 中的内部实现细节，不得出现在 `.cppm`。
- I/O 协程使用 `io_scheduler::schedule()` 或 `post_awaitable` 切换执行域。
- CPU 密集或阻塞调用使用 `thread_pool`、`pool_post_awaitable`、
  `blocking_invoke`，禁止阻塞 `io_context`。
- 同步入口等待 `task<T>` 使用原生 `sync_wait`。

## `io_scheduler`

`io_scheduler` 是绑定 `io_context` 的原生协程调度门面。

```cpp
export class io_scheduler
{
public:
    explicit io_scheduler(io_context& context) noexcept;
    auto operator==(const io_scheduler& other) const noexcept -> bool;
    [[nodiscard]] auto context() const noexcept -> io_context&;
    [[nodiscard]] auto schedule() const noexcept -> schedule_awaitable;
};
```

```cpp
auto run_on(io_context& io) -> task<void>
{
    io_scheduler scheduler{io};
    co_await scheduler.schedule();
    co_return;
}
```

## `thread_pool` 与 `pool_post_awaitable`

`thread_pool` 是 cnetmod 自有 PImpl 门面，具体执行器类型只存在于 `.cpp`。

```cpp
export class thread_pool
{
public:
    explicit thread_pool(
        unsigned thread_count = std::thread::hardware_concurrency());
    ~thread_pool();
    void request_stop() noexcept;
};

export struct pool_post_awaitable
{
    thread_pool& pool;
    explicit pool_post_awaitable(thread_pool& value) noexcept;
    auto await_ready() const noexcept -> bool;
    void await_suspend(std::coroutine_handle<> coroutine) noexcept;
    void await_resume() noexcept;
};
```

`co_await pool_post_awaitable{pool}` 后，协程在线程池线程恢复。需要继续 I/O
时必须再切回目标 `io_context`。

## `server_context`

`server_context` 管理 accept `io_context`、多个 worker `io_context` 和 CPU
线程池。

```cpp
export class server_context
{
public:
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency(),
        thread_affinity_options affinity = {});

    [[nodiscard]] auto accept_io() noexcept -> io_context&;
    [[nodiscard]] auto next_worker_io() noexcept -> io_context&;
    [[nodiscard]] auto worker_count() const noexcept -> unsigned;
    [[nodiscard]] auto worker_ios() -> std::vector<io_context*>;
    [[nodiscard]] auto pool() noexcept -> thread_pool&;

    template <typename F>
    requires std::invocable<std::decay_t<F>>
    auto offload(io_context& return_to, F&& fn);

    void spawn_next(task<void> value);
    void run();
    void stop();
};
```

## `blocking_invoke`

阻塞函数在线程池执行，完成后自动回到指定 `io_context`。

```cpp
auto value = co_await blocking_invoke(server.pool(), io, []
    {
        return blocking_client.request();
    });
```

互不依赖的调用可通过 `when_all` 并发卸载。不要在阻塞函数体中访问由事件
循环线程独占的对象。

## `from_awaitable`

将第三方 awaitable 包装为 cnetmod `task<T>`：

```cpp
auto value = co_await from_awaitable<int>(third_party_async_call());
co_await from_awaitable<void>(third_party_async_flush());
```

## 正确用法

| 正确 | 错误 |
|---|---|
| `co_await scheduler.schedule()` 切换到目标事件循环 | 在公开模块中暴露 sender/receiver 类型 |
| `blocking_invoke(pool, io, fn)` 桥接阻塞 API | 在 `io_context` 线程直接调用阻塞函数 |
| `pool_post_awaitable` 卸载 CPU 计算 | 在事件循环线程执行长时间计算 |
| `from_awaitable<T>` 统一第三方 awaitable | 假设所有第三方 task 与 cnetmod 生命周期一致 |
| 同步入口使用 `sync_wait(task)` | 在协程内部同步等待 task |

## 参考示例

- `examples/concurrency/stdexec_bridge.cpp`：原生 `task<T>` 与 `sync_wait`
- `examples/concurrency/blocking_bridge_demo.cpp`：阻塞卸载、并发组合、第三方 awaitable
- `examples/http/multicore_http.cpp`：`server_context` 多核 HTTP 服务
<!-- END SOURCE: skill/coro/executor-bridge.md -->

<!-- BEGIN SOURCE: skill/coro/timer-retry.md -->
# Source: `skill/coro/timer-retry.md`

# 定时器、重试、断路器与速率限制

> 提供异步定时器、自动重试、断路器和 Token Bucket 速率限制，增强网络应用的可靠性。

**import**: `import cnetmod.coro;`
**源码**: `src/coro/timer.cppm`, `src/coro/retry.cppm`, `src/coro/circuit_breaker.cppm`, `src/coro/rate_limiter.cppm`

## 场景导航

| 场景 | 推荐 API |
|------|----------|
| 等待一段时间后执行 | `async_sleep()` / `steady_timer` |
| 等待到指定时间点 | `async_sleep_until()` / `high_resolution_timer` |
| 给异步操作加超时限制 | `with_timeout()` |
| 失败后自动重试（指数退避） | `retry()` |
| 失败后重试（抛异常风格） | `retry_throwing()` |
| 防止级联故障（熔断） | `circuit_breaker` |
| 限制请求速率与突发流量 | `token_bucket` |

---

## API 参考

### 定时器类

#### `steady_timer` — 标准精度定时器

基于 `io_context` 的异步定时器，使用平台原生定时器。

```cpp
export class steady_timer {
public:
    explicit steady_timer(io_context& ctx) noexcept;
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto context() noexcept -> io_context&;
};
```

#### `high_resolution_timer` — 高精度定时器

支持等待到指定时间点。

```cpp
export class high_resolution_timer {
public:
    explicit high_resolution_timer(io_context& ctx) noexcept;
    auto async_wait_until(std::chrono::steady_clock::time_point deadline)
        -> task<std::expected<void, std::error_code>>;
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto context() noexcept -> io_context&;
};
```

---

### 便捷函数

#### `async_sleep` — 异步休眠

抛异常的便捷封装。如需显式错误处理，请使用定时器类。

```cpp
export auto async_sleep(io_context& ctx,
    std::chrono::steady_clock::duration duration) -> task<void>;

export auto async_sleep_until(io_context& ctx,
    std::chrono::steady_clock::time_point tp) -> task<void>;
```

---

### `deadline` / `with_deadline` — 统一请求时间预算

`deadline` 是可复制的单调时钟截止时间，默认无限期。在入口创建一次后传给下游；子调用使用 `constrain()` 取更早的截止时间，不要在每层重新启动独立的相对计时器。

```cpp
import cnetmod.coro;

auto request_deadline = cnetmod::deadline::after(std::chrono::seconds{2});
auto db_deadline = request_deadline.constrain(
    cnetmod::deadline::after(std::chrono::milliseconds{300}));

cnetmod::cancel_token token;
auto result = co_await cnetmod::with_deadline(ctx, db_deadline,
    operation(token), token);
```

`with_deadline()` 与 `with_timeout()` 只包装可取消的 `task<std::expected<T, std::error_code>>`。超时会触发传入的 `cancel_token`，并返回 `std::errc::timed_out`；调用方显式取消仍为 `std::errc::operation_canceled`。底层 I/O 必须遵守 token，才能真正中止读写。

### `with_timeout` — 超时包装

为异步操作添加超时控制：

```cpp
export template <typename T>
auto with_timeout(io_context& ctx, std::chrono::steady_clock::duration timeout,
    task<std::expected<T, std::error_code>> op, cancel_token& op_token)
    -> task<std::expected<T, std::error_code>>;
```

超时后通过 `cancel_token` 取消被包装的操作，返回 `std::errc::timed_out`。内部并行启动定时器和操作任务，任一完成即取消另一方；调用方显式取消则仍返回 `std::errc::operation_canceled`。

---

### 重试机制

#### `retry_options` — 重试配置

```cpp
export struct retry_options {
    std::uint32_t max_attempts = 3;           ///< 最大尝试次数（含首次）
    std::chrono::steady_clock::duration
        initial_delay = std::chrono::milliseconds(100); ///< 首次失败后延迟
    std::chrono::steady_clock::duration
        max_delay = std::chrono::seconds(5);  ///< 延迟上限
    double multiplier = 2.0;                  ///< 退避乘数
    bool jitter = true;                       ///< 添加随机 ±25% 抖动
};
```

延迟计算方式：每次失败后延迟乘以 `multiplier`，但不超过 `max_delay`。
启用 `jitter` 时，实际延迟在当前值的 75%–125% 之间随机浮动，避免惊群效应。

#### `retry` — 重试（expected 风格）

```cpp
// 返回 task<expected<T, E>> 的操作
export template <typename T, typename E, typename Fn>
requires std::invocable<Fn> &&
             std::same_as<std::invoke_result_t<Fn>, task<std::expected<T, E>>>
auto retry(io_context& ctx, retry_options opts, Fn fn)
    -> task<std::expected<T, E>>;

// task<expected<void, E>> 特化重载
export template <typename Fn>
requires std::invocable<Fn> &&
             detail::is_task_expected_void<std::invoke_result_t<Fn>>
auto retry(io_context& ctx, retry_options opts, Fn fn)
    -> std::invoke_result_t<Fn>;
```

对返回 `task<std::expected<T, E>>` 的异步操作进行重试。
遇到成功立即返回；达到 `max_attempts` 后返回最后一次错误。

#### `retry_throwing` — 重试（异常风格）

对通过异常报告失败的异步操作进行重试。成功时返回结果；所有尝试失败后重新抛出最后一次异常。

---

### 断路器

#### `circuit_breaker_options` — 断路器配置

```cpp
export struct circuit_breaker_options {
    std::uint32_t failure_threshold = 5;  ///< 触发熔断的失败次数
    std::uint32_t success_threshold = 2;  ///< half_open 状态恢复所需的成功次数
    std::chrono::steady_clock::duration
        timeout = std::chrono::seconds(30); ///< open 状态等待时间
};
```

#### `circuit_breaker_state` — 断路器状态枚举

```cpp
export enum class circuit_breaker_state : std::uint8_t {
    closed,    ///< 正常 — 请求通过，追踪失败
    open,      ///< 熔断 — 请求立即拒绝
    half_open, ///< 探测 — 允许有限请求以测试恢复
};
```

#### `circuit_breaker_errc` — 断路器错误码

```cpp
export enum class circuit_breaker_errc {
    success = 0,
    circuit_open, ///< 断路器处于 open 状态，请求被拒绝
};
```

#### `circuit_breaker` — 三态断路器

```cpp
export class circuit_breaker {
public:
    explicit circuit_breaker(circuit_breaker_options opts = {}) noexcept;
    template <typename T, typename E, typename Fn>
    auto execute(Fn fn) -> task<std::expected<T, E>>;
    template <typename T, typename Fn>
    auto execute_ec(Fn fn) -> task<std::expected<T, std::error_code>>;
    [[nodiscard]] auto state() const noexcept -> circuit_breaker_state;
    void reset() noexcept;
    void trip() noexcept;
};
```

**状态转换**：closed → open（失败累积）→ half_open（超时自动恢复）→ closed（成功验证）。
open 状态下立即返回错误；从 open 到 half_open 在下次 execute 时自动检查。

---

## Do's & Don'ts

### Do's

- **定时器用 `async_sleep` 快速开始**：简单延时最简洁
- **需要错误码时用定时器类**：`steady_timer` / `high_resolution_timer` 返回 `std::expected`
- **给网络 IO 加超时**：使用 `with_timeout()` 包装长时间运行的读写操作
- **重试配合断路器**：对远程服务调用同时使用 `retry()` + `circuit_breaker`
- **合理设置 `jitter`**：多实例场景下保持 `jitter = true`，防止重试风暴

### Don'ts

- **不要在热循环中使用 `async_sleep`**：高频调用请用计数器或令牌桶
- **不要把 `retry` 用于幂等性不满足的操作**：确保操作可安全重复
- **不要忽略 `with_timeout` 的 `cancel_token`**：只有底层操作遵守 token，超时才能真正中止 I/O
- **不要在 open 状态下强行调用**：返回 `circuit_open` 时应在上层做降级处理
- **不要共用 `circuit_breaker` 实例跨不同服务**：每个下游服务应有独立的断路器

---

## 参考示例

### 定时器用法

```cpp
import std;
import cnetmod.coro;
import cnetmod.io;

using namespace cnetmod;

// 简易延时
co_await async_sleep(ctx, 300ms);

// 精确控制定时器
steady_timer timer(ctx);
auto result = co_await timer.async_wait(500ms);
if (!result) {
    std::println("timer error: {}", result.error().message());
}

// 等待到特定时间点
high_resolution_timer hr(ctx);
auto deadline = std::chrono::steady_clock::now() + 150ms;
co_await hr.async_wait_until(deadline);
```

### 超时控制

```cpp
cancel_token token;
auto result = co_await with_timeout(ctx, 5s,
    async_read_some(ctx, socket, buffer, token), token);

if (!result) {
    std::println("timeout: {}", result.error().message());
}
```

### 重试机制

```cpp
// Expected 风格
auto result = co_await retry(ctx, {
    .max_attempts = 5,
    .initial_delay = 200ms,
    .max_delay = 10s,
    .multiplier = 3.0,
    .jitter = true,
}, [&]() {
    return http_get("https://api.example.com/data");
});

// 异常风格
try {
    auto data = co_await retry_throwing(ctx, {
        .max_attempts = 3,
        .initial_delay = 100ms
    }, [] {
        return parse_config();
    });
} catch (const std::exception& e) {
    std::println("retry failed: {}", e.what());
}
```

### 断路器模式

```cpp
circuit_breaker cb({
    .failure_threshold = 5,
    .success_threshold = 2,
    .timeout = 30s,
});

auto result = co_await cb.execute_ec([]() {
    return db_query("SELECT * FROM users");
});

if (result.error() == make_error_code(circuit_breaker_errc::circuit_open)) {
    // 执行降级逻辑
}
```

### Token Bucket 速率限制

`token_bucket` 是同步、非阻塞的准入控制器，内部使用项目的 atomic latch，可安全用于多个协程发起请求前的快速判断。

```cpp
cnetmod::token_bucket requests({
    .tokens_per_second = 20.0,
    .burst = 40.0,
});

if (!requests.try_consume()) {
    cnetmod::logger::warn{"request rate limit exceeded"};
    co_return;
}
```

> **完整示例**: `examples/core/timer_demo.cpp`
<!-- END SOURCE: skill/coro/timer-retry.md -->

<!-- BEGIN SOURCE: skill/database/database-orm.md -->
# Source: `skill/database/database-orm.md`

# ORM 模型定义 / CRUD / 迁移

> cnetmod 协议无关 SQL ORM，支持 MySQL / PostgreSQL。
> 模块: `import cnetmod.protocol.mysql;` + `#include <cnetmod/orm.hpp>`

## 核心原则

- 模型定义用 `CNETMOD_MODEL` + `CNETMOD_FIELD` 宏（编译期反射）
- CRUD 操作通过 `mysql_session` 或 `base_mapper<T>`
- 流式查询用 `query_wrapper<T>`（支持成员指针类型安全）
- DDL 迁移用 `mysql_synchronize_schema<T>()`
- XML mapper 提供 MyBatis 风格动态 SQL

## 1. 模型定义

```cpp
#include <cnetmod/orm.hpp>
import std;
import cnetmod.protocol.mysql;

struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
    std::time_t created_at = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_),
    CNETMOD_FIELD(created_at, "created_at", timestamp, NULLABLE))
```

### CNETMOD_FIELD 参数

`CNETMOD_FIELD(member, "col", type [, flags [, strategy]])`

列类型后缀: `bigint`, `int_`, `varchar`, `text`, `double_`, `char_`, `timestamp`, `tinyint`, `boolean_`

### 字段标志

| 宏 | 含义 | 宏 | 含义 |
|----|------|----|------|
| `PK` | 主键 | `VERSION` | 乐观锁版本号 |
| `AUTO_INC` | 自增 | `LOGIC_DELETE` | 软删除标记 |
| `NULLABLE` | 允许 NULL | `FILL_INSERT` | 插入时自动填充 |
| `UNIQUE_KEY` | 唯一约束 | `FILL_INSERT_UPDATE` | 插入+更新填充 |
| `TENANT_ID` | 多租户字段 | | |

标志可组合: `PK | AUTO_INC`。

## 2. ID 生成策略

### UUID 主键

```cpp
struct Tag { orm::uuid id; std::string name; };
CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id, "id", char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar))
```

- `orm::uuid` — 128 位 UUID，`to_string()` / `from_string()`
- `uuid_v4()` — 生成随机 UUID v4
- DDL 生成 `CHAR(36)`

### Snowflake 主键

```cpp
struct Event { std::int64_t id = 0; std::string title; };
CNETMOD_MODEL(Event, "events",
    CNETMOD_FIELD(id, "id", bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar))
```

- `snowflake_generator(uint16_t machine_id)` — 构造（0~1023）
- `next_id() -> int64_t` — 生成 ID（非线程安全）

创建 session 时传入: `orm::mysql_session db(cli, snowflake);`

## 3. mysql_session — 异步 ORM 会话

```cpp
using mysql_session = basic_db_session<mysql::client>;
mysql_session(mysql::client& cli);
mysql_session(mysql::client& cli, snowflake_generator& sf);
```

| 方法 | 签名 |
|------|------|
| `find_all<T>()` | `-> task<orm_result<T>>` |
| `find_by_id<T>(param_value)` | `-> task<orm_result<T>>` |
| `find(const select_builder<T>&)` | `-> task<orm_result<T>>` |
| `find(const query_wrapper<T>&)` | `-> task<orm_result<T>>` |
| `insert(T&)` | `-> task<orm_result<T>>` |
| `insert_many(span<T>)` | `-> task<orm_result<T>>` |
| `update(const T&)` | `-> task<orm_result<T>>` |
| `update(const update_wrapper<T>&)` | `-> task<orm_result<T>>` |
| `remove(const T&)` | `-> task<orm_result<T>>` |
| `remove_by_id<T>(param_value)` | `-> task<orm_result<T>>` |
| `remove(const delete_builder<T>&)` | `-> task<orm_result<T>>` |
| `remove(const query_wrapper<T>&)` | `-> task<orm_result<T>>` |
| `count(const query_wrapper<T>&)` | `-> task<expected<size_t, string>>` |
| `create_table<T>()` / `drop_table<T>()` | `-> task<orm_result<T>>` |
| `raw_query(sql)` | `-> task<result_set>` |
| `transaction(Func&&)` | `-> task<result_set>` |

### orm_result<T>

```cpp
template <class T> struct orm_result {
    std::vector<T> data;
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
    std::string error_msg;
    std::string sql_state;
    std::uint32_t error_code = 0;
    auto ok() const noexcept -> bool;
    auto is_err() const noexcept -> bool;
    auto empty() const noexcept -> bool;
    auto first() const -> std::optional<T>;
};
```

### 示例

```cpp
orm::mysql_session db(cli, snowflake);

Article a; a.title = "Hello"; a.status = 1;
auto r = co_await db.insert(a);           // a.id 自动回填
auto all = co_await db.find_all<Article>();
auto one = co_await db.find_by_id<Article>(orm::param_value::from_int(42));
a.view_count += 100;
co_await db.update(a);
co_await db.remove(a);
co_await db.remove_by_id<Article>(orm::param_value::from_int(1));
```

## 分库分表

`shard_catalog` 把稳定的 `shard_key` 同时映射到具名数据库实例和经过校验的物理表。
默认 `hash_shard_strategy` 使用确定性哈希，不依赖进程随机种子；也可实现
`shard_strategy` 注入范围、目录或租户路由。目录在 `freeze()` 后只读，启动前拒绝空拓扑、
重复实例和非法 SQL 标识符。

```cpp
auto catalog = std::make_shared<orm::shard_catalog>();
catalog->add_database("orders-0");
catalog->add_database("orders-1");
catalog->freeze("orders", 64,
    std::make_shared<orm::hash_shard_strategy>());

auto gateway = application::make_mysql_sharded_session_gateway(
    host.services(), catalog);

auto result = co_await gateway->write<OrderId>(orm::shard_key{tenant_id},
    [&](auto& session) -> task<std::expected<OrderId, std::string>> {
        Order order{/* ... */};
        auto inserted = co_await session.insert(order);
        if (inserted.is_err())
            co_return std::unexpected(inserted.error_msg);
        co_return order.id;
    });
```

物理表名形如 `orders_00`～`orders_63`，所有 CRUD 和 wrapper SQL 都使用路由结果，
值仍由参数绑定传输。`write()` 只向回调暴露已经固定到一个库和一张表的 session，并在
同一连接上开启、提交或回滚事务，因此不会静默产生跨分片事务。

跨分片能力必须显式调用：

- `scatter_read<T>()` 访问全部物理分片并保留每个分片的成功或失败结果。
- `scatter_gather<Item, Result>()` 在完整 scatter 结果上调用业务提供的合并器；排序、分页、
  聚合及是否接受部分结果均由合并器决定。
- `distributed_transaction<T>()` 在单数据库时使用普通事务，在多个 MySQL 实例时使用
  XA 两阶段提交，并把固定到物理表的 `distributed_session_context` 交给回调。
- scatter 回调抛出的异常会转换为对应分片的失败结果；分布式事务回调抛出的异常会转换
  为事务错误并回滚已经启动的分支，不会越过 ORM 边界泄漏异常。
- 任一提交返回失败时会报告结果不确定；生产系统应配合 MySQL `XA RECOVER`、事务日志和
  运维补偿处理进程崩溃或网络分区，不能把 XA 当作无故障的本地事务。

Application 中先配置多个具名 `mysql_service`。工厂创建 gateway 时验证 catalog 引用的
每个实例均已注册；连接池的启动、健康恢复和逆序停机仍由 Application 管理。

调用 `enable_auto_configuration()` 后，也可以通过 `orm.sharding.enabled` 自动创建具名网关：

```json
{
  "orm": {
    "sharding": {
      "enabled": true,
      "topologies": {
        "orders": {
          "logical_table": "orders",
          "table_count": 64,
          "databases": ["orders-0", "orders-1"],
          "scatter_gather": true,
          "distributed_transactions": true
        }
      }
    }
  }
}
```

通过 `host.services().require<application::mysql_sharded_session_gateway>("orders")`
取得对应网关。`enabled` 默认是 `false`；关闭时仍使用普通 `database_session`，不会创建
catalog、分片网关或改变原有 MySQL 服务。

## ORM JSON：纯 import、零实体样板

`CNETMOD_MODEL` 的字段元数据可直接用于 JSON，不需要 `#include <nlohmann/json.hpp>`，也不需要
`NLOHMANN_DEFINE_TYPE_*` 宏：

```cpp
import nlohmann.json;
import cnetmod.orm;

auto payload = orm::to_json(article).dump();
auto decoded = orm::from_json<Article>(nlohmann::json::parse(payload, nullptr, false));
```

`from_json<T>` 返回 `std::expected<T, std::string>`；缺失字段保留模型默认值，类型不匹配返回错误。
当前覆盖数值、布尔、字符串、枚举与可空字段；日期/时间和二进制字段需要显式边界格式后再加入。

## XML ResultMap

`mapper_registry` 会加载 `<resultMap>`，并支持 `namespace.id` 查询：

```xml
<resultMap id="UserMap" type="User" autoMapping="false">
  <id property="id" column="id" jdbcType="BIGINT"/>
  <result property="displayName" column="display_name" jdbcType="VARCHAR"/>
</resultMap>
<select id="findById" resultMap="UserMap">SELECT ...</select>
```

`<id>`、`<result>`、`<association>` 与 `<collection>` 的映射元数据已解析并注册。

`mysql_mapper_session::query_object_graph()` 提供 XML 对象图执行：连接查询会按根和 collection 的
`<id>` 去重聚合；`association` / `collection` 带 `select` 时，会以父行的 `column` 值作为同名参数执行
引用语句，并将结果填回动态 `mapped_object`。例如：

```xml
<resultMap id="UserGraph" type="User">
  <id property="id" column="user_id"/>
  <result property="name" column="user_name"/>
  <!-- JOIN 查询：同一 user_id 的多行会聚合为一个用户和多个 roles -->
  <collection property="roles" resultMap="RoleMap"/>
</resultMap>

<resultMap id="UserWithOrders" type="User">
  <id property="id" column="id"/>
  <!-- 嵌套查询：父行 id 会作为 #{id} 传给 findOrdersByUserId -->
  <collection property="orders" column="id" select="findOrdersByUserId" resultMap="OrderMap"/>
</resultMap>
<select id="findOrdersByUserId" resultMap="OrderMap">
  SELECT id, user_id, total FROM orders WHERE user_id = #{id}
</select>
```

嵌套 select 当前为显式的 eager N+1 执行。根对象的标量字段可通过
`query_object_graph_as<T>()` 直接投影到 `CNETMOD_MODEL` DTO；关联和集合由
`xml_object_graph_binder<T>` 显式绑定到真实的 C++ 成员，避免 XML 字符串猜测成员布局。
例如为 `User` 声明一次绑定：

```cpp
namespace cnetmod::orm {
template <> struct xml_object_graph_binder<User> {
    static void bind(User& user, const mapped_object& source) {
        user.team = mapped_association_as<Team>(source, "team");
        user.roles = mapped_collection_as<Role>(source, "roles");
    }
};
} // namespace cnetmod::orm
```

`lazy_relation<T>` 可用于 C++ 业务层显式协程按需加载，访问必须 `co_await get()`，不会在普通属性访问中阻塞。
因此这里不是 MyBatis / MyBatis-Plus 的完整 XML 运行时兼容。

## 4. base_mapper<T> — MyBatis-Plus 风格

```cpp
orm::mysql_base_mapper<User> mapper(cli);

co_await mapper.insert(user);
auto opt = co_await mapper.select_by_id(42);
auto list = co_await mapper.select_list();
auto cnt = co_await mapper.select_count();
bool exists = co_await mapper.exists_by_id(42);
co_await mapper.update_by_id(user);
co_await mapper.update_selective(user);   // 仅更新非 null 字段
co_await mapper.delete_by_id(42);
co_await mapper.delete_batch_ids(id_vec);
auto page = co_await mapper.select_page(1, 20, wrapper);
```

主要方法: `insert`, `insert_get_id`, `insert_batch`, `delete_by_id`, `delete_batch_ids`, `update_by_id`, `update_selective`, `select_by_id`, `select_batch_ids`, `select_list`, `select_one`, `select_count`, `exists_by_id`, `select_page`, `delete_by_wrapper`, `update_by_wrapper`。

## 5. query_wrapper<T> — 流式查询

```cpp
// 成员指针（类型安全）
auto qw = orm::query_wrapper<User>{}
    .eq(&User::status, 1)
    .contains(&User::name, "Alice")
    .order_by_desc(&User::created_at)
    .limit(10);

// 字符串列名
auto qw2 = orm::query_wrapper<User>{}
    .eq("status", 1)
    .like("name", "%Alice%")
    .between("age", 18, 65)
    .in("role", std::vector<std::string>{"admin", "editor"})
    .order_by_desc("created_at")
    .limit(20).offset(40);
```

### 条件方法

| 方法 | SQL | 方法 | SQL |
|------|-----|------|-----|
| `eq` / `ne` | `=` / `!=` | `like` / `not_like` | `LIKE` |
| `gt` / `ge` / `lt` / `le` | `>` / `>=` / `<` / `<=` | `is_null` / `is_not_null` | `IS NULL` |
| `in` / `not_in` | `IN` / `NOT IN` | `between` / `not_between` | `BETWEEN` |
| `starts_with` / `ends_with` / `contains` | LIKE 变体 | `is_true` / `is_false` | `IS TRUE/FALSE` |
| `raw(sql)` | 原始 SQL（慎用） | `when(bool, fn)` | 条件执行 |

### 逻辑 / 排序 / 聚合

`and_()` / `or_()` 切换连接符 · `and_(nested)` / `or_(nested)` 嵌套条件组 · `order_by_asc` / `order_by_desc` · `limit` / `offset` · `select({...})` 指定列 · `group_by` · `having` · `inner_join` / `left_join` / `right_join` / `full_outer_join` · `select_count` / `select_sum` / `select_avg` / `select_min` / `select_max`

### 构建 SQL

```cpp
auto [sql, params] = qw.build_select_sql();          // MySQL 默认
auto [sql, params] = qw.build_select_sql(sql_dialect::postgresql);
auto [sql, params] = qw.build_count_sql();
auto [sql, params] = qw.build_delete_sql();
auto [sql, params] = qw.build_update_sql(entity);
```

### update_wrapper<T>

```cpp
auto uw = orm::update_wrapper<User>{}
    .set(&User::name, "Bob")
    .eq(&User::id, 42);
co_await mapper.update_by_wrapper(uw);
```

## 6. 查询构建器

```cpp
auto qb = orm::mysql_select<Article>()
    .where("`status` = {}", {orm::param_value::from_int(1)})
    .order_by("`view_count` DESC")
    .limit(10);
auto result = co_await db.find(qb);

auto del = orm::mysql_delete<Article>()
    .where("`status` = {}", {orm::param_value::from_int(0)});
co_await db.remove(del);
```

## 7. DDL 自动迁移

```cpp
auto result = co_await orm::mysql_synchronize_schema<Product>(cli);
if (result.is_err()) { /* handle */ }
if (result.created)
    std::println("表已创建");
else
    std::println("应用了 {} 项变更", result.diff.changes.size());
```

对比 C++ 模型与数据库表结构，自动 ADD / DROP / MODIFY 列。

## 8. MyBatis 风格 XML Mapper（动态 SQL）

XML mapper 提供 MyBatis 风格的 SQL 定义与动态 SQL 能力：SQL 写在 `.xml` 文件中，
运行时由 `mapper_registry` 加载、`dynamic_sql_processor` 根据参数上下文渲染为
最终 SQL 并执行。

### XML 文件格式

根标签必须是 `<mapper>` 且必须带 `namespace` 属性；语句标签为
`<select>` / `<insert>` / `<update>` / `<delete>`（各需 `id` 属性），
可复用片段用 `<sql id="...">` 定义、`<include refid="..."/>` 引用。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mapper namespace="UserMapper">

    <!-- 可复用 SQL 片段 -->
    <sql id="columns">
        `id`, `name`, `email`, `status`, `created_at`
    </sql>

    <!-- 简单查询 -->
    <select id="findById">
        SELECT <include refid="columns"/>
        FROM `users`
        WHERE `id` = #{id}
    </select>

    <!-- 动态条件查询 -->
    <select id="findByCondition">
        SELECT <include refid="columns"/>
        FROM `users`
        <where>
            <if test="name != null and name != ''">
                AND `name` = #{name}
            </if>
            <if test="status != null">
                AND `status` = #{status}
            </if>
        </where>
        ORDER BY `id` DESC
    </select>

    <insert id="insertUser">
        INSERT INTO `users` (`name`, `email`, `status`, `created_at`)
        VALUES (#{name}, #{email}, #{status}, #{created_at})
    </insert>

    <!-- 动态 SET（自动补 SET 关键字、去尾部逗号） -->
    <update id="updateSelective">
        UPDATE `users`
        <set>
            <if test="name != null">`name` = #{name},</if>
            <if test="email != null">`email` = #{email},</if>
        </set>
        WHERE `id` = #{id}
    </update>

    <delete id="deleteByStatus">
        DELETE FROM `users` WHERE `status` = #{status}
    </delete>
</mapper>
```

### 支持的标签

| 标签 | 用途 | 属性 |
|------|------|------|
| `<mapper>` | 根元素 | `namespace`（必填） |
| `<sql>` | 可复用 SQL 片段 | `id` |
| `<select>` / `<insert>` / `<update>` / `<delete>` | 语句定义 | `id` |
| `<include>` | 引入 `<sql>` 片段 | `refid` |
| `<if>` | 条件包含 | `test`（布尔表达式） |
| `<where>` | 自动补 `WHERE`、去掉首部 `AND`/`OR` | — |
| `<set>` | 自动补 `SET`、去掉尾部逗号 | — |
| `<trim>` | 前后缀增删 | `prefix`、`suffix`、`prefixOverrides`、`suffixOverrides` |
| `<foreach>` | 遍历集合 | `collection`、`item`、`open`、`close`、`separator` |
| `<choose>` / `<when>` / `<otherwise>` | 多分支（首个匹配的 `when` 生效） | `when` 带 `test` |
| `<bind>` | 绑定表达式到新变量 | `name`、`value` |

语句标签读取 `id`；`<select>` 支持 `resultMap` 或 `resultType`（两者互斥，加载时
校验）；所有语句都可声明 `parameterType`，并可通过
`statement_result_type()` / `statement_parameter_type()` 查询元数据。
`<foreach>` 支持可选的零基 `index` 属性，迭代期间作为参数上下文变量绑定。XML 中 `>`、`<`、`&` 需写成
`&gt;`、`&lt;`、`&amp;`。

### namespace 与语句 ID

- `<mapper namespace="UserMapper">` + `<select id="findById">` → 语句全限定 ID
  `UserMapper.findById`。
- `registry.find_statement()` 同时支持全限定 ID（`"namespace.id"`）和裸 ID（`"id"`，
  全局唯一时可用），C++ 调用处写法相同。
- namespace 与 C++ 接口/类**无绑定关系**，它只是语句 ID 的命名空间前缀；
  `<include refid>` 只能引用同一 namespace 内的 `<sql>` 片段。
- 一个 `mapper_registry` 可加载多个不同 namespace 的 mapper 文件。

### 参数占位符

| 语法 | 行为 |
|------|------|
| `#{name}` | 参数化占位符（安全，值进入参数列表后由 SQL 格式化层转义） |
| `${name}` | 直接字符串替换（有注入风险，用于 ORDER BY / GROUP BY / 表名等无法参数化的位置） |

- 支持点路径访问集合元素属性：`#{user.name}`、`${cond.field}`。
- 参数值来自 `param_context`（map、模型对象或集合，见「注册与加载」）。
- `#{property,jdbcType=...,javaType=...,typeHandler=...,mode=...,numericScale=...}`
  会绑定 `property`，并保留修饰元数据。MySQL XML session 默认使用
  `COM_STMT_PREPARE` / `COM_STMT_EXECUTE`，值以二进制参数编码发送而非插入 SQL
  文本；`set_native_prepared_statements(false)` 仅用于不支持 MySQL prepared
  protocol 的旧代理兼容。
- `${}` 只做直接替换，不能用来传递 JDBC 修饰符。

### test 表达式

`<if test>` / `<when test>` / `<bind value>` 使用内置表达式引擎，支持：

- 比较：`==`、`!=`、`<`、`>`、`<=`、`>=`（XML 中写 `&lt;` `&gt;`）
- 逻辑：`and`、`or`、`not`（不支持 `&&` / `||`）
- 算术：`+`、`-`、`*`、`/`、`%`，括号分组
- 字面量：整数、浮点、`'单引号'` 或 `"双引号"` 字符串、`true`、`false`、`null`
- 属性路径：`a.b.c` 逐级解析

示例：`test="name != null and name != ''"`、`test="limit &gt; 0"`、
`test="role == 'admin'"`、`test="includeOrders == true"`。

### 动态 SQL 示例

**foreach — IN 子句 / 批量插入**：

```xml
<!-- 集合由 param_context::add_collection("ids", ...) 提供 -->
<select id="findByIds">
    SELECT <include refid="columns"/>
    FROM `users`
    WHERE `id` IN
    <foreach collection="ids" item="id" open="(" close=")" separator=",">
        #{id}
    </foreach>
</select>

<!-- 批量插入：每个元素是带字段的 param_context -->
<insert id="batchInsert">
    INSERT INTO `users` (`name`, `email`, `status`, `created_at`)
    VALUES
    <foreach collection="users" item="user" separator=",">
        (#{user.name}, #{user.email}, #{user.status}, #{user.created_at})
    </foreach>
</insert>
```

**choose/when/otherwise**：

```xml
<select id="findByRole">
    SELECT <include refid="columns"/>
    FROM `users`
    <where>
        <choose>
            <when test="role == 'admin'">AND `status` = 1</when>
            <when test="role == 'moderator'">AND `status` IN (1, 2)</when>
            <otherwise>AND `status` = 0</otherwise>
        </choose>
    </where>
</select>
```

**trim + bind**：

```xml
<select id="advancedFilter">
    SELECT <include refid="columns"/>
    FROM `users`
    <where>
        <!-- 输出 ( `name` LIKE ? OR `email` LIKE ? )，去掉首部 OR -->
        <trim prefix="(" suffix=")" prefixOverrides="OR">
            <if test="namePattern != null and namePattern != ''">
                OR `name` LIKE #{namePattern}
            </if>
            <if test="emailPattern != null and emailPattern != ''">
                OR `email` LIKE #{emailPattern}
            </if>
        </trim>
    </where>
</select>

<select id="dynamicTableQuery">
    SELECT * FROM ${tableName}
    <where>
        <foreach collection="filters" item="filter" separator="AND">
            <bind name="fieldName" value="filter.field"/>
            <bind name="fieldValue" value="filter.value"/>
            ${fieldName} = #{fieldValue}
        </foreach>
    </where>
</select>
```

### 结果集映射

普通查询的结果类型仍由 C++ 侧决定；XML `<select resultMap="...">` 则可用于动态对象图：

- `session.query<T>(...)`：按**列名**匹配 `CNETMOD_MODEL` 注册的字段名
  （列别名 `AS xxx` 只要与字段名一致即可映射），返回 `orm_result<T>`。
- `session.query_tuple<Ts...>(...)`：按**列序**映射到 tuple 元素，适合聚合/单列查询，
  无需定义模型。
- `session.execute_query(...)`：返回原始 `result_set`（`columns` + `rows`），自行解析。
- `session.query_object_graph(...)`：返回 `std::expected<std::vector<mapped_object>, std::string>`；支持
  join 行按 `<id>` 去重聚合，以及 `association` / `collection` 的 eager 嵌套 select。

### 注册与加载

**mapper_registry API**（同步，返回 `std::expected<void, std::string>`）：

| 方法 | 说明 |
|------|------|
| `load_file(path)` | 加载单个 `.xml` 文件 |
| `load_xml(content)` | 从字符串加载（如嵌入式资源） |
| `load_directory(dir)` | 加载目录下所有 `.xml` 文件 |
| `find_statement(id)` | 查找语句节点（`"Ns.id"` 或裸 `"id"`） |
| `statement_type(id)` | 返回语句标签名（select/insert/update/delete） |

**mysql_mapper_session API**（`orm::mysql_mapper_session`）：

```cpp
mysql_mapper_session(client& cli, mapper_registry& registry);
void set_sql_logging(bool enabled);               // 打印生成/最终 SQL
auto last_generated_sql() const -> std::string_view;
auto last_final_sql() const -> std::string_view;

// select → 模型（参数可以是 param_context、模型对象或 map）
template <Model T> auto query(std::string_view id, const param_context& ctx) -> task<orm_result<T>>;
template <Model T> auto query(std::string_view id, const T& model) -> task<orm_result<T>>;

// select → tuple（按列序）
template <typename... Ts> auto query_tuple(std::string_view id, const param_context& ctx)
    -> task<orm_result<std::tuple<Ts...>>>;

// insert/update/delete → exec_result{affected_rows, last_insert_id, error_msg}
auto execute(std::string_view id, const param_context& ctx) -> task<exec_result>;
template <Model T> auto execute(std::string_view id, const T& model) -> task<exec_result>;

// 任意语句 → 原始 result_set
auto execute_query(std::string_view id, const param_context& ctx) -> task<result_set>;

// select(resultMap) -> 动态对象图
auto query_object_graph(std::string_view id, const param_context& ctx)
    -> task<std::expected<std::vector<mapped_object>, std::string>>;
```

**参数传递（param_context）**：

```cpp
// 1. map 参数
auto ctx = orm::param_context::from_map({
    {"name", orm::param_value::from_string("Alice")},
    {"status", orm::param_value::from_int(1)},
    {"limit", orm::param_value::from_int(10)}});

// 2. 模型对象作为参数源（按字段名映射）
auto ctx2 = orm::param_context::from_model(user);

// 3. 集合参数（供 <foreach> 使用）
auto ctx3 = orm::param_context::from_map({});
std::vector<orm::param_context> items;
items.push_back(orm::param_context::from_map({{"id", orm::param_value::from_int(1)}}));
items.push_back(orm::param_context::from_map({{"id", orm::param_value::from_int(2)}}));
ctx3.add_collection("ids", std::move(items));
```

**完整示例**（加载 → 建表 → 查询 → 插入 → foreach）：

```cpp
import std;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

using namespace cnetmod;
using namespace cnetmod::orm;

struct User
{
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
    std::time_t created_at = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_),
    CNETMOD_FIELD(created_at, "created_at", timestamp, NULLABLE))

auto work(mysql::client& cli) -> task<void>
{
    // 1. 加载 mapper（文件 / 目录 / 字符串三种方式）
    mapper_registry registry;
    if (auto r = registry.load_file("mappers/user_mapper.xml"); !r)
        std::println("load failed: {}", r.error());
    // registry.load_directory("mappers");
    // registry.load_xml(xml_string);

    // 2. （可选）确保表存在
    co_await orm::mysql_synchronize_schema<User>(cli);

    // 3. 创建 session，打开 SQL 日志
    mysql_mapper_session session(cli, registry);
    session.set_sql_logging(true);

    // 4. select —— map 参数
    auto r1 = co_await session.query<User>("UserMapper.findByCondition",
        param_context::from_map({{"name", param_value::from_string("Alice")},
            {"status", param_value::from_int(1)},
            {"limit", param_value::from_int(10)}}));
    if (r1.ok())
        std::println("found {} users", r1.data.size());

    // 5. insert —— 模型作为参数源，回填 last_insert_id
    User nu;
    nu.name = "Charlie";
    nu.email = "charlie@example.com";
    nu.status = 1;
    nu.created_at = std::time(nullptr);
    auto r2 = co_await session.execute("UserMapper.insertUser", nu);
    if (r2.ok())
        std::println("inserted id={}", r2.last_insert_id);

    // 6. foreach —— 集合参数
    auto ctx = param_context::from_map({});
    std::vector<param_context> ids;
    for (int i = 1; i <= 5; ++i)
        ids.push_back(param_context::from_map({{"id", param_value::from_int(i)}}));
    ctx.add_collection("ids", std::move(ids));
    auto r3 = co_await session.query<User>("UserMapper.findByIds", ctx);

    // 7. query_tuple —— 聚合查询按列序映射，无需模型
    auto r4 = co_await session.query_tuple<std::int64_t, double>(
        "ProjectMapper.selectStats",
        param_context::from_map({{"start_date", param_value::from_string("2026-01-01")},
            {"end_date", param_value::from_string("2026-12-31")}}));
}
```

> **生产模式**：`mapper_registry` 通常在启动时全局构建一次（`static` 全局变量或
> `load_xml` 加载嵌入式资源），之后每个请求用连接池取出的 `mysql::client&`
> 临时构造 `mysql_mapper_session`（构造开销极低）。

## 9. 自动填充 / 软删除 / 多租户

### 自动填充
`CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT)` — `fill_strategy`: `current_timestamp`, `current_date`, `current_time`, `uuid`, `custom`。`global_auto_fill_interceptor()` 获取全局实例。

### 软删除
`CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)` — `logical_delete_interceptor` 自动将 DELETE 转为 `UPDATE SET deleted=1`，SELECT 追加 `deleted=0`。`global_logical_delete_interceptor()`。

Nullable datetime markers use an explicit mode. Active rows are selected with
`deleted_at IS NULL`, and deletion writes `CURRENT_TIMESTAMP`:

```cpp
logical_delete_config config;
config.field_name = "deleted_at";
config.mode = logical_delete_mode::nullable_datetime;
config.touch_fields = {{"updated_at",
    logical_delete_touch_value::current_timestamp}};
logical_delete_interceptor interceptor{std::move(config)};
```

`touch_fields` 与逻辑删除标记在同一条 `UPDATE` 中更新，保持单语句原子性。字段名只能是
安全 SQL 标识符，赋值只能从 `current_timestamp`、`current_date`、`current_time`
枚举选择；不接受任意 SQL 表达式。重复字段、非法标识符或与删除标记重复会在配置
拦截器时抛出 `std::invalid_argument`。

### 多租户
`CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID)` — `tenant_context::set_tenant_id(id)` 设置线程级租户；`tenant_guard guard(id)` RAII 守卫；`multi_tenant_interceptor` 自动注入条件。`global_multi_tenant_interceptor()`。

## 10. database_session<Client> — 协议无关会话

```cpp
template <asynchronous_database_client Client>
class database_session {
    explicit database_session(Client& client,
        sql_dialect dialect = sql_dialect::mysql);
    auto query(std::string_view sql) -> task<query_result>;
    auto execute(std::string_view sql) -> task<query_result>;
    auto execute(parameterized_query) -> task<query_result>;

    template <Model T> auto find_all() -> task<model_result<T>>;
    template <Model T> auto find_by_id(param_value) -> task<model_result<T>>;
    template <Model T> auto find_one_by(std::string_view, param_value)
        -> task<model_result<T>>;
    template <Model T> auto insert(T&) -> task<model_result<T>>;
    template <Model T> auto update(const T&) -> task<model_result<T>>;
    template <Model T> auto remove(const T&) -> task<model_result<T>>;
    template <Model T> auto remove_by(std::string_view, param_value)
        -> task<model_result<T>>;
    template <Model T> auto remove_by_id(param_value) -> task<model_result<T>>;
    template <Model T> auto find(const query_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto remove(const query_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto update(const update_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto execute(const query_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto execute(const update_wrapper<T>&) -> task<model_result<T>>;
    auto transaction(Func&&) -> task<query_result>;
    auto transaction(Func&&, isolation_level) -> task<query_result>;
};
```

MySQL failures retain both the native server error number (for example `1062`
for a duplicate key) and SQLSTATE. Prefer `error_code` for vendor-specific
classification and keep `sql_state` for portable error classes.

Model fields may use `std::optional<calendar_datetime>` for nullable
`DATETIME`/`TIMESTAMP` columns. Mapping a database datetime into an integral
Unix time treats the stored wall-clock fields as UTC and therefore does not
depend on the process or database-session timezone.

`database_session` is the protocol-independent repository surface. It accepts
either a MySQL or PostgreSQL client and preserves the native wire client below
it. `model_result<T>` contains `data`, `affected_rows`, `last_insert_id`,
`error_msg`, `sql_state`, and the native `error_code`; use `ok()` and `first()`
to distinguish an empty query from a failed operation.

```cpp
import cnetmod.orm;
import cnetmod.protocol.mysql;

task<void> load_user(mysql::client& client) {
    orm::database_session db{client, orm::sql_dialect::mysql};

    auto user = co_await db.find_by_id<User>(orm::param_value::from_int(42));
    if (!user.ok())
        co_return;

    auto active = co_await db.find(
        orm::query_wrapper<User>{}.eq("status", 1).order_by_desc("id"));
}
```

For PostgreSQL construct the same session with `sql_dialect::postgresql`.
The session then emits quoted identifiers, `$1…$N` placeholders, uses the
client's parameter binding, and adds `RETURNING *` for model inserts, updates,
and deletes. MySQL keeps its native formatting path and fills an auto-increment
primary key from `last_insert_id`. This makes the model mapping and CRUD API
portable without removing `mysql_session` or `postgresql_session` for
protocol-specific operations.

`query_wrapper<T>` and `update_wrapper<T>` never perform I/O: they only retain
structured conditions and values, then build dialect-aware parameterized SQL.
`database_session` is the sole execution/mapping boundary. The convenience
methods (`find_by_id`, `remove_by_id`, and model `insert`/`update`/`remove`)
delegate to the same wrapper path where applicable; use `find(wrapper)`,
`update(wrapper)`, and `remove(wrapper)` for conditional work. Direct dispatch
is also available: `execute(query_wrapper)` defaults to SELECT, while
`execute(query_wrapper.as_delete())` performs DELETE; `execute(update_wrapper)`
performs UPDATE.

## CMake 启用

```cmake
-DCNETMOD_ENABLE_ORM=ON     # ORM（默认 ON）
-DCNETMOD_ENABLE_MYSQL=ON   # MySQL 协议
```

## 连接池（生产级用法）

### MySQL connection_pool

ORM 的 `mysql_session` 接受 `mysql::client&`，而 `mysql::connection_pool` 提供的 `pooled_connection` 可通过 `->` 操作符获取 `mysql::client&`，两者天然集成。

**Pool API**（来自 `mysql_pool.cppm`）：

```cpp
// 连接池参数
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string username, password, database;
    ssl_mode ssl = ssl_mode::enable;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(20);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
    // ...
};

// RAII 连接句柄 — 析构时自动归还
class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> mysql::client&;
    auto operator->() noexcept -> mysql::client*;
    void return_without_reset();
};

// 连接池
class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
};

// 分片连接池（多 worker 专用）
class sharded_connection_pool {
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params);
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params,
        std::size_t num_shards);
    auto async_run() -> task<void>;
    auto async_get_connection(io_context& io) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;
};
```

**单线程 + 连接池示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    // 创建连接池
    cn::mysql::connection_pool pool(ctx, cn::mysql::pool_params{
        .host = "127.0.0.1",
        .port = 3306,
        .username = "root",
        .password = "secret",
        .database = "myapp",
        .initial_size = 2,
        .max_size = 16,
    });

    // 启动连接池后台维护
    cn::spawn(ctx, pool.async_run());
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(100));

    // 从池中获取连接
    auto conn = co_await pool.async_get_connection();
    if (!conn) {
        std::println("get connection failed: {}", conn.error().message());
        co_return;
    }

    // 用 pooled_connection 创建 ORM session
    orm::mysql_session db(conn->get());

    Article a;
    a.title = "Hello ORM";
    a.status = 1;
    auto r = co_await db.insert(a);
    std::println("inserted id={}", a.id);

    auto all = co_await db.find_all<Article>();
    std::println("total: {}", all.data.size());

    // pooled_connection 析构时自动归还连接池
}
```

## 多核服务器部署

### sharded_connection_pool + server_context

生产环境中，每个 worker 线程使用 `sharded_connection_pool` 获取本分片连接，避免跨线程竞争。

**架构**：

```
server_context
├── accept_io()          — 接受 HTTP 请求
├── worker_io[0]         — sharded_pool shard[0]
├── worker_io[1]         — sharded_pool shard[1]
├── worker_io[2]         — sharded_pool shard[2]
└── worker_io[3]         — sharded_pool shard[3]
```

**生产级 CRUD 服务示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.http;
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

namespace cn = cnetmod;
namespace mysql = cnetmod::mysql;

// 模型定义
struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_))

// 全局分片连接池指针（worker 共享）
mysql::sharded_connection_pool* g_pool = nullptr;

// 处理 GET /users — 查询所有用户
auto handle_get_users(cn::io_context& io, const cn::http::request& req)
    -> cn::task<cn::http::response>
{
    auto conn = co_await g_pool->async_get_connection(io);
    if (!conn)
        co_return cn::http::make_json_response(500, R"({"error":"db unavailable"})");

    orm::mysql_session db(conn->get());
    auto result = co_await db.find_all<User>();

    // 构建 JSON 响应...
    co_return cn::http::make_json_response(200, "[...]");
}

// 处理 POST /users — 创建用户
auto handle_create_user(cn::io_context& io, const cn::http::request& req)
    -> cn::task<cn::http::response>
{
    auto conn = co_await g_pool->async_get_connection(io);
    if (!conn)
        co_return cn::http::make_json_response(500, R"({"error":"db unavailable"})");

    orm::mysql_session db(conn->get());

    User user;
    user.name = "Alice";
    user.status = 1;
    auto r = co_await db.insert(user);

    co_return cn::http::make_json_response(201,
        std::format(R"({{"id":{}}})", user.id));
}

int main() {
    cn::net_init net;

    // 4 worker 线程
    cn::server_context sctx(4, 4);

    // 分片连接池 — 每个 worker 一个分片，避免锁竞争
    mysql::sharded_connection_pool pool(
        sctx.worker_ios(),
        mysql::pool_params{
            .host = "127.0.0.1",
            .port = 3306,
            .username = "root",
            .password = "secret",
            .database = "myapp",
            .initial_size = 4,     // 每分片初始连接
            .max_size = 32,        // 每分片最大连接
            .ssl = mysql::ssl_mode::disable,
        });
    g_pool = &pool;

    // HTTP 路由
    cn::http::router router;
    router.get("/users", [](cn::io_context& io, const cn::http::request& req)
        -> cn::task<cn::http::response> {
        co_return co_await handle_get_users(io, req);
    });
    router.post("/users", [](cn::io_context& io, const cn::http::request& req)
        -> cn::task<cn::http::response> {
        co_return co_await handle_create_user(io, req);
    });

    cn::http::server srv(sctx);
    srv.listen("0.0.0.0", 8080);
    srv.set_router(std::move(router));

    // 启动连接池和服务器
    cn::spawn(sctx.accept_io(), pool.async_run());
    cn::spawn(sctx.accept_io(), srv.run());

    sctx.run();
}
```

> **关键模式**：`async_get_connection(io)` 传入当前 worker 的 `io_context`，分片池优先从对应分片获取连接，避免跨线程竞争。

### 每 worker 独立 session 模式

如果不想使用分片池，也可以为每个 worker 创建独立的 `connection_pool` + `mysql_session`：

```cpp
// 在 worker 启动时为每个 io_context 创建独立连接池
for (auto* worker_io : sctx.worker_ios()) {
    auto* pool = new mysql::connection_pool(*worker_io, mysql::pool_params{
        .host = "127.0.0.1",
        .username = "root",
        .password = "secret",
        .database = "myapp",
        .max_size = 8,
    });
    cn::spawn(*worker_io, pool->async_run());
    // pool 与 worker_io 生命周期一致
}
```

> **推荐**：大多数场景使用 `sharded_connection_pool` 更简洁；独立池适合需要不同配置的混合负载。

## Do's & Don'ts（连接池补充）

| Do | Don't |
|---|---|
| 多核使用 `sharded_connection_pool` + `worker_ios()` | 不要跨 worker 共享单个 `connection_pool` |
| `pooled_connection` 用完自动归还，作用域控制在最小 | 不要长期持有 `pooled_connection` 不放 |
| 合理设置 `max_size` 避免数据库连接耗尽 | 不要设置 `max_size` 超过数据库 `max_connections` |
| 使用 `pool_timeout` 防止获取连接无限等待 | 不要忽略 `async_get_connection()` 的错误 |
<!-- END SOURCE: skill/database/database-orm.md -->

<!-- BEGIN SOURCE: skill/database/mongodb.md -->
# Source: `skill/database/mongodb.md`

# MongoDB 协议模块

## 等待关闭动作

`connection_pool::async_close() -> task<void>` 返回持有共享池状态的关闭任务，
Application MongoDB 服务使用 `co_await` 等待它，不通过 `close()` 投递后立即报告成功。
此入口等待关闭动作本身，不自动等待独立启动的维护与借用者；这些任务仍需
由调用方监管。锁竞争产生的已登记归还动作由连接槽持有，`async_close()` 会等待
这些投递执行完毕；它不等待调用方尚未释放的租约。I/O 上下文必须保持运行直到收尾完成。
排队借用的超时任务由 `acquire()` 持有，借用返回前会取消并等待
该任务结束；超时执行异常传播给借用者，不再从裸 `spawn()` 终止进程。
借用必须在所属 I/O 线程执行，不可提前销毁仍在挂起的借用任务。
`std::stop_source::request_stop()` 可以从其他线程发起；借用取消回调只通知定时器，
队列移除与结果发布由所属 I/O 线程上的超时任务完成，不从取消线程操作池元数据。
原有 `close()` 入口仍不是完整的关闭等待屏障。
已请求关闭或已关闭的池调用 `warm_up()` 返回 `connection_closed`，即使最小连接数为零，
也不会将关闭状态当作预热成功；重新启动服务不能复用已关闭的池。

## 命令超时与 I/O 取消

命令读写（普通 socket 与 SSL 分支）使用连接持有的取消 token。命令超时先取消
挂起的 I/O，命令与看门狗收尾后再关闭连接；不能仅靠关闭 fd 唤醒 epoll 中的读取。
`cancel_active_command()` 通知同一 token，不直接从调用线程销毁 socket 或 SSL 对象。
该 token 仅在上一条命令 I/O 已结束后复用；仍禁止并发使用同一连接。
Arch epoll/ASAN 已验证无响应 hello 的超时收尾；这不是 MongoDB TLS 运行验证。

> 异步 MongoDB C++ 客户端，基于 Wire Protocol，支持 SCRAM-SHA-256 认证、TLS、连接池、事务、变更流与重试逻辑。

**import**: `import cnetmod.protocol.mongodb;`
**CMake**: `-DCNETMOD_ENABLE_MONGODB=ON`
**源码**: `src/protocol/mongodb/`

## 场景导航

| 场景 | 推荐入口 |
|------|----------|
| 简单查询 / CRUD | `connection::command` |
| 连接管理 | `topology_connection_pool` |
| 多节点部署 | `connection_pool` + `topology_monitor` |
| 事务 | `client_session` + `start_transaction` |
| 变更监听 | `change_stream` |
| BSON 处理 | `bson_document`, `bson_array` |
| 重试机制 | `retryable_operation_policy` |

## API 参考

### 类型系统 (`bson_document`)

核心 BSON 类型：

```cpp
struct bson_null {}; struct bson_binary { std::uint8_t subtype = 0; std::vector<std::byte> bytes; };
struct bson_object_id { std::array<std::byte, 12> bytes{}; };
struct bson_datetime { std::int64_t milliseconds_since_epoch = 0; };
struct bson_timestamp { std::uint32_t increment = 0; std::uint32_t seconds = 0; };
struct bson_regex { std::string pattern; std::string options; };
struct bson_min_key {}; struct bson_max_key {};

class bson_value { using storage = std::variant<bson_null, double, std::string, bson_object_id, bool, bson_datetime, bson_timestamp, bson_min_key, bson_max_key, std::int32_t, std::int64_t>; auto data() const noexcept -> const storage&; template <class T> auto get_if() const noexcept -> const T*; };
class bson_document { using element = std::pair<std::string, bson_value>; auto append(std::string key, bson_value value) -> bson_document&; auto set(std::string key, bson_value value) -> bson_document&; [[nodiscard]] auto find(std::string_view key) const noexcept -> const bson_value*; [[nodiscard]] auto contains(std::string_view key) const noexcept -> bool; auto size() const noexcept -> std::size_t; };
auto encode_bson_document(const bson_document&, bson_limits = {}) -> result<std::vector<std::byte>>;
auto decode_bson_document(std::span<const std::byte>, bson_limits = {}) -> result<bson_document>;
```

**示例**:
```cpp
using namespace cnetmod::mongodb;
bson_document doc{ {"name", bson_value{"Alice"}}, {"age", bson_value{std::int32_t{30}}}, {"status", bson_value{true}}, {"scores", bson_array{95, 88, 92}} };
auto rs = co_await db.command("users", doc);
```

### `error` — 错误模型

**签名**:
```cpp
enum class error_code {
    invalid_bson, message_too_large, protocol_error, connection_failed,
    tls_failed, authentication_failed, compression_failed, server_selection_failed,
    pool_exhausted, transaction_failed, change_stream_closed, operation_timed_out, ...
};

struct error {
    error_code code = error_code::protocol_error;
    std::string message;
    std::int32_t server_code = 0;
    std::string server_code_name;
    std::map<std::string, std::string> labels;
};

template <class T> using result = std::expected<T, error>;
auto make_error(error_code code, std::string message) -> error;
```

### 连接选项 (`connection_options`)

**签名**:
```cpp
struct connection_options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 27017;
    std::string database = "admin", username, password;
    std::string authentication_database = "admin";
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
    std::chrono::milliseconds connect_timeout{10000};
    std::chrono::milliseconds command_timeout{30000};
    bool enable_zlib_compression = true;
    std::size_t max_message_bytes = 48 * 1024U * 1024U;
};
```

### `connection` — 单连接客户端

**签名**:
```cpp
class connection {
    explicit connection(io_context& context) noexcept;
    ~connection();
    auto connect(connection_options options = {}) -> task<result<void>>;
    auto command(std::string_view database, bson_document command_document)
        -> task<result<bson_document>>;
    auto command(bson_document command_document) -> task<result<bson_document>>;
    auto ping() -> task<result<void>>;
    auto is_open() const noexcept -> bool;
    auto secure_channel() const noexcept -> bool;
    auto capabilities() const noexcept -> const server_capabilities&;
    auto hello_response() const noexcept -> const bson_document&;
    void cancel_active_command() noexcept;
    void close() noexcept;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mongodb;

namespace cn = cnetmod;
namespace mg = cn::mongodb;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    mg::connection conn(ctx);
    mg::connection_options opts;
    opts.host = "127.0.0.1";
    opts.database = "mydb";

    auto rs = co_await conn.connect(std::move(opts));
    if (!rs) { ctx.stop(); co_return; }
    co_await conn.ping();
    conn.close();
    ctx.stop();
}
```

### `connection_pool` — 单节点连接池

**签名**:
```cpp
struct connection_pool_options {
    connection_options connection;
    std::size_t minimum_size = 0;
    std::size_t maximum_size = 32;
    std::size_t maximum_connecting = 2;
    std::chrono::milliseconds wait_queue_timeout{10000};
    std::chrono::milliseconds maximum_idle_time{60000};
    std::chrono::milliseconds health_check_interval{30000};
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> connection&;
    auto operator->() noexcept -> connection*;
    void discard() noexcept;
};

class connection_pool {
    connection_pool(io_context& context, connection_pool_options options);
    ~connection_pool();
    auto warm_up() -> task<result<void>>;
    auto acquire() -> task<result<pooled_connection>>;
    auto acquire(std::stop_token cancellation) -> task<result<pooled_connection>>;
    auto health_check() -> task<void>;
    auto close() noexcept;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
};
```

**示例**:
```cpp
mg::connection_pool_options opts;
opts.connection.host = "127.0.0.1";
opts.connection.database = "mydb";
opts.minimum_size = 4;
opts.maximum_size = 32;

mg::connection_pool pool(ctx, opts);
co_await pool.warm_up();

auto conn_r = co_await pool.acquire();
if (conn_r) {
    auto& conn = conn_r->get();
    co_await conn.command("mydb", bson_doc{{"ping", 1}});
} // pooled_connection 析构时自动归还
```

### `server_description` & `topology_monitor`

```cpp
enum class server_kind { unknown, standalone, mongos, replica_primary, replica_secondary, replica_arbiter, load_balancer };

struct server_address { std::string host; std::uint16_t port = 27017; };

struct server_description {
    server_address address;
    server_kind kind = server_kind::unknown;
    std::string replica_set_name;
    std::optional<std::string> primary;
    std::vector<server_address> hosts;
    std::map<std::string, std::string> tags;
    std::optional<std::chrono::milliseconds> round_trip_time;
    std::int32_t minimum_wire_version = 0;
    auto readable() const noexcept -> bool;
    auto writable() const noexcept -> bool;
};

class topology_monitor {
    topology_monitor(std::optional<std::string> required_replica_set = {});
    void update(server_description description);
    void mark_unknown(const server_address& address, error reason);
    [[nodiscard]] auto kind() const noexcept -> topology_kind;
    [[nodiscard]] auto snapshot() const -> std::vector<server_description>;
    auto select_server(server_selection_options options = {}) const -> result<server_description>;
    auto check_server(io_context& context, connection_options options) -> task<result<server_description>>;
};
```

### `topology_connection_pool` — 多节点拓扑连接池

```cpp
struct topology_connection_pool_options {
    std::vector<server_address> seeds{{"127.0.0.1", 27017}};
    connection_pool_options per_server_pool;
    std::optional<std::string> replica_set_name;
};

class topology_connection_pool {
    topology_connection_pool(io_context& context, topology_connection_pool_options options);
    auto refresh() -> task<result<void>>;
    auto run_monitoring(std::stop_token stop, std::chrono::milliseconds heartbeat_frequency = std::chrono::seconds{10}) -> task<void>;
    auto acquire(server_selection_options selection = {}) -> task<result<pooled_connection>>;
    auto command(std::string_view database, bson_document command_document, server_selection_options selection = {}) -> task<result<bson_document>>;
    [[nodiscard]] auto topology() noexcept -> topology_monitor&;
    auto close() noexcept;
};
```

### `client_session` — 客户端会话（事务）

**签名**:
```cpp
enum class transaction_state { none, starting, in_progress, committed, aborted };

struct transaction_options {
    std::optional<std::string> read_concern_level;
    std::optional<std::string> write_concern = std::string{"majority"};
    std::optional<std::chrono::milliseconds> maximum_commit_time;
    std::size_t maximum_commit_attempts = 2;
    std::chrono::milliseconds commit_retry_backoff{10};
};

class client_session {
    client_session();
    ~client_session();
    auto start_transaction(transaction_options options = {}) -> result<void>;
    auto command(connection_pool& pool, std::string_view database, bson_document command_document) -> task<result<bson_document>>;
    auto commit_transaction(connection_pool& pool) -> task<result<void>>;
    auto abort_transaction(connection_pool& pool) -> task<result<void>>;
    void reset() noexcept;
    [[nodiscard]] auto id() const noexcept -> const bson_binary&;
    [[nodiscard]] auto state() const noexcept -> transaction_state;
    [[nodiscard]] auto transaction_number() const noexcept -> std::int64_t;
    [[nodiscard]] auto has_pinned_connection() const noexcept -> bool;
};
```

**示例**:
```cpp
mg::client_session session;
auto start_r = co_await session.start_transaction();
if (!start_r) { /* handle error */ }
auto commit_rs = co_await session.commit_transaction(pool);
co_await session.abort_transaction(pool); // 或提交
```

### `change_stream` — 变更流监听

**签名**:
```cpp
struct change_stream_options {
    std::string full_document = "default";
    std::optional<bson_document> resume_after;
    std::optional<bson_document> start_after;
    std::int32_t batch_size = 100;
    std::chrono::milliseconds maximum_await_time{1000};
    std::vector<bson_document> pipeline;
};

class change_stream {
    change_stream(connection_pool& pool, std::string database, std::string collection, change_stream_options options = {});
    auto open() -> task<result<void>>;
    auto next() -> task<result<std::optional<bson_document>>>;
    auto close() -> task<void>;
    [[nodiscard]] auto resume_token() const noexcept -> const bson_document*;
    [[nodiscard]] auto cursor_id() const noexcept -> std::int64_t;
};
```

**示例**:
```cpp
mg::change_stream cs(pool, "mydb", "users");
co_await cs.open();

while (true) {
    auto event_opt = co_await cs.next();
    if (!event_opt || !event_opt->value()) break;
    const auto& event = event_opt.value().value();
    // 处理变更事件
}
```

### `retryable_operation` — 重试策略

```cpp
enum class operation_kind { read, write, commit_transaction, change_stream_get_more };

struct retryable_operation_options {
    bool retry_reads = true;
    bool retry_writes = true;
    std::size_t maximum_attempts = 2;
    std::chrono::milliseconds initial_backoff{10};
    std::chrono::milliseconds maximum_backoff{500};
};

class retryable_operation_policy {
    retryable_operation_policy(retryable_operation_options options = {});
    [[nodiscard]] auto should_retry(operation_kind operation, const error& failure, std::size_t completed_attempts, bool acknowledged_write = true) const noexcept -> bool;
    [[nodiscard]] auto backoff(std::size_t completed_attempts) const noexcept -> std::chrono::milliseconds;
};

auto execute_retryable_command(connection_pool& pool, std::string_view database, bson_document command_document, operation_kind operation, retryable_operation_options options = {}) -> task<result<bson_document>>;
auto execute_retryable_command(topology_connection_pool& pool, std::string_view database, bson_document command_document, operation_kind operation, server_selection_options selection = {}, retryable_operation_options options = {}) -> task<result<bson_document>>;
```

## 连接池（生产级用法）

### Pool API

MongoDB 提供两级连接池：单节点 `connection_pool` 和多节点 `topology_connection_pool`。

**`connection_pool`** — 单节点连接池：

```cpp
struct connection_pool_options {
    connection_options connection;                       // 连接参数（host/auth/tls 等）
    std::size_t minimum_size = 0;                        // 最小连接数
    std::size_t maximum_size = 32;                       // 最大连接数
    std::size_t maximum_connecting = 2;                  // 最大并发建连数
    std::chrono::milliseconds wait_queue_timeout{10000}; // 等待连接超时
    std::chrono::milliseconds maximum_idle_time{60000};  // 空闲连接回收时间
    std::chrono::milliseconds health_check_interval{30000}; // 健康检查间隔
};

class connection_pool {
    connection_pool(io_context& context, connection_pool_options options);
    ~connection_pool();
    auto warm_up() -> task<result<void>>;                // 预热连接池
    auto acquire() -> task<result<pooled_connection>>;   // 获取连接
    auto acquire(std::stop_token cancellation) -> task<result<pooled_connection>>;
    auto health_check() -> task<void>;                   // 手动健康检查
    auto run_maintenance(std::stop_token stop) -> task<void>; // 后台维护（清理空闲/重连）
    void close() noexcept;                               // 关闭池
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto checked_out_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
    auto context() noexcept -> io_context&;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> connection&;
    auto operator->() noexcept -> connection*;
    void discard() noexcept;   // 标记为废弃（连接异常时）
    // 析构时自动归还
};
```

**`topology_connection_pool`** — 多节点拓扑感知连接池（副本集/分片集群）：

```cpp
struct topology_connection_pool_options {
    std::vector<server_address> seeds{{"127.0.0.1", 27017}};  // 种子节点
    connection_pool_options per_server_pool;                    // 每个节点的池配置
    std::optional<std::string> replica_set_name;               // 副本集名称
};

struct topology_connection_pool_statistics {
    std::size_t server_pool_count{};
    std::size_t connection_count{};
    std::size_t idle_connection_count{};
    std::size_t checked_out_connection_count{};
    std::size_t waiting_request_count{};
};

class topology_connection_pool {
    topology_connection_pool(io_context& context, topology_connection_pool_options options);
    auto refresh() -> task<result<void>>;                // 刷新拓扑信息
    auto run_monitoring(std::stop_token stop,
        std::chrono::milliseconds heartbeat_frequency = std::chrono::seconds{10})
        -> task<void>;                                   // 后台拓扑监控
    auto acquire(server_selection_options selection = {})
        -> task<result<pooled_connection>>;              // 按策略选择节点获取连接
    auto command(std::string_view database, bson_document command_document,
        server_selection_options selection = {})
        -> task<result<bson_document>>;                  // 直接执行命令（自动选节点）
    auto topology() noexcept -> topology_monitor&;
    auto statistics() -> topology_connection_pool_statistics;
    void close() noexcept;
};
```

**示例 — 生产级单节点连接池**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mongodb;

namespace cn = cnetmod;
namespace mg = cn::mongodb;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    mg::connection_pool_options opts;
    opts.connection.host = "mongo.example.com";
    opts.connection.database = "production_db";
    opts.connection.username = "app_user";
    opts.connection.password = "secret";
    opts.connection.tls = true;
    opts.minimum_size = 4;
    opts.maximum_size = 32;
    opts.maximum_connecting = 4;
    opts.wait_queue_timeout = std::chrono::milliseconds(5000);
    opts.maximum_idle_time = std::chrono::minutes(5);

    mg::connection_pool pool(ctx, opts);

    // 预热连接池
    auto warmup_r = co_await pool.warm_up();
    if (!warmup_r) {
        std::println("预热失败: {}", warmup_r.error().message);
        ctx.stop();
        co_return;
    }

    // 启动后台维护（清理空闲连接、健康检查）
    std::stop_source stop_src;
    cn::spawn(ctx, pool.run_maintenance(stop_src.get_token()));

    // 获取连接并执行命令
    auto conn_r = co_await pool.acquire();
    if (conn_r) {
        mg::bson_document cmd;
        cmd.append("find", mg::bson_value{std::string("users")});
        cmd.append("filter", mg::bson_value{mg::bson_document{
            {"status", mg::bson_value{std::string("active")}}}});
        cmd.append("limit", mg::bson_value{std::int32_t{100}});

        auto result = co_await conn_r->get().command("production_db", std::move(cmd));
        if (result) {
            std::println("查询成功: {} 字段", result->size());
        }
    } // pooled_connection 析构时自动归还

    std::println("池统计: size={}, idle={}, checked_out={}, waiters={}",
        pool.size(), pool.idle_count(), pool.checked_out_count(), pool.waiter_count());

    stop_src.request_stop();
    pool.close();
    ctx.stop();
}
```

**示例 — topology_connection_pool 多节点部署**:

```cpp
mg::topology_connection_pool_options topo_opts;
topo_opts.seeds = {
    {"mongo1.example.com", 27017},
    {"mongo2.example.com", 27017},
    {"mongo3.example.com", 27017}
};
topo_opts.replica_set_name = "rs0";
topo_opts.per_server_pool.connection.tls = true;
topo_opts.per_server_pool.connection.username = "app_user";
topo_opts.per_server_pool.connection.password = "secret";
topo_opts.per_server_pool.minimum_size = 2;
topo_opts.per_server_pool.maximum_size = 16;

mg::topology_connection_pool topo_pool(ctx, topo_opts);

// 启动拓扑监控（后台发现新节点、检测故障）
std::stop_source stop_src;
cn::spawn(ctx, topo_pool.run_monitoring(stop_src.get_token(),
    std::chrono::seconds{10}));

// 刷新初始拓扑
auto refresh_r = co_await topo_pool.refresh();
if (refresh_r) {
    auto stats = topo_pool.statistics();
    std::println("拓扑: {} 个节点池, {} 连接, {} 空闲",
        stats.server_pool_count, stats.connection_count, stats.idle_connection_count);
}

// 直接执行命令（自动选择 primary 节点）
mg::bson_document ping_cmd;
ping_cmd.append("ping", mg::bson_value{std::int32_t{1}});
auto ping_r = co_await topo_pool.command("admin", std::move(ping_cmd));
```

## 多核服务器部署

### server_context 模式

MongoDB 多核部署使用 `server_context` + 每 worker 独立连接池，或使用 `topology_connection_pool` 共享（需注意并发安全）。

```cpp
class server_context {
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    auto accept_io() noexcept -> io_context&;        // accept 专用 io_context
    auto next_worker_io() noexcept -> io_context&;   // round-robin 选择 worker
    auto worker_count() const noexcept -> unsigned;
    auto worker_ios() -> std::vector<io_context*>;    // 所有 worker io_context
    auto pool() noexcept -> thread_pool&;             // cnetmod CPU 线程池
    void spawn_next(task<void> t);                    // 在下一个 worker 上启动协程
    void run();                                       // 阻塞运行
    void stop();                                      // 停止所有线程
};
```

**MongoDB 多核部署示例**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.mongodb;

namespace cn = cnetmod;
namespace mg = cn::mongodb;

constexpr unsigned WORKER_THREADS = 4;

// 每个 worker 持有独立的连接池
struct mongo_worker {
    cn::io_context& io;
    std::unique_ptr<mg::connection_pool> pool;
};

auto handle_mongo_command(mg::connection_pool& pool) -> cn::task<void>
{
    auto conn_r = co_await pool.acquire();
    if (!conn_r) {
        std::println("获取连接失败: {}", conn_r.error().message);
        co_return;
    }

    mg::bson_document cmd;
    cmd.append("aggregate", mg::bson_value{std::string("events")});
    cmd.append("pipeline", mg::bson_value{mg::bson_array{
        mg::bson_document{{"$match", mg::bson_value{mg::bson_document{
            {"type", mg::bson_value{std::string("error")}}}}}},
        mg::bson_document{{"$limit", mg::bson_value{std::int32_t{10}}}}
    }});
    cmd.append("cursor", mg::bson_value{mg::bson_document{}});

    auto result = co_await conn_r->get().command("production_db", std::move(cmd));
    if (result)
        std::println("聚合查询成功");
    // pooled_connection 析构时自动归还
}

auto main() -> int
{
    cn::net_init net;

    // 1. 创建多核 server_context
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 2. 为每个 worker 创建独立连接池
    mg::connection_pool_options pool_opts;
    pool_opts.connection.host = "mongo.example.com";
    pool_opts.connection.database = "production_db";
    pool_opts.connection.username = "app_user";
    pool_opts.connection.password = "secret";
    pool_opts.connection.tls = true;
    pool_opts.minimum_size = 4;
    pool_opts.maximum_size = 32;

    std::vector<mongo_worker> workers;
    for (auto* io_ptr : sctx.worker_ios()) {
        auto pool = std::make_unique<mg::connection_pool>(*io_ptr, pool_opts);
        workers.push_back({*io_ptr, std::move(pool)});
    }

    // 3. 预热所有连接池 + 启动后台维护
    for (auto& w : workers) {
        cn::spawn(w.io, [&w]() -> cn::task<void> {
            co_await w.pool->warm_up();
            std::println("Worker 连接池预热完成: size={}", w.pool->size());
        });
        // 每个 worker 的维护协程
        cn::spawn(w.io, [&w]() -> cn::task<void> {
            std::stop_source stop;
            co_await w.pool->run_maintenance(stop.get_token());
        });
    }

    // 4. 接受连接，round-robin 分发
    cn::spawn(sctx.accept_io(), [&]() -> cn::task<void> {
        auto listener = cn::tcp_listener::create(sctx.accept_io());
        listener.bind("0.0.0.0", 9090);
        listener.listen(1024);

        std::atomic<std::size_t> next_idx{0};
        while (true) {
            auto [sock, addr] = co_await listener.accept();
            auto idx = next_idx.fetch_add(1, std::memory_order_relaxed) % workers.size();
            auto& w = workers[idx];
            cn::spawn(w.io, [&w]() -> cn::task<void> {
                co_await handle_mongo_command(*w.pool);
            });
        }
    }());

    // 5. 阻塞运行
    sctx.run();
    return 0;
}
```

## Do's & Don'ts

**Do**:
- 多节点环境优先使用 `topology_connection_pool`
- 生产环境启用 TLS (`opts.tls = true`)
- 使用 `retryable_operation_policy` 封装易失败命令
- 长时间操作设置 `command_timeout`
- 启动后台 `run_maintenance()` 自动清理空闲连接和健康检查
- 多核场景为每个 worker `io_context` 创建独立 `connection_pool`
- 使用 `topology_connection_pool::run_monitoring()` 自动发现副本集拓扑变化

**Don't**:
- 不要共享 `connection` 实例——线程不安全
- 不要忽略 `result` 错误检查
- 不要在 `change_stream` 中阻塞回调
- 不要手动拼接字段顺序——BSON 键值对不保证顺序敏感
- 不要在多 worker 场景共享同一个 `connection_pool` 实例

## 参考示例

- `examples/database/mongodb/mongodb_production_service.cpp` — 生产级架构
- `examples/http/multicore_http.cpp` — `server_context` 多核架构参考
- 更多示例参见 `examples/database/mongodb/` 目录
## Exhaust / `moreToCome` 连续响应

普通 `connection::command()` 只适用于一问一答；若服务端 OP_MSG 设置
`moreToCome`，它会返回协议错误，避免后续命令误读同一 socket 中残留的响应。
对于 exhaust cursor 等连续响应场景，使用 `command_stream()`：

```cpp
auto streamed = co_await conn.command_stream("analytics",
    bson_document{{"find", "events"}, {"filter", bson_document{}}},
    [](bson_document response) -> task<result<void>> {
        // 处理每一条 OP_MSG 响应；这里可 co_await 异步写入或业务处理。
        co_return result<void>{};
    });
```

流期间连接是独占的，不能并发执行其他命令。处理回调返回错误、网络错误或调用
`cancel_active_command()` 时，连接会被关闭并从连接池中淘汰；这是必要的，因为
尚未读取的连续响应不能安全地留给下一位租户。`command_stream()` 不使用普通命令
的整体超时，长流应通过 MongoDB 命令本身的 `maxTimeMS`、应用 deadline 和取消来
控制生命周期。
<!-- END SOURCE: skill/database/mongodb.md -->

<!-- BEGIN SOURCE: skill/database/mysql.md -->
# Source: `skill/database/mysql.md`

# MySQL 协议模块

## 停止后的传输释放

`client::close() noexcept` 不发送 COM_QUIT，也不等待网络，保留最近 I/O 错误并清空连接状态。
只能在所属执行线程、所有客户端操作均已结束后调用；正常协议退出仍使用 `quit()`。
连接池维护入口等待全部连接任务结束后关闭未借出的客户端；维护已结束后的重复
`cancel()` 也清理未借出连接。外部 lease 仍须在池销毁前归还。

> 高性能异步 MySQL 客户端，支持文本/二进制协议、连接池、管道、事务与 ORM 集成。

**import**: `import cnetmod.protocol.mysql;`
**CMake**: `-DCNETMOD_ENABLE_MYSQL=ON`
**源码**: `src/protocol/mysql/`

## 场景导航

| 场景 | 推荐入口 |
|------|----------|
| 简单查询 / DDL | `client::query` |
| 带参数 SQL | `client::execute` + `with_params` |
| 二进制协议 | `client::prepare` / `execute_stmt` |
| 批量命令 | `pipeline_request` / `run_pipeline` |
| 事务 | `client::transaction` |
| 连接池 | `connection_pool` |
| ORM 映射 | `orm::mysql_session`（见 [database-orm.md](database-orm.md)） |

## API 参考

### 类型系统 (`types`)

**枚举**:
- `field_type` — 协议字段类型（`tiny`, `long_type`, `varchar`, `json`, `blob` 等）
- `field_kind` — 客户端分类（`null`, `int64`, `uint64`, `string`, `float_`, `double_`, `date`, `datetime`, `time`）
- `column_type` — 语义列类型（`bigint`, `varchar`, `json`, `geometry` 等）

**值容器**:
```cpp
// field_value — 行字段值（类型安全访问）
auto kind() const noexcept -> field_kind;
auto as_int64() const -> std::int64_t;        // 类型不匹配抛 bad_field_access
auto as_string() const -> std::string_view;
static auto from_int64(std::int64_t) -> field_value;
static auto from_string(std::string) -> field_value;

// param_value — SQL 参数绑定
static auto null() -> param_value;
static auto from_int(std::int64_t) -> param_value;
static auto from_string(std::string) -> param_value;

// result_set — 查询结果
struct result_set {
    std::vector<column_meta> columns;
    std::vector<row> rows;
    std::uint64_t affected_rows{}, last_insert_id{};
    std::uint16_t warning_count{};
    auto ok() const noexcept -> bool;
    auto is_err() const noexcept -> bool;
    auto has_rows() const noexcept -> bool;
};
```

### 错误码 (`error_codes`)

```cpp
enum class client_errc : int { pool_not_running, no_connection_available, not_connected, ... };
enum class common_server_errc : int { er_dup_entry = 1062, er_parse_error = 1064, er_lock_deadlock = 1213, ... };
auto client_errc_to_str(client_errc) noexcept -> const char*;
auto is_fatal_error(client_errc) noexcept -> bool;
```

### 诊断信息 (`diagnostics`)

```cpp
class diagnostics {
    auto server_message() const noexcept -> std::string_view;
    auto client_message() const noexcept -> std::string_view;
    void clear() noexcept;
    auto empty() const noexcept -> bool;
};
enum class ssl_mode { disable, enable, require };
struct format_options { character_set charset = utf8mb4_charset; bool backslash_escapes = true; };
auto escape_string(std::string_view input, const format_options& opts,
    quoting_context ctx = quoting_context::single_quote) -> std::string;
```

### `format_sql` — SQL 格式化

**签名**:
```cpp
auto format_sql(const format_options& opts, std::string_view fmt,
    std::span<const param_value> args) -> std::expected<std::string, format_errc>;
auto with_params(std::string_view query, std::initializer_list<param_value> args) -> with_params_t;
```

### `client::connect`

**签名**: `auto connect(connect_options opts = {}) -> task<result_set>`

需要可取消的连接或心跳时，使用 `connect(connect_options, cancel_token&)`
与 `ping(cancel_token&)`。令牌必须存活到操作结束；不要并发操作同一个
client。取消会传递到连接和认证读写，`last_error()` 保留传输错误。
已经开始的系统 DNS 查询仍可能需要等解析返回，不能将它当成硬实时取消。

```cpp
struct connect_options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string username = "root", password, database, charset = "utf8mb4";
    ssl_mode ssl = ssl_mode::disable;
    bool multi_statements{};
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mysql;

namespace cn = cnetmod;
namespace mysql = cn::mysql;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    mysql::client db(ctx);
    mysql::connect_options opts;
    opts.host = "127.0.0.1";
    opts.username = "root";
    opts.password = "your_password";
    opts.database = "mydb";

    auto rs = co_await db.connect(std::move(opts));
    if (rs.is_err()) { ctx.stop(); co_return; }

    // 文本查询
    auto result = co_await db.query("SELECT id, name FROM users LIMIT 10");
    for (auto& row : result.rows)
        std::println("id={}, name={}", row[0].to_string(), row[1].to_string());

    // 带参数查询
    using P = mysql::param_value;
    auto rs2 = co_await db.execute(mysql::with_params(
        "SELECT * FROM users WHERE age > {} AND city = {}",
        {P::from_int(18), P::from_string("上海")}));

    // Prepared Statement（二进制协议）
    auto stmt_r = co_await db.prepare("SELECT * FROM users WHERE id = ?");
    if (stmt_r) {
        std::array<P, 1> params = {P::from_int(42)};
        co_await db.execute_stmt(*stmt_r, params);
        co_await db.close_stmt(*stmt_r);
    }
    co_await db.quit();
    ctx.stop();
}
```

### `client` 其他方法

**签名**:
```cpp
auto query(std::string_view sql) -> task<result_set>;
auto execute(std::string_view sql) -> task<result_set>;
auto execute(with_params_t wp) -> task<result_set>;
auto prepare(std::string_view sql) -> task<std::expected<statement, std::string>>;
auto execute_stmt(const statement& stmt, std::span<const param_value> params = {}) -> task<result_set>;
auto close_stmt(const statement& stmt) -> task<void>;
auto run_pipeline(const pipeline_request& req, std::vector<stage_response>& responses) -> task<void>;
auto ping() -> task<result_set>;
auto reset_connection() -> task<result_set>;
auto quit() -> task<void>;
auto is_open() const noexcept -> bool;
auto reconnect() -> task<result_set>;
auto secure_channel() const noexcept -> bool;
```

### `connection_pool` — 连接池

**签名**:
```cpp
struct pool_params {
    std::string host = "127.0.0.1";  std::uint16_t port = 3306;
    std::string username, password, database;
    ssl_mode ssl = ssl_mode::enable;
    std::size_t initial_size = 1, max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(20);
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
};
class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
};
class pooled_connection {
    auto valid() const noexcept -> bool;
    auto operator->() noexcept -> client*;
    void return_without_reset();
    // 析构时自动归还连接池
};
```

**示例**:
```cpp
mysql::pool_params params;
params.host = "127.0.0.1";
params.username = "root";
params.database = "mydb";
params.initial_size = 4;
params.max_size = 16;

mysql::connection_pool pool(ctx, params);
cn::spawn(ctx, pool.async_run());

auto conn_r = co_await pool.async_get_connection();
if (conn_r) {
    auto rs = co_await (*conn_r)->query("SELECT COUNT(*) FROM users");
    if (rs.has_rows())
        std::println("总数: {}", rs.rows[0][0].to_string());
} // pooled_connection 析构时自动归还
```

### `pipeline` — 管道批量执行

**签名**:
```cpp
class pipeline_request {
    auto add_execute(std::string) -> pipeline_request&;
    auto add_prepare(std::string) -> pipeline_request&;
    auto add_close_statement(std::uint32_t) -> pipeline_request&;
    auto add_reset_connection() -> pipeline_request&;
};
class stage_response {
    auto has_results() const noexcept -> bool;
    auto has_error() const noexcept -> bool;
    auto get_results() const noexcept -> const result_set&;
    auto error_msg() const noexcept -> std::string_view;
};
```

**示例**:
```cpp
mysql::pipeline_request req;
req.add_execute("SELECT COUNT(*) FROM users")
   .add_execute("UPDATE users SET last_login = NOW() WHERE id = 1");
std::vector<mysql::stage_response> responses;
co_await db.run_pipeline(req, responses);
```

### `transaction` — 事务管理

**签名**:
```cpp
class transaction_guard {
    auto commit() -> task<result_set>;
    auto rollback() -> task<result_set>;
    auto is_committed() const noexcept -> bool;
};
class transaction {
    static auto begin(client& cli) -> task<std::expected<transaction_guard, std::string>>;
    static auto begin(client& cli, isolation_level level)
        -> task<std::expected<transaction_guard, std::string>>;
    template <typename Func>
    static auto execute(client& cli, Func&& func) -> task<result_set>;
};
// client 便捷方法：
// auto transaction(Func&& func) -> task<result_set>;
// auto transaction(Func&& func, isolation_level level) -> task<result_set>;
```

**示例**:
```cpp
// lambda 自动提交/回滚
auto rs = co_await cli.transaction([&]() -> cn::task<void> {
    co_await cli.execute("INSERT INTO accounts (name, balance) VALUES ('Alice', 1000)");
    co_await cli.execute("UPDATE accounts SET balance = balance - 200 WHERE name = 'Alice'");
    co_return;
});

// 指定隔离级别
auto rs2 = co_await cli.transaction([&]() -> cn::task<void> {
    co_await cli.execute("SELECT * FROM accounts FOR UPDATE");
    co_return;
}, mysql::isolation_level::serializable);
```

### ORM 集成

MySQL ORM 通过 `cnetmod.protocol.mysql:orm` 导出，包含核心映射、XML Mapper、MyBatis-Plus 功能。详见 [database-orm.md](database-orm.md)。

```cpp
#include <cnetmod/orm.hpp>
struct User { std::int64_t id = 0; std::string name; double balance = 0.0; };
CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(balance, "balance", double_))

orm::mysql_session db(cli);
co_await db.create_table<User>();
User user{.name = "Alice", .balance = 1000.0};
auto rs = co_await db.insert(user);
```

## 连接池（生产级用法）

### Pool API

MySQL 协议提供两个层级的连接池：

**`connection_pool`** — 单 io_context 连接池：

```cpp
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string username, password, database;
    ssl_mode ssl = ssl_mode::enable;
    std::size_t initial_size = 1;           // 初始连接数
    std::size_t max_size = 16;              // 最大连接数
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(20);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);     // 等待连接超时
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);  // 重连间隔
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);      // 心跳间隔
    std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
    bool tls_verify = false;
    std::string tls_ca_file;
};

class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;                  // 启动池（必须先调用）
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>; // 非阻塞获取
    auto cancel() -> task<void>;                     // 关闭池
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto operator->() noexcept -> client*;
    void return_without_reset();   // 归还但不重置连接状态
    // 析构时自动归还连接池（需要 reset）
};
```

**`sharded_connection_pool`** — 多核分片连接池（每个 worker io_context 绑定独立分片）：

默认析构归还的打开连接先进入 resetting，由已有连接维护任务执行
`COM_RESET_CONNECTION`，成功后才进入空闲位图并交给等待者。reset 使用
`ping_timeout` 作为维护操作超时，支持停机取消；失败则进入 dead 等待重连。
`client::reset_connection(cancel_token&)` 提供对应的取消感知协议调用。
显式 `return_without_reset()` 保持不重置语义，可保留会话状态并立即再次借出。
默认归还增加的是之前遗漏的协议清理往返，不是 OTEL 开关带来的成本。
114 专用测试库已验证普通归还清除会话变量、不重置归还保留变量。本地协议对端
在收到 reset 后不回复，已验证 30ms 维护超时关闭旧会话，以及设置 10 秒超时
时主动停机仍能取消 reset 并等待维护任务退出。测试同时断言 reset 期间无空闲连接。
超时分支还完成新 TCP 连接的认证，验证等待者重新获得连接并成功 PING，池大小
保持 1，退出后无残留等待者。reset 响应只接受结构完整的 OK 或 ERR；未知响应头、
截断的长度编码或状态字段、畸形错误包会关闭连接并报告 protocol_error。
reset 回归使用独立 `test_mysql_pool` 目标，只要求启用 MYSQL，不依赖 HTTP 或 ORM；
Windows 已验证 HTTP=OFF、ORM=OFF 配置。TLS 及锁竞争归还的分配失败仍需单独验证，
不能据此认定连接池已完整验收。

锁竞争归还不再创建辅助协程：默认归还进入 resetting，不重置归还进入 returning，
由已有、可等待退出的连接维护任务获得元数据锁后通知 FIFO 等待者。非竞争的
不重置归还仍直接进入 idle。分配探针已覆盖扩容持锁期间的重入不重置归还：
不消耗嵌套分配失败探针，等待者最终取得连接，另一等待者取消后计数归零。
该定向测试复用 `test_http_disabled_overhead` 的测试专用分配器，不改变生产分配器；
同一探针也覆盖 optional 租约直接析构：析构不消耗失败探针，对端随后收到
COM_RESET_CONNECTION，返回成功后等待者获得连接。登录和 reset 读取处理 TCP
分片，避免将一次 read 当作完整报文。它不是跨线程竞争或 CPU 性能等价的完整证明。

`async_run()` 是完整生命周期任务：启动各分片，并在停止后等待已投递的分片
退出，最后传播错误。不能把 `co_await pool.async_run()` 放在业务请求之前
当作启动屏障。用 `when_all` 或应用监管器并发运行生命周期和业务任务。
`request_stop()` 可跨线程请求所有分片停止；`cancel()` 也只发出停止请求。
必须等待运行中的 `async_run()` 结束，再停事件循环；销毁池前仍须归还所有
借出的连接。异步归还与借出连接的完整生命周期安全尚未验收。

`connection_pool::checked_out_count()` 在所属执行线程扫描节点状态，报告尚未归还的
租约数；不在借还快速路径增加计数器。Application 的 `mysql_service::stop()` 请求池
停止后等待租约归还，并遵守阶段取消令牌和截止时间。到期仍有租约则失败，保留服务登记，
归还后可重试关闭，不能再把仅停止维护任务当作服务已释放。114 隔离库验证了持有租约时
30ms 关闭超时、归还后再次关闭成功；这不是任意逃逸引用可安全析构的保证。

```cpp
class sharded_connection_pool {
    // 单 io_context + 指定分片数
    sharded_connection_pool(io_context& ctx, pool_params params,
        std::size_t num_shards = 4);
    // 多 worker io_context，每个 worker 一个分片
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params);
    // 多 worker io_context + 指定分片数
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params, std::size_t num_shards);

    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;
};
```

**示例 — 生产级分片连接池**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.mysql;

namespace cn = cnetmod;
namespace mysql = cn::mysql;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    mysql::pool_params params;
    params.host = "db.example.com";
    params.username = "app_user";
    params.password = "secret";
    params.database = "production_db";
    params.ssl = mysql::ssl_mode::require;
    params.initial_size = 8;
    params.max_size = 64;
    params.ping_interval = std::chrono::minutes(30);

    // 4 个分片共用此事件循环；多线程时传入各 worker 的 io_context。
    mysql::sharded_connection_pool pool(ctx, params, 4);
    auto workload = [&]() -> cn::task<void> {
        struct stop_on_exit {
            mysql::sharded_connection_pool& pool;
            ~stop_on_exit() { pool.request_stop(); }
        } stop{pool};

        // 限定 lease 作用域，确保停止前归还。
        if (auto connection = co_await pool.async_get_connection(); connection) {
            auto result = co_await (*connection)->query("SELECT COUNT(*) FROM orders");
            if (result.is_err())
                co_return;
        }

        if (auto connection = co_await pool.async_get_connection(ctx); connection) {
            auto result = co_await (*connection)->execute(
                mysql::with_params("UPDATE orders SET status = {} WHERE id = {}",
                    {mysql::param_value::from_string("shipped"),
                     mysql::param_value::from_int(1024)}));
            if (result.is_err())
                co_return;
        }

    };
    co_await cn::when_all(pool.async_run(), workload());
    ctx.stop();
}
```

## 多核服务器部署

### server_context 模式

`server_context` 提供多核部署架构：

- **1 个 accept 线程** — 专职接受新连接（`accept_io()`）
- **N 个 worker 线程** — round-robin 处理业务请求（`next_worker_io()`）
- **M 个 CPU 工作线程** — 通过 cnetmod 线程池卸载 CPU 密集操作（`pool()`）

```cpp
class server_context {
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    auto accept_io() noexcept -> io_context&;        // accept 专用 io_context
    auto next_worker_io() noexcept -> io_context&;   // round-robin 选择 worker
    auto worker_count() const noexcept -> unsigned;
    auto worker_ios() -> std::vector<io_context*>;    // 所有 worker io_context
    auto pool() noexcept -> thread_pool&;             // cnetmod CPU 线程池
    void spawn_next(task<void> t);                    // 在下一个 worker 上启动协程
    void run();                                       // 阻塞运行
    void stop();                                      // 停止所有线程
};
```

**MySQL 多核部署示例**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.mysql;
import cnetmod.protocol.http;

namespace cn = cnetmod;
namespace mysql = cn::mysql;

constexpr unsigned WORKER_THREADS = 4;

auto handle_query(mysql::sharded_connection_pool& pool,
                  cn::io_context& worker_io) -> cn::task<void>
{
    // 绑定到当前 worker 的分片获取连接
    auto conn_r = co_await pool.async_get_connection(worker_io);
    if (!conn_r) co_return;

    auto rs = co_await (*conn_r)->query("SELECT id, name, balance FROM accounts LIMIT 100");
    for (auto& row : rs.rows)
        std::println("id={}, name={}, balance={}",
            row[0].to_string(), row[1].to_string(), row[2].to_string());
}

auto main() -> int
{
    cn::net_init net;

    // 1. 创建多核 server_context：4 worker + 4 pool 线程
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 2. 为每个 worker 创建分片连接池
    mysql::pool_params params;
    params.host = "db.example.com";
    params.username = "app_user";
    params.password = "secret";
    params.database = "production_db";
    params.ssl = mysql::ssl_mode::require;
    params.initial_size = WORKER_THREADS * 4;   // 每个 worker 4 个初始连接
    params.max_size = WORKER_THREADS * 16;      // 每个 worker 最多 16 个连接

    mysql::sharded_connection_pool pool(
        sctx.worker_ios(), params);

    // 3. 在 accept_io 上启动连接池
    cn::spawn(sctx.accept_io(), pool.async_run());

    // 4. 接受 TCP 连接，round-robin 分发到 worker
    cn::spawn(sctx.accept_io(), [&]() -> cn::task<void> {
        auto listener = cn::tcp_listener::create(sctx.accept_io());
        listener.bind("0.0.0.0", 9090);
        listener.listen(1024);

        while (true) {
            auto [sock, addr] = co_await listener.accept();
            auto& worker = sctx.next_worker_io();  // round-robin
            cn::spawn(worker, [&pool, &worker]() -> cn::task<void> {
                co_await handle_query(pool, worker);
            });
        }
    }());

    // 5. 阻塞运行（accept 线程 + worker 线程）
    sctx.run();
    return 0;
}
```

## Do's & Don'ts

**Do**:
- 生产环境使用 `connection_pool` 而非裸 `client`
- 参数化查询使用 `with_params` 或 `prepare`/`execute_stmt`
- 使用 `ssl_mode::require` 保护敏感数据传输
- 对大结果集使用 `start_execution` + `read_some_rows` 流式读取
- 多核场景使用 `sharded_connection_pool` + `server_context`，每个 worker 绑定独立分片
- 通过 `async_get_connection(io_context&)` 绑定到当前 worker，避免跨线程 IO

**Don't**:
- 不要在事务 lambda 中静默忽略异常——异常会触发自动回滚
- 不要跨协程共享同一个 `client` 实例（非线程安全）
- 不要在 `with_params` 中混合手动与自动格式化
- 不要在多 worker 场景使用单 `connection_pool`——应使用 `sharded_connection_pool`

## 参考示例

- `examples/database/mysql/mysql_crud.cpp` — 完整 CRUD、Prepared Statement、Pipeline
- `examples/database/mysql/mysql_orm.cpp` — ORM 模型映射与 CRUD
- `examples/database/mysql/mysql_transaction.cpp` — 事务与隔离级别
- `examples/database/mysql/mysql_mybatis_plus_demo.cpp` — MyBatis-Plus 风格查询
- `examples/http/multicore_http.cpp` — `server_context` 多核架构参考
<!-- END SOURCE: skill/database/mysql.md -->

<!-- BEGIN SOURCE: skill/database/postgresql.md -->
# Source: `skill/database/postgresql.md`

# PostgreSQL 协议模块

> 异步 PostgreSQL 客户端，支持 SCRAM-SHA-256/MD5 认证、TLS、参数化查询、COPY 流式导入导出、连接池与 ORM 集成。

**import**: `import cnetmod.protocol.postgresql;`
**CMake**: `-DCNETMOD_ENABLE_POSTGRESQL=ON`
**源码**: `src/protocol/postgresql/`

> **命名空间别名**: `namespace pgsql = cnetmod::postgresql;`

## 场景导航

| 场景 | 推荐入口 |
|------|----------|
| 简单查询 | `client::query` |
| 参数化查询 | `client::execute(parameterized_query)` |
| Prepared Statement | `client::prepare` / `execute(prepared_statement)` |
| 事务 | `client::transaction` |
| COPY 导入/导出 | `client::copy_from` / `copy_to` |
| 大批量流式读取 | `client::query_batches` |
| 连接池 | `connection_pool` |
| ORM 映射 | `orm::postgresql_session`（见 [database-orm.md](database-orm.md)） |

## API 参考

### 类型系统 (`query_result`)

复用 `cnetmod.database` 共享类型：

```cpp
using result_set = database::query_result;
using row = database::row;
using field_value = database::field_value;
using column_meta = database::column_metadata;
using param_value = database::query_parameter;
using format_options = database::sql_format_options;
using isolation_level = database::isolation_level;
using parameterized_query = database::parameterized_query;
using database::with_params;

struct prepared_statement { std::string name, sql; std::size_t parameter_count{}; auto valid() const noexcept -> bool; };
```

### 连接选项 (`connection_options`)

**签名**:
```cpp
enum class tls_mode : std::uint8_t { disable, prefer, require, verify_ca, verify_full };
struct connection_options {
    std::string host = "localhost";  std::uint16_t port = 5432;
    std::string username = "postgres", password, database = "postgres";
    std::string application_name = "cnetmod";
    tls_mode tls = tls_mode::prefer;
    std::string tls_ca_file, tls_cert_file, tls_key_file;
    std::chrono::milliseconds connect_timeout{10000};
    std::size_t maximum_connect_attempts = 3;
    std::unordered_map<std::string, std::string> startup_parameters;
};
```

### `client` — 异步连接客户端

**签名**:
```cpp
class client {
    explicit client(io_context&);
    auto connect(connection_options options = {}) -> task<result_set>;
    auto connect(connection_options options, cancel_token& cancellation) -> task<result_set>;
    auto query(std::string_view sql) -> task<result_set>;
    auto query(std::string_view sql, cancel_token& cancellation) -> task<result_set>;
    auto execute(parameterized_query parameters) -> task<result_set>;
    auto prepare(std::string_view sql, std::string name = {})
        -> task<std::expected<prepared_statement, std::string>>;
    auto execute(const prepared_statement&, std::span<const param_value> = {})
        -> task<result_set>;
    auto copy_from(std::string_view copy_sql, copy_data_source source) -> task<result_set>;
    auto query_batches(std::string_view sql, std::size_t batch_size,
        std::function<task<void>(std::span<const row>)> consume) -> task<result_set>;
    auto terminate() -> task<void>;
    auto terminate(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>;
    auto is_open() const noexcept -> bool;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.postgresql;

namespace cn = cnetmod;
namespace pg = cn::postgresql;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    pg::client db(ctx);
    pg::connection_options opts;
    opts.host = "127.0.0.1";
    opts.username = "postgres";
    opts.password = "your_password";
    opts.database = "mydb";
    opts.tls = pg::tls_mode::prefer;

    auto rs = co_await db.connect(std::move(opts));
    if (rs.is_err()) { ctx.stop(); co_return; }

    // 简单查询
    auto result = co_await db.query("SELECT id, name FROM users LIMIT 10");
    for (auto& row : result.rows)
        std::println("id={}, name={}", row[0].to_string(), row[1].to_string());

    // 参数化查询（$1, $2 ... 风格）
    auto rs2 = co_await db.execute(cn::database::with_params(
        "SELECT * FROM users WHERE age > $1 AND city = $2",
        {pg::param_value{18}, pg::param_value{std::string("北京")}}));

    // Prepared Statement
    auto stmt_r = co_await db.prepare("SELECT * FROM users WHERE id = $1");
    if (stmt_r) {
        std::array<pg::param_value, 1> params = {pg::param_value{42}};
        co_await db.execute(*stmt_r, params);
        co_await db.close_statement(*stmt_r);
    }
    co_await db.terminate();
    ctx.stop();
}
```

### 事务支持

**签名**:
```cpp
template <typename Function>
auto transaction(Function&& function) -> task<result_set>;

template <typename Function>
auto transaction(Function&& function, isolation_level level) -> task<result_set>;
```

**示例**:
```cpp
auto rs = co_await db.transaction([&]() -> cn::task<void> {
    co_await db.execute("INSERT INTO accounts (name, balance) VALUES ('Alice', 1000)");
    co_await db.execute("UPDATE accounts SET balance = balance - 200 WHERE name = 'Alice'");
});
```

### COPY 流式导入导出

**签名**:
```cpp
using copy_data_source = std::function<task<std::optional<std::vector<std::uint8_t>>>()>;
using copy_data_sink = std::function<task<void>(std::span<const std::uint8_t>)>;

auto copy_from(std::string_view copy_sql, copy_data_source source) -> task<result_set>;
auto copy_to(std::string_view copy_sql, copy_data_sink sink) -> task<result_set>;
```

**示例**:
```cpp
// COPY TO — 流式导出数据到回调
co_await db.copy_to("COPY users TO STDOUT WITH (FORMAT csv)",
    [](std::span<const std::uint8_t> chunk) -> cn::task<void> {
        std::println("收到 {} 字节", chunk.size());
        co_return;
    });
```

### `query_batches` — 流式分批读取

**签名**:
```cpp
auto query_batches(std::string_view sql, std::size_t batch_size,
    std::function<task<void>(std::span<const row>)> consume) -> task<result_set>;
```

**示例**:
```cpp
// 每次最多保留 1000 行，回调提供背压
co_await db.query_batches("SELECT * FROM large_table", 1000,
    [](std::span<const pg::row> batch) -> cn::task<void> {
        for (auto& row : batch)
            process_row(row);
        co_return;
    });
```

### `connection_pool` — 连接池

**签名**:
```cpp
struct connection_pool_options {
    connection_options connection;
    std::size_t minimum_connections = 1;
    std::size_t maximum_connections = 16;
    std::chrono::milliseconds acquire_timeout{5000};
};

class connection_pool {
    connection_pool(io_context&, connection_pool_options);
    auto warm_up() -> task<result_set>;
    auto warm_up(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>;
    auto acquire() -> task<std::expected<pooled_connection, std::error_code>>;
    auto acquire(cancel_token& cancellation)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto close() -> task<void>;
    auto close(cancel_token& cancellation) -> task<std::expected<void, std::error_code>>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto checked_out_count() const noexcept -> std::size_t;
    auto background_error() const noexcept -> std::error_code;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto operator->() noexcept -> client*;
    auto get() noexcept -> client&;
    void discard() noexcept;
    // 析构时自动归还连接池
};
```

**示例**:
```cpp
pg::connection_pool_options opts;
opts.connection.host = "127.0.0.1";
opts.connection.database = "mydb";
opts.minimum_connections = 2;
opts.maximum_connections = 16;

pg::connection_pool pool(ctx, opts);
co_await pool.warm_up();

auto conn_r = co_await pool.acquire();
if (conn_r) {
    auto rs = co_await (*conn_r)->query("SELECT COUNT(*) FROM users");
} // pooled_connection 析构时自动归还
```

可取消 terminate 拒绝重叠操作；预取消保留会话，便于之后重试。开始关闭后，同一 token
传到 PostgreSQL Terminate 写入以及启用 SSL 时的 async_shutdown。传输错误断开连接并
保留 error_code。普通无参 terminate 未改变。当前回归验证明文协议终止报文与预取消，
池的 close(cancellation) 与 Application stop 已接入这一入口。可取消关闭等待关闭锁、
重连任务和租约时检查取消，超时后池仍持有槽位与通知；调用方必须保持池存活，归还租约并重试。
Application 使用停止上下文的 deadline 映射 timed_out；从未启动且无资源的 stop 仍幂等成功。
当前服务回归验证持有租约超过 30ms 截止时间后返回超时、连接保留、归还后再次停止成功。
这不代表任意失控业务协程或传输阻塞均已完成有界退出验证。

### 认证机制

`client::reconnect()` 使用保存的连接配置，并由 `connect()` 统一取得操作所有权后
关闭旧连接。在所属 executor 上与尚未完成的操作重叠调用时，重连返回错误，
不会提前断开原操作的传输连接。认证期间的传输失败在清理后仍保留于
`last_error()`；这不表示所有认证错误都有网络错误码。

`query(sql, cancellation)` 将 token 显式传给该次查询的网络读写，包括 TLS 接口。
调用前已取消时不发送 SQL，保留会话；进行中的读写被取消时断开会话，必须重连后
才能复用。重叠调用在网络操作之前被拒绝，token 与 SQL 存储必须活到任务完成。
内部使用编译期传输选择：普通入口采用 `std::nullptr_t` 实例，可取消入口采用
`cancel_token*` 实例；客户端没有共享 token 槽位，普通读写不检查运行时取消指针。
这是传输取消，不是 `cancel_current_operation()` 的 PostgreSQL CancelRequest。
`connect(options, cancellation)` 也采用编译期分流，将同一 token 传到 Happy Eyeballs、
重试定时器、TLS 握手与认证读写。token 必须活到连接任务完成。
当前本地回归验证明文查询和认证等待取消；真实 TLS、DNS 阻塞、中途写入取消及
重试等待取消仍需独立验证，连接池/Application 尚未全面接入这些重载。

Application 的 PostgreSQL 健康探测使用同一个 deadline 获取连接并执行 `SELECT 1`，
不再仅凭池大小报告 up。失败连接被标记 discard；下次探测可重新建连。
健康报告使用固定诊断文本和错误码，不转发服务器 SQL 错误详情。当前本地测试覆盖
探测超时后丢弃连接、下一次探测重连成功。Application 启动使用可取消预热与同一个
截止时间；预热临时持有连接租约直到达到 minimum_connections，随后归还池。
可取消预热失败时，会在返回前关闭本次持有的连接（包括复用的空闲连接），不影响其他
调用方已借出的连接。槽位保留为可重试状态，池不进入关闭状态；`size()` 仍统计槽位，
不能用它推断活连接数。生命周期回归覆盖第二条连接认证超时、第一条连接自动关闭、
原始启动错误保留，以及同一服务再次启动成功并最终停止。该回归使用明文 TCP 模拟对端，
不是完整 PostgreSQL 服务器。后台重连监管仍需完善。

丢弃槽位的后台重连由池延迟创建的 `task_group` 持有，每个任务使用独立取消 token。
`close()` 先禁止新任务、取消并等待重连结束，再清理连接。正常租借路径不创建任务组。
`background_error()` 在所属 executor 上无分配地读取重连派发错误，或已完成任务组的错误；
派发错误优先返回。没有派发错误且任务仍运行或尚无任务组时返回空错误，不能将其
当作连通性判断。Application 的 PostgreSQL probe 在发起查询前检查此结果并以固定
诊断文本报告 down。当前接口不提供跨线程池操作保证。
已启动服务再次 `start()` 时，若存在派发错误或已完成后台错误，会重新进入可取消预热，而不是
直接成功返回。预热在任务组不存在或已经完成时处理已记录的派发错误或任务组错误，
清除失败状态并将未借出、已丢弃槽位恢复为可重试；
不会清除仍运行任务的状态。重新调度不代表健康已恢复，仍需查询探测和健康状态确认。
回归分别注入任务组创建前和已排队重连开始时的分配失败，验证 probe 报告 down、
再次 start 后原等待者获得替代连接、错误清除和最终 close。对端为明文认证模拟服务，
另有认证期间对端断开的回归：普通 connect 错误也会使重连任务失败，优先保留
客户端 `last_error()`，没有传输错误码时回退为 `io_error`，不再把连接失败报告为成功。
不证明真实 SQL 健康确认、恢复预算或完整 host 生命周期。调用方仍必须在销毁池前
等待 `close()`；已借出的连接和跨线程等待者仍需独立验证，不能理解为所有池后台路径均已闭环。

模块内置 **SCRAM-SHA-256**（推荐）、**MD5**（兼容旧版）、**Trust** 认证，在 `connect()` 阶段自动处理。TLS 协商在认证前完成。

### ORM 集成 (`orm::postgresql_session`)

**签名**（关键方法）:
```cpp
class postgresql_session {
    explicit postgresql_session(client& connection) noexcept;
    template <Model T> auto create_table() -> task<result_set>;
    template <Model T> auto find_all() -> task<postgresql_orm_result<T>>;
    template <Model T> auto find_by_id(param_value id) -> task<postgresql_orm_result<T>>;
    template <Model T> auto insert(T& model) -> task<postgresql_orm_result<T>>;
    template <Model T> auto insert_or_get(T& model, std::string_view unique_column)
        -> task<postgresql_orm_result<T>>;
    template <Model T> auto update(const T& model) -> task<postgresql_orm_result<T>>;
    template <Model T> auto remove(const T& model) -> task<postgresql_orm_result<T>>;
    template <Model T> auto find(const query_wrapper<T>& qb) -> task<postgresql_orm_result<T>>;
    template <Function> auto transaction(Function fn) -> task<result_set>;
};
template <class T> struct postgresql_orm_result {
    std::vector<T> data;  std::string error_msg, sql_state;
    auto ok() const noexcept -> bool;
    auto first() const -> std::optional<T>;
};
```

**示例**:
```cpp
#include <cnetmod/orm.hpp>
struct User { std::int64_t id = 0; std::string name; std::string email; };
CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar))

orm::postgresql_session db(pg_client);
co_await db.create_table<User>();
User user{.name = "Alice", .email = "alice@example.com"};
auto rs = co_await db.insert(user); // RETURNING * 自动回填 id
```

连接租约的普通归还保持同步快速路径；状态锁竞争时，归还通知存放在池的稳定槽位中，
通过所属 io_context 投递，不再创建 `release_async` 脱离协程。通知处理前槽位仍保持
已借出状态，因此 `close()` 不会提前移除它。锁仍被占用时通知留待下一次事件循环处理。
每个槽位增加固定通知存储；这不是历史性能无损的测量证明。取消清理使用等待者帧内的
通知，不创建脱离协程，取消回调只取得唤醒权并投递通知。close 会等待已登记等待者
完成清理，不能因等待队列已清空而提前返回。调用方必须保持池存活并等待 close，
不能提前停止事件循环。等待队列通过 cancel_token 的 register_callback、complete_callback、
finish_callback 同步登记、完成与取消，不直接读写旧平台回调字段。当前回归覆盖登记完成后
由另一线程发起取消、通知尚未执行时开始 close；同时登记/取消压力与持续锁竞争公平性仍需验证。

## 连接池（生产级用法）

### Pool API

PostgreSQL 连接池基于 `acquire()` 模式，支持预热和优雅关闭：

```cpp
struct connection_pool_options {
    connection_options connection;                    // 连接参数（host/port/auth/tls 等）
    std::size_t minimum_connections = 1;             // 最小连接数（保持热连接）
    std::size_t maximum_connections = 16;             // 最大连接数
    std::chrono::milliseconds acquire_timeout{5000};  // 获取连接超时
};

class connection_pool {
    connection_pool(io_context&, connection_pool_options);
    auto warm_up() -> task<result_set>;              // 预热：建立最小连接数
    auto acquire() -> task<std::expected<pooled_connection, std::error_code>>;
    auto acquire(cancel_token& cancellation)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto close() -> task<void>;                      // 优雅关闭
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto checked_out_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
    auto background_error() const noexcept -> std::error_code;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto operator->() noexcept -> client*;
    auto get() noexcept -> client&;
    void discard() noexcept;      // 标记连接为废弃（状态异常时使用）
    // 析构时自动归还连接池
};
```

**示例 — 生产级连接池配置**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.postgresql;

namespace cn = cnetmod;
namespace pg = cn::postgresql;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    pg::connection_pool_options opts;
    opts.connection.host = "pg.example.com";
    opts.connection.username = "app_user";
    opts.connection.password = "secret";
    opts.connection.database = "production_db";
    opts.connection.tls = pg::tls_mode::require;
    opts.connection.connect_timeout = std::chrono::milliseconds(5000);
    opts.minimum_connections = 4;
    opts.maximum_connections = 32;
    opts.acquire_timeout = std::chrono::milliseconds(3000);

    pg::connection_pool pool(ctx, opts);

    // 预热：提前建立 minimum_connections 个连接
    auto warmup_rs = co_await pool.warm_up();
    std::println("预热完成，池大小: {}, 空闲: {}", pool.size(), pool.idle_count());

    // 获取连接（RAII 自动归还）
    auto conn_r = co_await pool.acquire();
    if (conn_r) {
        auto& conn = conn_r->get();

        // 参数化查询
        auto rs = co_await conn.execute(cn::database::with_params(
            "SELECT id, name, email FROM users WHERE created_at > $1 LIMIT 100",
            {pg::param_value{std::string("2024-01-01")}}));

        for (auto& row : rs.rows)
            std::println("id={}, name={}", row[0].to_string(), row[1].to_string());

        // 如果连接状态异常，调用 discard() 通知池丢弃此连接
        if (!conn.is_open())
            conn_r->discard();
    }

    std::println("池统计: size={}, idle={}, checked_out={}, waiters={}",
        pool.size(), pool.idle_count(), pool.checked_out_count(), pool.waiter_count());

    co_await pool.close();
    ctx.stop();
}
```

## 多核服务器部署

### server_context 模式

PostgreSQL 模块没有内置分片池，多核部署方案为：**每个 worker io_context 持有独立的 `connection_pool`**。

```cpp
class server_context {
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    auto accept_io() noexcept -> io_context&;        // accept 专用 io_context
    auto next_worker_io() noexcept -> io_context&;   // round-robin 选择 worker
    auto worker_count() const noexcept -> unsigned;
    auto worker_ios() -> std::vector<io_context*>;    // 所有 worker io_context
    auto pool() noexcept -> thread_pool&;             // cnetmod CPU 线程池
    void spawn_next(task<void> t);                    // 在下一个 worker 上启动协程
    void run();                                       // 阻塞运行
    void stop();                                      // 停止所有线程
};
```

**PostgreSQL 多核部署示例**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.postgresql;

namespace cn = cnetmod;
namespace pg = cn::postgresql;

constexpr unsigned WORKER_THREADS = 4;

// 每个 worker 持有独立的连接池
struct worker_state {
    cn::io_context& io;
    std::unique_ptr<pg::connection_pool> pool;
};

auto handle_request(pg::connection_pool& pool) -> cn::task<void>
{
    auto conn_r = co_await pool.acquire();
    if (!conn_r) co_return;

    auto rs = co_await conn_r->get().query(
        "SELECT id, name, balance FROM accounts ORDER BY id LIMIT 50");
    for (auto& row : rs.rows)
        std::println("account: id={}, name={}, balance={}",
            row[0].to_string(), row[1].to_string(), row[2].to_string());
    // pooled_connection 析构时自动归还
}

auto main() -> int
{
    cn::net_init net;

    // 1. 创建多核 server_context
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 2. 为每个 worker 创建独立连接池
    pg::connection_pool_options pool_opts;
    pool_opts.connection.host = "pg.example.com";
    pool_opts.connection.username = "app_user";
    pool_opts.connection.password = "secret";
    pool_opts.connection.database = "production_db";
    pool_opts.connection.tls = pg::tls_mode::require;
    pool_opts.minimum_connections = 4;
    pool_opts.maximum_connections = 16;

    std::vector<worker_state> workers;
    for (auto* io_ptr : sctx.worker_ios()) {
        auto pool = std::make_unique<pg::connection_pool>(*io_ptr, pool_opts);
        workers.push_back({*io_ptr, std::move(pool)});
    }

    // 3. 预热所有连接池
    cn::spawn(sctx.accept_io(), [&]() -> cn::task<void> {
        for (auto& w : workers) {
            cn::spawn(w.io, [&w]() -> cn::task<void> {
                co_await w.pool->warm_up();
                std::println("Worker 连接池预热完成: size={}", w.pool->size());
            });
        }
        co_return;
    }());

    // 4. 接受连接，round-robin 分发到 worker
    cn::spawn(sctx.accept_io(), [&]() -> cn::task<void> {
        auto listener = cn::tcp_listener::create(sctx.accept_io());
        listener.bind("0.0.0.0", 9090);
        listener.listen(1024);

        std::atomic<std::size_t> next_worker{0};
        while (true) {
            auto [sock, addr] = co_await listener.accept();
            auto idx = next_worker.fetch_add(1, std::memory_order_relaxed) % workers.size();
            auto& w = workers[idx];
            cn::spawn(w.io, [&w]() -> cn::task<void> {
                co_await handle_request(*w.pool);
            });
        }
    }());

    // 5. 阻塞运行
    sctx.run();
    return 0;
}
```

## Do's & Don'ts

**Do**:
- 生产环境使用 `tls_mode::require` 或 `verify_full`
- 参数化查询使用 `$1, $2, ...` 占位符（PostgreSQL 风格，非 `?`）
- 大批量数据使用 `copy_from`/`copy_to` 而非逐行 INSERT
- 大结果集使用 `query_batches` 实现背压控制
- 启动时调用 `warm_up()` 预热连接池，避免首批请求延迟
- 多核场景为每个 worker `io_context` 创建独立 `connection_pool`
- 连接状态异常时调用 `discard()` 通知池丢弃连接

**Don't**:
- 不要在 SQL 中使用 `?` 占位符——PostgreSQL 使用 `$N` 编号参数
- 不要忽略 `discard()` 标记——连接状态异常时用它通知池丢弃连接
- 不要长时间持有 `pooled_connection`——及时归还以维持池吞吐量
- 不要在多 worker 场景共享同一个 `connection_pool`——每个 worker 应持有独立池

## 参考示例

- `examples/database/postgresql/postgresql_production_service.cpp` — 生产级服务架构
- `examples/http/multicore_http.cpp` — `server_context` 多核架构参考
- 更多示例参见 `examples/database/postgresql/` 目录
<!-- END SOURCE: skill/database/postgresql.md -->

<!-- BEGIN SOURCE: skill/database/redis.md -->
# Source: `skill/database/redis.md`

# Redis

> 异步 Redis 客户端，支持 RESP3 协议、连接池、分片池、集群路由及 Pipeline。

**import**: `import cnetmod.protocol.redis;`
**CMake**: `-DCNETMOD_ENABLE_REDIS=ON`
**源码**: `src/protocol/redis/`

## 场景导航

| 场景 | 推荐入口 |
|------|----------|
| 简单命令（GET/SET/HSET 等） | `client::cmd` |
| Pipeline 批量命令 | `client::pipe` |
| 请求构建器（复杂/流水线） | `request` + `client::exec` |
| 连接池 | `connection_pool` |
| Spring 风格值/Hash/Set 操作 | `redis_template` |
| 多核分片连接池 | `sharded_connection_pool` |
| 集群路由（MOVED/ASK） | `cluster_client` |
| Pub/Sub | `client::subscribe` / `client::psubscribe` |

## API 参考

### Redis Value 类型 (`resp3_node`)

RESP3 协议类型枚举：

```cpp
enum class resp3_type {
    array, push, set, map, attribute,
    simple_string, simple_error, number, doublean, boolean, big_number,
    null, blob_error, verbatim_string, blob_string, streamed_string_part, invalid
};

auto to_code(resp3_type type) noexcept -> char;
auto to_type(char code) noexcept -> resp3_type;
auto is_aggregate(resp3_type type) noexcept -> bool;
auto type_name(resp3_type type) noexcept -> std::string_view;
```

**`resp3_node`** — 解析后的 RESP3 节点：

```cpp
struct resp3_node {
    resp3_type data_type = resp3_type::invalid;
    std::size_t aggregate_size = 0;
    std::size_t depth = 0;
    std::string value;
    auto is_error() const noexcept -> bool;
    auto is_null() const noexcept -> bool;
    auto is_aggregate() const noexcept -> bool;
    auto as_integer() const noexcept -> std::int64_t;
    auto as_double() const noexcept -> double;
    auto as_bool() const noexcept -> bool;
    auto to_string() const -> std::string;
};
```

**辅助函数**:
```cpp
auto first_value(const std::vector<resp3_node>& nodes) noexcept -> std::string_view;
auto all_values(const std::vector<resp3_node>& nodes) -> std::vector<std::string_view>;
auto is_ok(const std::vector<resp3_node>& nodes) noexcept -> bool;
auto has_error(const std::vector<resp3_node>& nodes) noexcept -> bool;
auto error_message(const std::vector<resp3_node>& nodes) noexcept -> std::string_view;
```

### `redis_errc` — 错误码

```cpp
enum class redis_errc {
    success = 0, invalid_data_type, not_a_number, exceeds_max_nested_depth,
    unexpected_bool_value, empty_field, incompatible_size, not_a_double,
    resp3_simple_error, resp3_blob_error, resp3_null,
    not_connected, resolve_timeout, connect_timeout, pong_timeout,
    ssl_handshake_timeout, unknown_error
};
```

### `request` — 请求构建器

支持单命令、Pipeline 多命令、range 批量参数。

```cpp
class request {
    request() = default;

    auto push(std::span<const std::string> arguments) -> bool;

    /// 追加命令（可变参数）
    template <class... Ts> void push(std::string_view cmd, Ts const&... args);

    /// 追加命令：cmd key [range elements...]
    template <class ForwardIterator>
    void push_range(std::string_view cmd, std::string_view key,
        ForwardIterator begin, ForwardIterator end);

    /// 追加命令：cmd key [range container]
    template <class Range>
    void push_range(std::string_view cmd, std::string_view key, const Range& range);

    /// 追加键值对范围：cmd key [k1 v1 k2 v2 ...]
    template <class ForwardIterator>
    void push_range_pairs(std::string_view cmd, std::string_view key,
        ForwardIterator begin, ForwardIterator end);

    auto payload() const noexcept -> std::string_view;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    void clear();
    void reserve(std::size_t n);
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.redis;

using cn::redis::request;

request req;
req.push("SET", "key", "value");
req.push("GET", "key");
req.push("HSET", "hash", "field1", 100, "field2", 200);

// Pipeline 多命令
request multi;
multi.push("SET", "a", "alpha");
multi.push("SET", "b", "beta");
multi.push("MGET", "a", "b");
```

### `resp3_parser` — RESP 解析器

```cpp
class resp3_parser {
    static constexpr std::size_t max_embedded_depth = 5;
    resp3_parser();
    auto consume(std::string_view data, std::error_code& ec) -> std::optional<resp3_node>;
    auto done() const noexcept -> bool;
    auto consumed() const noexcept -> std::size_t;
    auto is_parsing() const noexcept -> bool;
    void reset();
};

auto parse_response(std::string_view data, std::size_t expected_responses = 1)
    -> std::expected<std::vector<resp3_node>, std::error_code>;
```

### `connect_options` — 连接配置

```cpp
struct connect_options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password;
    std::string username;
    std::uint32_t db = 0;
    bool resp3 = true;
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
};
```

### `client` — 异步 Redis 客户端

```cpp
class client {
    explicit client(io_context& ctx) noexcept;
    auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>>;
    auto is_open() const noexcept -> bool;
    auto is_reusable() const noexcept -> bool;
    void close() noexcept;

    /// 执行 request 构建器（支持 pipeline）
    auto exec(const request& req) -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// 快捷单命令
    auto cmd(std::initializer_list<std::string_view> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd(std::span<const std::string> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// 跟随集群 MOVED/ASK 重定向
    auto cmd_follow_redirect(std::vector<std::string> args, std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// Pipeline（initializer_list 语法）
    auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// Pub/Sub
    auto subscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto unsubscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto psubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto punsubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    void on_push(push_callback cb);
    auto receive_push() -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// Sentinel
    auto sentinel_get_master_addr_by_name(std::string_view master)
        -> task<std::expected<endpoint_info, std::string>>;

    /// 集群工具
    auto is_resp3() const noexcept -> bool;
    static auto key_slot(std::string_view key) noexcept -> std::uint16_t;
    static auto parse_redirect(const std::vector<resp3_node>& nodes) -> std::optional<cluster_redirect>;
    static auto parse_cluster_slots(const std::vector<resp3_node>& nodes)
        -> std::expected<std::vector<cluster_slot_range>, std::string>;
};
```

### `connection_pool` — 连接池

```cpp
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password, username;
    std::uint32_t db = 0;
    bool resp3 = true;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
    std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto operator->() noexcept -> client*;
    // RAII: 析构时自动归还连接池
};

class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};
```

### `sharded_connection_pool` — 分片连接池

连接池等待者的取消和池停止通知使用等待协程帧内的投递节点，不为通知本身分配堆内存。
取消仅投递原等待者，原协程恢复后取得协程锁并移除登记，不启动 detached 清理协程。
连接分配、调用者取消和池停止通过同一个 pending 标志竞争完成权，只有获胜者投递。
调用者仍必须等待获取连接的任务结束后再销毁池和事件循环；此机制不支持强制销毁在途任务。

归还连接遇到池锁竞争时，使用连接节点内的投递通知，不创建 detached 归还协程。
待处理归还计入 `pending_maintenance()`，`cancel()` 等待已登记通知完成。
这不替代外部借出连接的生命周期管理：所有 lease 仍须在池销毁前归还。

`checked_out_count()` 在所属执行线程扫描节点，统计正常借出及停止后仍被持有的连接，
不为每次借用增加计数器操作。停止后归还的连接会关闭而不重新进入空闲池。
Application Redis 服务停止时按调用方 deadline 等待 lease；超时返回错误并保留服务状态，
归还后可再次调用停止。该约定仍不允许销毁外部正在使用的池。

适用于多核 `server_context` 场景，每个 worker `io_context` 绑定独立分片。

```cpp
class sharded_connection_pool {
    sharded_connection_pool(io_context& ctx, pool_params params, std::size_t num_shards = 4);
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params);
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params, std::size_t num_shards);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;
};
```

### 集群路由 (`cluster_client`)

```cpp
struct endpoint_info { std::string host; std::uint16_t port = 0; };
enum class redirect_kind { moved, ask };
struct cluster_redirect { redirect_kind kind; std::uint16_t slot; endpoint_info endpoint; };
struct cluster_slot_range {
    std::uint16_t start, end;
    endpoint_info master;
    std::vector<endpoint_info> replicas;
};
struct cluster_pipeline_item { std::vector<std::string> args; std::string key; };

class cluster_slot_cache {
    void clear();
    void update(const std::vector<cluster_slot_range>& ranges);
    void update_slot(std::uint16_t slot, endpoint_info endpoint);
    auto endpoint_for_slot(std::uint16_t slot) const -> std::optional<endpoint_info>;
    auto endpoint_for_key(std::string_view key) const -> std::optional<endpoint_info>;
    auto covered_slots() const noexcept -> std::size_t;
};

class cluster_client {
    explicit cluster_client(io_context& ctx) noexcept;
    auto connect(connect_options seed) -> task<std::expected<void, std::string>>;
    auto connect(connect_options seed, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
    auto refresh_slots() -> task<std::expected<void, std::string>>;
    auto cmd_for_key(std::vector<std::string> args, std::string_view key, std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd_for_keys(std::vector<std::string> args,
        std::span<const std::string_view> keys, cancel_token& cancellation,
        std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::error_code>>;
    auto pipeline(std::span<const cluster_pipeline_item> items)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto pipeline_ordered(std::span<const cluster_pipeline_item> items,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<std::vector<resp3_node>>, std::error_code>>;
    void close() noexcept;
    auto slots() const noexcept -> const cluster_slot_cache&;
};
```

### `redis_template` — 业务语义门面

`redis_template` 通过 `import cnetmod.protocol.redis;` 导出。它复用
`connection_pool`、`client::exchange()` 与 `client::is_reusable()`，不创建协议实现或
执行域。子模块的合法名称是 `cnetmod.protocol.redis:redis_template`；C++ 关键字
`template` 不能直接作为模块名分段。

```cpp
redis::redis_template cache{pool, {
    .ns = {.prefix = "orders:"},
    .default_ttl = std::chrono::minutes{10},
    .scan_page = 128,
    .scan_limit = 100000,
}};

auto order = co_await cache.get_as<order_record>("42");
auto saved = co_await cache.set_as("42", value);

auto batch = cache.pipeline();
batch.get("42").exists("43").incr("revision");
auto replies = co_await cache.execute(batch);
```

公开操作提供无令牌便利重载以及 `cancel_token&` 重载。需要超时时，在所属
`io_context` 上用 `with_timeout` / `with_deadline` 包装带令牌重载；取消会传到连接获取
及完整 RESP exchange。任何未完整 exchange 都关闭连接，池只重新发布
`is_reusable()` 为真的 lease。

- `get` / `hget` 将 Redis nil 映射为成功的 `std::optional{}`，不映射成错误。
- `mget` 保持与输入逐位对应，内部消化 RESP aggregate 根节点。
- `hgetall` 同时规范化 RESP2 array 和 RESP3 map。
- `sscan_all` 循环游标、保持首次出现顺序、去重，并在超过 `scan_limit` 时整体失败。
- Pipeline 只执行一次 `exchange()`，返回
  `std::vector<std::expected<reply, std::error_code>>`；Redis 单条错误不会覆盖其他条。
- `json_codec` 是 `get_as` / `set_as` 的默认 codec，可用满足同一静态接口的业务 codec 替换。
- 配置 `span_exporter` 后，每条命令产生 CLIENT span，只记录
  `db.system.name=redis` 与 `db.operation.name`，不记录 key、value 或服务端错误正文。

Application 的 `redis_service::make_template(options, parent)` 自动复用服务连接池及
Telemetry Hub 的 span exporter；调用方只显式传递当前协程的 trace parent。

`is_open()` 只表示传输层 socket 尚未关闭；连接池使用更严格的
`is_reusable()`，同时要求不存在未消费的 RESP 数据。`cmd`、`exec`、`pipe` 和
`exchange` 在写入后发生解析错误、取消、EOF 或检测到多余应答时都会关闭连接，防止
残留帧被下一位借用者误认为自己的响应。

Cluster 只支持逻辑数据库 0，`connect_options::db != 0` 会在网络 I/O 前失败。客户端维护
16384 槽缓存，处理 MOVED，并在 ASKING 成功后把原命令真正重发到迁移目标。跨节点
pipeline 会先按节点批量发送，再按调用者原始顺序恢复每条完整 RESP 响应。

多 key 命令必须同槽。使用 `make_cluster_key("session", tenant, key)` 生成
`session:{tenant}:key`，并用 `keys_share_slot()` 在发送前验证。Redis Cluster 已经负责
Redis 数据分片和故障转移，不要再使用 SELECT/多 DB 模拟分库；业务隔离使用 key
namespace，原子多 key 操作用 hash tag。

Application 自动装配使用 `type: redis` 和 `mode: cluster`：

```json
{
  "type": "redis",
  "instance": "sessions",
  "enabled": true,
  "mode": "cluster",
  "database": 0,
  "seeds": [
    {"host": "redis-0.internal", "port": 6379},
    {"host": "redis-1.internal", "port": 6379}
  ],
  "password": "${REDIS_PASSWORD}"
}
```

`redis_cluster_service` 依次尝试 seed、缓存槽表、用 PING 与完整槽覆盖做健康判断，并在
停止时关闭 seed 和节点连接。恢复预算与 required/optional 语义沿用统一生命周期。

## 场景 1：基本命令

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;
using cn::redis::first_value;
using cn::redis::is_ok;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::redis::client r(ctx);
    auto result = co_await r.connect({
        .host = "127.0.0.1",
        .port = 6379,
        .password = "your-password",
        .db = 0,
    });
    if (!result) {
        std::println("连接失败: {}", result.error());
        ctx.stop();
        co_return;
    }

    // 快捷命令
    auto pong = co_await r.cmd({"PING"});
    auto set_r = co_await r.cmd({"SET", "mykey", "hello"});
    auto get_r = co_await r.cmd({"GET", "mykey"});
    if (get_r) std::println("GET mykey = {}", first_value(*get_r));

    // Hash 操作
    (void)co_await r.cmd({"HSET", "user:1", "name", "Alice", "score", "100"});
    auto hall = co_await r.cmd({"HGETALL", "user:1"});

    r.close();
    ctx.stop();
}
```

## 场景 2：Pipeline

```cpp
// Pipeline 语法：一次发送多个命令
auto replies = co_await r.pipe({
    {"SET", "p:a", "alpha"},
    {"SET", "p:b", "beta"},
    {"SET", "p:c", "gamma"},
    {"MGET", "p:a", "p:b", "p:c"},
    {"DEL", "p:a", "p:b", "p:c"},
});

auto vals = cn::redis::all_values(*replies);
for (std::size_t i = 0; i < vals.size(); ++i)
    std::println("[{}] {}", i, vals[i]);
```

## 场景 3：request 构建器

```cpp
using cn::redis::request;

// 单命令
request req;
req.push("SET", "rb:key", "value123");
auto set_r = co_await r.exec(req);

// Pipeline 多命令
request multi;
multi.push("SET", "rb:a", "alpha");
multi.push("SET", "rb:b", "beta");
multi.push("GET", "rb:a");
multi.push("GET", "rb:b");
multi.push("DEL", "rb:a", "rb:b", "rb:key");
auto multi_r = co_await r.exec(multi);
std::println("executed {} commands, got {} nodes", multi.size(), multi_r->size());

// 批量 range
std::vector<std::string> fields = {"f1", "v1", "f2", "v2", "f3", "v3"};
request hmset;
hmset.push_range_pairs("HSET", "myhash", fields.begin(), fields.end());
co_await r.exec(hmset);
```

## 场景 4：连接池

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;

auto pool_demo(cn::io_context& ctx) -> cn::task<void> {
    cn::redis::connection_pool pool(ctx, {
        .host = "127.0.0.1",
        .port = 6379,
        .password = "your-password",
        .initial_size = 4,
        .max_size = 32,
    });

    cn::spawn(ctx, pool.async_run());

    // 获取连接（RAII 自动归还）
    auto conn = co_await pool.async_get_connection();
    if (conn) {
        auto result = co_await conn->cmd({"SET", "pool:key", "pooled!"});
        auto get_r = co_await conn->cmd({"GET", "pool:key"});
        if (get_r) std::println("GET = {}", cn::redis::first_value(*get_r));
    } // conn 析构时自动归还

    co_await pool.cancel();
    ctx.stop();
}
```

## 场景 5：集群客户端

```cpp
cn::redis::cluster_client cluster(ctx);
co_await cluster.connect({.host = "node1", .port = 7000});

// 自动根据 key 的 slot 路由到正确节点
auto result = co_await cluster.cmd_for_key(
    {"GET", "user:100"}, "user:100");

// Pipeline（按 key 分组路由）
std::vector<cn::redis::cluster_pipeline_item> items = {
    {.args = {"SET", "k1", "v1"}, .key = "k1"},
    {.args = {"SET", "k2", "v2"}, .key = "k2"},
};
auto pipe_r = co_await cluster.pipeline(items);
```

## 连接池（生产级用法）

### Pool API 详解

Redis 连接池提供 `connection_pool`（单核）和 `sharded_connection_pool`（多核分片）两种模式。

**`connection_pool`** — 单 io_context 连接池：

```cpp
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password, username;
    std::uint32_t db = 0;
    bool resp3 = true;
    std::size_t initial_size = 1;           // 初始连接数
    std::size_t max_size = 16;              // 最大连接数
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);     // 等待连接超时
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);  // 重连间隔
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);      // 心跳间隔
    std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
};

class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;                  // 启动池（必须先调用）
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection()
        -> std::expected<pooled_connection, std::error_code>; // 非阻塞获取
    auto cancel() -> task<void>;                     // 关闭池
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto operator->() noexcept -> client*;
    // RAII: 析构时自动归还连接池
};
```

**`sharded_connection_pool`** — 多核分片连接池（每个 worker io_context 绑定独立分片）：

```cpp
class sharded_connection_pool {
    // 单 io_context + 指定分片数
    sharded_connection_pool(io_context& ctx, pool_params params,
        std::size_t num_shards = 4);
    // 多 worker io_context，每个 worker 一个分片（推荐）
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params);
    // 多 worker io_context + 指定分片数
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params, std::size_t num_shards);

    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;
};
```

**示例 — 生产级分片连接池**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    cn::redis::pool_params params;
    params.host = "redis.example.com";
    params.port = 6379;
    params.password = "production_secret";
    params.db = 0;
    params.initial_size = 16;        // 大初始池
    params.max_size = 128;           // 高并发池上限
    params.ping_interval = std::chrono::minutes(30);  // 保活心跳

    // 4 分片，适合 4 worker 线程
    cn::redis::sharded_connection_pool pool(ctx, params, 4);
    co_await pool.async_run();

    // 等待连接建立
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));
    std::println("分片池就绪: shards={}, total={}, idle={}",
        pool.shard_count(), pool.size(), pool.idle_count());

    // 自动分片选择
    auto conn_r = co_await pool.async_get_connection();
    if (conn_r) {
        co_await conn_r->cmd({"SET", "app:config:version", "2.1"});
        auto val = co_await conn_r->cmd({"GET", "app:config:version"});
        if (val && !val->empty())
            std::println("version = {}", (*val)[0].value);
    }

    // 绑定 io_context（多 worker 场景推荐）
    auto conn2_r = co_await pool.async_get_connection(ctx);
    if (conn2_r) {
        // Pipeline 批量操作
        auto replies = co_await conn2_r->pipe({
            {"SET", "session:abc", "data", "EX", "3600"},
            {"SET", "session:def", "data", "EX", "3600"},
            {"MGET", "session:abc", "session:def"}
        });
    }

    co_await pool.cancel();
    ctx.stop();
}
```

## 多核服务器部署

### server_context 模式

Redis 分片连接池天然支持多核架构：使用 `sharded_connection_pool` + `server_context`，每个 worker 线程绑定独立分片，避免锁竞争。

```cpp
class server_context {
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    auto accept_io() noexcept -> io_context&;        // accept 专用 io_context
    auto next_worker_io() noexcept -> io_context&;   // round-robin 选择 worker
    auto worker_count() const noexcept -> unsigned;
    auto worker_ios() -> std::vector<io_context*>;    // 所有 worker io_context
    auto pool() noexcept -> thread_pool&;             // cnetmod CPU 线程池
    void spawn_next(task<void> t);                    // 在下一个 worker 上启动协程
    void run();                                       // 阻塞运行
    void stop();                                      // 停止所有线程
};
```

**Redis 多核部署示例**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.redis;

namespace cn = cnetmod;

constexpr unsigned WORKER_THREADS = 4;
constexpr std::uint16_t PORT = 9090;

auto handle_redis_command(cn::redis::sharded_connection_pool& pool,
                          cn::io_context& worker_io) -> cn::task<void>
{
    // 绑定到当前 worker 的分片获取连接（零跨线程开销）
    auto conn_r = co_await pool.async_get_connection(worker_io);
    if (!conn_r) {
        std::println("获取连接失败: {}", conn_r.error().message());
        co_return;
    }

    auto& conn = *conn_r;

    // 高并发写入
    co_await conn->cmd({"SET", "metrics:requests", "1", "EX", "60"});
    co_await conn->cmd({"INCR", "metrics:total_requests"});

    // Pipeline 批量读取
    auto results = co_await conn->pipe({
        {"GET", "app:config:feature_a"},
        {"GET", "app:config:feature_b"},
        {"GET", "app:config:feature_c"}
    });
    if (results && !results->empty()) {
        for (auto& node : *results)
            std::println("config value: {}", node.value);
    }
}

auto main() -> int
{
    std::println("=== Redis 多核服务 ===");
    std::println("Workers: {}, Pool threads: {}", WORKER_THREADS, WORKER_THREADS);

    cn::net_init net;

    // 1. 创建多核 server_context：4 worker + 4 pool 线程
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 2. 使用 worker io_context 列表创建分片池（每 worker 一个分片）
    cn::redis::pool_params params;
    params.host = "redis.example.com";
    params.port = 6379;
    params.password = "production_secret";
    params.db = 0;
    params.initial_size = WORKER_THREADS * 4;   // 每 worker 4 个初始连接
    params.max_size = WORKER_THREADS * 32;      // 每 worker 最多 32 个连接
    params.ping_interval = std::chrono::minutes(30);

    cn::redis::sharded_connection_pool pool(sctx.worker_ios(), params);

    // 3. 在 accept_io 上启动连接池
    cn::spawn(sctx.accept_io(), pool.async_run());

    // 4. 接受 TCP 连接，round-robin 分发到 worker
    cn::spawn(sctx.accept_io(), [&]() -> cn::task<void> {
        auto listener = cn::tcp_listener::create(sctx.accept_io());
        listener.bind("0.0.0.0", PORT);
        listener.listen(4096);

        std::println("Redis 代理监听 0.0.0.0:{}", PORT);

        while (true) {
            auto [sock, addr] = co_await listener.accept();
            auto& worker = sctx.next_worker_io();  // round-robin
            cn::spawn(worker, [&pool, &worker]() -> cn::task<void> {
                co_await handle_redis_command(pool, worker);
            });
        }
    }());

    // 5. 阻塞运行（accept 线程 + worker 线程）
    sctx.run();
    return 0;
}
```

## Do's & Don'ts

| Do | Don't |
|---|---|
| 使用 `request` 构建器实现 Pipeline 批量操作 | 不要在循环中逐条 `cmd` 发送大量命令 |
| 连接池使用 `pooled_connection` RAII 自动归还 | 不要手动管理连接的释放 |
| 集群环境使用 `cluster_client` 自动处理重定向 | 不要忽略 MOVED/ASK 重定向错误 |
| 使用 `first_value` / `is_ok` 辅助函数解析结果 | 不要假设 `resp3_node` 的 value 字段总是有效 |
| 长连接启用 `ping_interval` 保活 | 不要在高并发场景为每个请求创建新 client |
| 多核场景使用 `sharded_connection_pool` + `server_context` | 不要在多 worker 场景使用单 `connection_pool` |
| 通过 `async_get_connection(io_context&)` 绑定 worker 分片 | 不要让请求跨 worker 分片获取连接 |

## 本地真实服务测试

`test_application_redis_live` 只连接显式指定端口的 `127.0.0.1`，执行 RESP3 建连、
健康 PING、三次关闭自身借出的连接后重新建连，以及受监管停止，不创建键或修改服务配置。设置 `CNETMOD_REDIS_INTEGRATION=1`
及 `CNETMOD_REDIS_TEST_PORT` 后通过 CTest 运行；未启用时返回 77，由 CTest 标记 skipped。
端口缺失或非法直接失败。测试服务须自行启动、隔离和回收；该入口不验证服务端宕机恢复，
也不意味着 Redis/Valkey 的真实服务验收已经通过。

## 参考示例

- `examples/redis/redis_client.cpp` — 基本命令 + request 构建器 + Pipeline + 阻塞调用桥接
- `examples/redis/redis_pool.cpp` — 连接池：单/多线程获取连接
- `examples/redis/redis_sharded_pool.cpp` — 分片连接池 + server_context 多核场景
- `examples/http/multicore_http.cpp` — `server_context` 多核架构参考
<!-- END SOURCE: skill/database/redis.md -->

<!-- BEGIN SOURCE: skill/http/http-client.md -->
# Source: `skill/http/http-client.md`

# HTTP Client

> 统一异步 HTTP/HTTPS 客户端，支持 HTTP/1.1、HTTP/2、HTTP/3（QUIC），并内置 Cookie、重定向与连接复用。

**import**: `import cnetmod.protocol.http;`
**CMake**: `-DCNETMOD_ENABLE_HTTP=ON`
**源码**: `src/protocol/http/client/`

## 场景导航
- 我要发送 GET/POST 请求 → [看这里](#快捷请求方法)
- 我要自定义请求头和请求体 → [看这里](#request--请求构建)
- 我要配置超时和 SSL → [看这里](#client_options--配置)
- 我要通过统一客户端发 HTTP/3 请求 → [看这里](#http3统一-httpclient)
- 我要管理 Cookie → [看这里](#cookie--cookie-管理)
- 我要复用连接（连接池）→ [看这里](#client_pool--连接池)
- 我要发送并发 HTTP/2 请求 → [看这里](#send_batch--http2-并发)
- 我要升级为 WebSocket → [参见 websocket.md](websocket.md)

## API 参考

### `client_options` — 配置

```cpp
struct client_options {
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    bool follow_redirects = true;
    std::size_t max_redirects = 10;
    bool keep_alive = true;
    std::string user_agent = "cnetmod-http-client/1.0";

    // SSL/TLS
    bool verify_peer = true;
    std::string ca_file;
    std::string cert_file;
    std::string key_file;

    // HTTP/2
    http_version_preference version_pref = http_version_preference::http2_preferred;
    std::uint32_t h2_max_concurrent_streams = 100;
    std::uint32_t h2_initial_window_size = 1 * 1024 * 1024;

    // HTTP/3 / QUIC
    std::uint64_t h3_qpack_max_table_capacity = 64 * 1024;
    std::uint64_t h3_qpack_blocked_streams = 100;
    std::uint32_t h3_max_concurrent_streams = 100;
    bool http3_fallback_to_tcp = false;
    bool enable_alt_svc_http3 = true;

    // Cookie
    bool enable_cookies = true;
};
```

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `connect_timeout` | 5s | TCP 连接超时 |
| `request_timeout` | 30s | 请求总超时 |
| `follow_redirects` | `true` | 自动跟踪 3xx 重定向 |
| `max_redirects` | 10 | 最大重定向次数 |
| `keep_alive` | `true` | 保持 TCP 连接（HTTP/1.1 Keep-Alive） |
| `verify_peer` | `true` | 验证服务器证书 |
| `version_pref` | `http2_preferred` | HTTP 版本偏好 |
| `h3_qpack_max_table_capacity` | 64 KiB | HTTP/3 QPACK 动态表容量 |
| `h3_qpack_blocked_streams` | 100 | HTTP/3 QPACK 允许阻塞的 stream 数 |
| `h3_max_concurrent_streams` | 100 | `send_batch` 在一条 HTTP/3 连接上同时运行的请求上限 |
| `http3_fallback_to_tcp` | `false` | 仅为安全方法显式允许 HTTP/3 到 TCP 回退 |
| `enable_alt_svc_http3` | `true` | 在 HTTPS 的 HTTP/1.1/2 响应收到 `Alt-Svc: h3=...` 后，为同一 origin 升级到 HTTP/3；遵守 `ma`，`ma=0` 会清除记录 |

**`http_version_preference` 枚举**:
| 值 | 说明 |
|---|------|
| `http1_only` | 仅使用 HTTP/1.1 |
| `http2_only` | 仅使用 HTTP/2（需 ALPN） |
| `http2_preferred` | 优先 HTTP/2，回退 HTTP/1.1 |
| `http1_preferred` | 优先 HTTP/1.1，接受 HTTP/2 |
| `http3_only` | 仅使用 HTTP/3/QUIC；失败不降级 |
| `http3_preferred` | 优先 HTTP/3；默认不降级，避免隐式重放 |

### HTTP/3（统一 `http::client`）

HTTP/3 与 HTTP/1.1、HTTP/2 使用同一个 `client`。设置版本偏好后，现有的 `get`、`post`、`send` 会自动经 QUIC 发送请求，并仍然返回 `http::response`。

```cpp
import cnetmod.protocol.http;

using namespace cnetmod::http;

client_options options;
options.version_pref = http_version_preference::http3_only;
client http_client(*ctx, options);

auto result = co_await http_client.get("https://api.example.com/v1/profile");
```

`http3_only` 只接受绝对 `https://` URL，绝不降级。`http3_preferred` 同样默认不降级，避免请求已到达服务端时被静默重放。业务明确允许 GET、HEAD、OPTIONS 回退时，才设置 `http3_fallback_to_tcp = true`；POST、PUT、PATCH 等非幂等请求不会自动重放。

当 `version_pref = http2_preferred` 时，客户端会从成功的 HTTPS TCP 响应学习 `Alt-Svc: h3=...`，并在同一 origin 的后续请求中改用 HTTP/3。支持 `h3=":端口"`、缓存寿命 `ma` 与 `ma=0` 失效；不接受跨主机的替代端点，因此 TLS 证书和请求 authority 始终保持同源。

`http3_preferred` 的首次 GET/HEAD/OPTIONS 会在 HTTP/3 与 TCP 并发竞速；首个成功路径获胜后会取消 loser。POST/PUT/PATCH 不参加竞速，避免重复提交。

---

### 创建客户端

#### `client::client`
**签名**: `explicit client(io_context& ctx, client_options opts = {})`
**说明**: 创建异步 HTTP 客户端，不可拷贝，可移动。

**示例**:
```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

auto main() -> int {
    auto ctx = make_io_context();

    client_options opts;
    opts.connect_timeout = std::chrono::seconds(5);
    opts.request_timeout = std::chrono::seconds(30);
    opts.follow_redirects = true;
    opts.verify_peer = true;
    opts.version_pref = http_version_preference::http2_preferred;

    client http_client(*ctx, opts);

    spawn(*ctx, [&](client& c) -> task<void> {
        auto result = co_await c.get("http://httpbin.org/get");
        if (result) {
            std::println("Status: {}", result->status_code());
            std::println("Body: {}", result->body());
        } else {
            std::println("Error: {}", result.error().message());
        }
    }(http_client));

    ctx->run();
}
```

---

### 快捷请求方法

## 请求级 deadline 与取消

除 `client_options::request_timeout` 的默认保护外，业务入口可把一个绝对 deadline 直接传给单个请求。它会创建本次请求专属的取消 token，并向 HTTP、TLS 以及底层 I/O 传播；超时时返回 `std::errc::timed_out`。

```cpp
import cnetmod.coro;
import cnetmod.protocol.http;

cnetmod::http::request request{cnetmod::http::http_method::GET,
    "https://api.example.com/profile"};
auto result = co_await http_client.send(request,
    cnetmod::deadline::after(std::chrono::milliseconds{800}));
```

当上层已经管理取消时，使用 `send(request, cancel_token&)`。需要为多个下游共享总预算时传递同一个 `deadline`，下游用 `constrain()` 缩短自身预算。

#### `client::get`
**签名**: `[[nodiscard]] auto get(std::string_view url) -> task<std::expected<response, std::error_code>>`

#### `client::post`
**签名**: `[[nodiscard]] auto post(std::string_view url, std::string_view body) -> task<std::expected<response, std::error_code>>`

#### `client::put`
**签名**: `[[nodiscard]] auto put(std::string_view url, std::string_view body) -> task<std::expected<response, std::error_code>>`

#### `client::delete_`
**签名**: `[[nodiscard]] auto delete_(std::string_view url) -> task<std::expected<response, std::error_code>>`

#### `client::patch`
**签名**: `[[nodiscard]] auto patch(std::string_view url, std::string_view body) -> task<std::expected<response, std::error_code>>`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto fetch_data(client& c) -> task<void> {
    // GET
    auto r1 = co_await c.get("https://api.example.com/users");
    if (r1) std::println("Users: {}", r1->body());

    // POST JSON
    auto r2 = co_await c.post("https://api.example.com/users",
        R"({"name":"Alice","age":30})");
    if (r2) std::println("Created: {}", r2->status_code());

    // DELETE
    auto r3 = co_await c.delete_("https://api.example.com/users/42");
    if (r3) std::println("Deleted: {}", r3->status_code());
}
```

---

### `request` — 请求构建

#### `request::request`
**签名**: `explicit request(http_method method, std::string_view uri, http_version version = http_version::http_1_1)`

#### `request::set_header`
**签名**: `auto& set_header(std::string_view key, std::string_view value)`

#### `request::append_header`
**签名**: `auto& append_header(std::string_view key, std::string_view value)`

#### `request::set_body`
**签名**: `auto& set_body(std::string_view body)` / `auto& set_body(std::string body)`
**说明**: 自动设置 `Content-Length` 头。

#### `request::set_body_stream`
**签名**:
```cpp
using request_body_reader =
    std::function<task<std::optional<request_body_chunk>>(cancel_token&)>;

request_body_source(request_body_reader reader,
    std::optional<std::uint64_t> content_length = std::nullopt);
auto& request::set_body_stream(request_body_source source);
```

**说明**: 生产者在每次上一个分块写入完成后才会被再次调用；返回 `std::nullopt` 表示 EOF。提供长度时自动设置 `Content-Length`，否则 HTTP/1.1 使用 chunked，HTTP/2/3 使用连续 DATA 帧。取消 token 会传给生产者，并在底层 I/O 上终止当前请求。

流式 body 是一次性 producer，不应把同一个请求重复用于重定向或自动重放；需要重试时应重新创建 producer。

#### HTTP/3 大响应的流式消费

统一 `http::client::send()` 保持完整 `response.body` 兼容语义。需要边收边处理
时，使用 `cnetmod::http::v3::http3_client::send_request_streaming()`；它在
HEADERS 到达后调用 handler，并通过有界 `request_body_stream` 提供 DATA。详见
`skill/http/http3-quic.md` 的“HTTP/3 客户端响应体流式消费”章节。

#### `client::send`
**签名**:
```cpp
[[nodiscard]] auto send(const request& req) -> task<std::expected<response, std::error_code>>;
[[nodiscard]] auto send(http_method method, std::string_view url, std::string_view body = {})
    -> task<std::expected<response, std::error_code>>;
```

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto custom_request(client& c) -> task<void> {
    request req(http_method::POST, "https://api.example.com/upload");
    req.set_header("Content-Type", "application/json");
    req.set_header("Authorization", "Bearer token123");
    req.set_body(R"({"file":"data.bin"})");

    auto result = co_await c.send(req);
    if (result) {
        std::println("Status: {}", result->status_code());
        std::println("Header: {}", result->get_header("Content-Type"));
    }
}
```

---

### `response` — 响应访问

#### `response::status_code`
**签名**: `[[nodiscard]] auto status_code() const noexcept -> int`

#### `response::body`
**签名**: `[[nodiscard]] auto body() const noexcept -> std::string_view`

#### `response::get_header`
**签名**: `[[nodiscard]] auto get_header(std::string_view key) const -> std::string_view`

#### `response::headers`
**签名**: `[[nodiscard]] auto headers() const noexcept -> const header_map&`

---

### `cookie` — Cookie 管理

#### `cookie` 结构体
```cpp
struct cookie {
    std::string name, value;
    std::string domain, path = "/";
    std::optional<std::chrono::seconds> max_age;
    bool secure = false, http_only = false;
    enum class same_site_policy { none, lax, strict };
    std::optional<same_site_policy> same_site;
};
```

#### `client::set_cookie`
**签名**: `auto& set_cookie(std::string_view name, std::string_view value, std::string_view domain = {}, std::string_view path = "/")`

#### `client::cookies`
**签名**: `auto cookies() -> cookie_jar&`

#### `client::clear_cookies`
**签名**: `auto& clear_cookies()`

#### `cookie_jar::add`
**签名**: `void add(const cookie& c)`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto cookie_demo(client& c) -> task<void> {
    // 快捷设置
    c.set_cookie("session", "abc123", "example.com", "/");

    // 完整控制
    cookie ck;
    ck.name = "token"; ck.value = "xyz";
    ck.domain = "api.example.com";
    ck.secure = true; ck.http_only = true;
    ck.same_site = cookie::same_site_policy::strict;
    c.cookies().add(ck);

    // 请求自动携带 Cookie
    auto r = co_await c.get("https://api.example.com/data");

    // 查看已存储的 Cookie
    for (auto& ck : c.cookies().cookies()) {
        std::println("{}={} (domain: {})", ck.name, ck.value, ck.domain);
    }

    c.clear_cookies();
}
```

---

### `send_batch` — HTTP/2 / HTTP/3 并发

**签名**: `[[nodiscard]] auto send_batch(std::span<const request> requests) -> task<std::vector<std::expected<response, std::error_code>>>`
**说明**: 对同一 origin 的请求使用 HTTP/2 或 HTTP/3 多路复用，在同一连接上并发发送。HTTP/1.1 回退为顺序发送。HTTP/3 批处理只接受同源绝对 `https://` URL，不会错误建立 TCP 连接；并发 stream 数由 `h3_max_concurrent_streams` 限制，防止单个批次压垮连接。

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto batch_demo(client& c) -> task<void> {
    std::vector<request> reqs;
    for (int i = 1; i <= 5; ++i) {
        reqs.emplace_back(http_method::GET,
            std::format("https://api.example.com/users/{}", i));
    }

    auto results = co_await c.send_batch(reqs);
    for (auto& r : results) {
        if (r) std::println("Status: {}", r->status_code());
    }
}
```

---

### `client_pool` — 连接池

**签名**:
```cpp
class client_pool {
    client_pool(io_context& context, client_options options = {}, std::size_t max_idle = 64);
    [[nodiscard]] auto acquire() -> std::unique_ptr<client>;
    void release(std::unique_ptr<client> value);
    void clear() noexcept;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto pool_demo(client_pool& pool) -> task<void> {
    auto c = pool.acquire();
    auto r = co_await c->get("https://api.example.com/health");
    if (r) std::println("Status: {}", r->status_code());
    pool.release(std::move(c));
}
```

---

### WebSocket 升级

#### `client::release_connection`
**签名**: `[[nodiscard]] auto release_connection() -> std::optional<socket>`
**说明**: 释放底层 socket 用于 WebSocket 升级，之后 client 不可再使用。

详见 [websocket.md](websocket.md)。

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 复用同一个 `client` 实例发送多个请求 | 每次请求创建新 `client` |
| 用 `send_batch` 并发请求同域 API | 逐个 `await` 同域请求 |
| 通过 `client_options` 设置超时 | 不设超时导致请求永久挂起 |
| 检查 `std::expected` 的 `error()` | 直接 `*result` 不检查错误 |
| HTTPS 请求启用 `verify_peer` | 生产环境关闭证书验证 |

## 连接池（生产级用法）

### `client_pool` — 完整 API

**签名**（源码 `client_pool.cppm`）：
```cpp
struct client_pool_key {
    std::string host;
    std::uint16_t port{};
    bool tls{};
};

class client_pool {
    client_pool(io_context& context, client_options options = {}, std::size_t max_idle = 64);

    // 通用池：acquire/release 不区分端点
    [[nodiscard]] auto acquire() -> std::unique_ptr<client>;
    void release(std::unique_ptr<client> value);

    // 端点感知池：按 host:port:tls 复用连接
    [[nodiscard]] auto acquire(client_pool_key key) -> std::unique_ptr<client>;
    void release(client_pool_key key, std::unique_ptr<client> value);

    void clear() noexcept;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
};
```

**连接复用策略**：
| 模式 | 说明 |
|------|------|
| 通用 `acquire()` / `release()` | 池内任意空闲 client，适合单端点场景 |
| 端点感知 `acquire(key)` / `release(key, ...)` | 按 `(host, port, tls)` 精确匹配，适合多端点代理网关 |
| HTTP/2 多路复用 | 同一 client 实例自动通过 `send_batch` 在同一连接上并发多个 stream |
| HTTP/1.1 Keep-Alive | `client_options::keep_alive = true`（默认），同一 client 串行复用连接 |

---

### 端点感知连接池示例

```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

auto gateway_handler(client_pool& pool) -> task<void> {
    // 请求 user-service
    {
        auto c = pool.acquire({"user-service.internal", 8080, false});
        auto r = co_await c->get("http://user-service.internal:8080/api/users/42");
        if (r) std::println("User: {}", r->body());
        pool.release({"user-service.internal", 8080, false}, std::move(c));
    }

    // 请求 order-service（不同端点，独立连接池）
    {
        auto c = pool.acquire({"order-service.internal", 8081, false});
        auto r = co_await c->get("http://order-service.internal:8081/api/orders");
        if (r) std::println("Orders: {}", r->body());
        pool.release({"order-service.internal", 8081, false}, std::move(c));
    }

    std::println("Idle clients in pool: {}", pool.idle_count());
}

auto main() -> int {
    auto ctx = make_io_context();

    client_options opts;
    opts.connect_timeout = std::chrono::seconds(3);
    opts.request_timeout = std::chrono::seconds(10);
    opts.keep_alive = true;
    opts.version_pref = http_version_preference::http2_preferred;

    // 最多缓存 128 个空闲 client
    client_pool pool(*ctx, opts, 128);

    spawn(*ctx, gateway_handler(pool));
    ctx->run();
    return 0;
}
```

### HTTP/2 多路复用 + 连接池

```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto h2_batch_with_pool(client_pool& pool) -> task<void> {
    auto c = pool.acquire();

    // send_batch 在同一 HTTP/2 连接上并发发送多个请求
    std::vector<request> reqs;
    for (int i = 1; i <= 10; ++i) {
        reqs.emplace_back(http_method::GET,
            std::format("https://api.example.com/items/{}", i));
    }

    auto results = co_await c->send_batch(reqs);
    for (auto& r : results) {
        if (r) std::println("Status: {}, Body: {}", r->status_code(), r->body());
    }

    pool.release(std::move(c));
}
```

### Do's & Don'ts（连接池）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用完毕立即 `release` 归还 client | 长期持有 client 不归还导致池耗尽 |
| 多端点场景使用 `client_pool_key` 精确路由 | 混用不同端点却使用通用 `acquire()` |
| 配合 `send_batch` 对同域请求做 HTTP/2 多路复用 | 对同域请求逐个 `await` 浪费连接 |
| 合理设置 `max_idle` 避免内存浪费 | 设置过大导致大量空闲连接占内存 |
| 生产环境保持 `keep_alive = true` | 每次请求后关闭连接 |

---

## 参考示例
- `examples/http/client_demo.cpp` — GET/POST、HTTP/2、SSL 基础示例
- `examples/http/cookie_demo.cpp` — Cookie 自动管理示例
- `examples/http/cookie_and_chunked_demo.cpp` — Cookie 简化 API 与 chunked 传输

# HTTP/3 ticket persistence

`client_options::http3_resumption_ticket_file` optionally persists one TLS 1.3
session ticket per HTTPS origin. The file is replaced atomically and should be
kept in a directory readable only by the application account because it
contains sensitive TLS session material. Leave it empty to keep the existing
in-memory-only behavior.

Ticket loading/export is wired into the unified `http::client`; HTTP requests
are still sent after the HTTP/3 handshake and control streams are ready. This
is deliberate: the option does not claim full HTTP-layer 0-RTT request
support, and replay-safe request policy remains explicit.

Set `client_options::enable_http3_early_data = true` only together with a
trusted ticket cache. The client then queues replay-safe GET/HEAD/OPTIONS
streams during the handshake; if the server rejects 0-RTT, those streams are
reset and the request is retried once at 1-RTT. Non-idempotent methods are
never replayed.
<!-- END SOURCE: skill/http/http-client.md -->

<!-- BEGIN SOURCE: skill/http/http-middleware.md -->
# Source: `skill/http/http-middleware.md`

# HTTP Middleware

> HTTP 中间件管道系统，提供认证、限流、压缩、日志、防火墙等 18 个可组合中间件。

**import**: `import cnetmod.protocol.http.middleware;`
**CMake**: `-DCNETMOD_ENABLE_HTTP=ON`
**源码**: `src/protocol/http/middleware/`

## 场景导航
- 我要了解中间件管道顺序 → [看这里](#中间件管道顺序)
- 我要处理跨域请求 → [cors](#1-cors--跨域)
- 我要做 JWT 认证 → [jwt_auth](#2-jwt_auth--jwt-认证)
- 我要做权限授权 → [authorization](#3-authorization--授权)
- 我要限制请求频率 → [rate_limiter](#4-rate_limiter--速率限制)
- 我要 gzip 压缩响应 → [compress](#5-compress--gzip-压缩)
- 我要限制请求体大小 → [body_limit](#6-body_limit--请求体限制)
- 我要注入请求 ID → [request_id](#7-request_id--请求-id)
- 我要接入 W3C Trace Context / OpenTelemetry → [tracing](#w3c-trace-context--opentelemetry-bridge)
- 我要记录访问日志 → [access_log](#8-access_log--访问日志)
- 我要采集 Prometheus 指标 → [metrics](#9-metrics--指标采集)
- 我要控制请求超时 → [timeout](#10-timeout--超时控制)
- 我要优雅关闭服务 → [graceful_shutdown](#11-graceful_shutdown--优雅关闭)
- 我要设置 IP 防火墙 → [ip_firewall](#12-ip_firewall--ip-防火墙)
- 我要过滤 IP → [ip_filter](#13-ip_filter--ip-过滤)
- 我要缓存响应 → [cache_store / http_cache](#14-cache_store--缓存存储)
- 我要做健康检查 → [health_check](#15-health_check--健康检查)
- 我要处理文件上传 → [upload](#16-upload--文件上传)
- 我要 panic 恢复 → [recover](#17-recover--panic-恢复)

## 中间件类型

```cpp
using handler_fn = std::function<task<void>(request_context&)>;
using next_fn = std::function<task<void>()>;
using middleware_fn = std::function<task<void>(request_context&, next_fn)>;
```

注册方式统一：`server.use(middleware_fn)`。中间件按注册顺序形成洋葱模型管道。

## 中间件管道顺序

**推荐注册顺序**（外层 → 内层）：

```
1. recover         ← 最外层：捕获所有异常
2. request_timeout ← 超时检测（包裹 handler 执行）
3. tracing         ← 解析 traceparent，建立 server span
4. access_log      ← 记录请求/响应日志
5. cors            ← 处理 OPTIONS 预检
6. request_id      ← 注入 X-Request-ID
7. ip_firewall     ← IP 封禁检查（check_middleware）
8. ip_filter       ← IP 黑白名单
9. rate_limiter    ← 频率限制
10. body_limit     ← 请求体大小
11. compress       ← gzip 压缩
12. metrics        ← 指标采集
13. jwt_auth       ← 认证
14. authorization  ← 授权
15. upload         ← 文件上传解析
16. handler        ← 业务逻辑
17. ip_firewall    ← 违规追踪（track_middleware，最内层）
```

```cpp
import std;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware;

using namespace cnetmod;
using namespace cnetmod::http;

server srv(*ctx);
srv.use(recover());
srv.use(request_timeout(std::chrono::seconds{5}));
srv.use(access_log());
srv.use(cors());
srv.use(request_id());
srv.use(body_limit(2 * 1024 * 1024));
srv.use(compress());
srv.use(jwt_auth({.verify = my_verify, .skip_paths = {"/", "/login"}}));
srv.set_router(std::move(r));
```

---

## API 参考

### 1. cors — 跨域

**签名**: `auto cors(cors_options opts = {}) -> http::middleware_fn`

```cpp
struct cors_options {
    std::vector<std::string> allow_origins = {"*"};
    std::vector<std::string> allow_methods = {"GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"};
    std::vector<std::string> allow_headers = {"Content-Type", "Authorization", "X-Request-ID"};
    std::vector<std::string> expose_headers = {"X-Request-ID"};
    bool allow_credentials = false;
    int max_age = 86400;
};
```

**行为**: OPTIONS 预检自动返回 204；其他请求添加 CORS 头后调用 `next()`。

```cpp
srv.use(cors({
    .allow_origins = {"https://app.example.com"},
    .allow_credentials = true,
    .max_age = 3600,
}));
```

### 2. jwt_auth — JWT 认证

**签名**: `auto jwt_auth(jwt_auth_options opts) -> http::middleware_fn`

```cpp
struct jwt_auth_options {
    std::function<bool(std::string_view token)> verify;
    std::vector<std::string> skip_paths;
    std::string header_name = "Authorization";
    std::string token_prefix = "Bearer ";
};
```

**行为**: 检查 `skip_paths` → 提取 `Authorization` 头 → 去除 `Bearer ` 前缀 → 调用 `verify(token)` → 失败返回 401。

```cpp
srv.use(jwt_auth({
    .verify = [](std::string_view token) {
        return token == "my-secret-key";
    },
    .skip_paths = {"/", "/login", "/register"},
}));
```

辅助函数：`generate_secure_token(std::size_t bytes = 32) -> std::string` 生成 CSPRNG 安全令牌。

### 3. authorization — 授权

**签名**: `auto authorize(authorization_options options) -> middleware_fn`

```cpp
struct authorization_principal {
    std::string subject;
    std::string tenant_id;
    std::vector<std::string> permissions;
};

struct authorization_requirement {
    std::vector<std::string> all_of;  // 必须全部匹配
    std::vector<std::string> any_of;  // 至少匹配一个
};

struct authorization_options {
    principal_authenticator authenticate;
    authorization_requirement_resolver requirement_for;
    authenticated_principal_sink on_authenticated;
    std::function<bool(const request_context&)> skip;
};
```

支持通配符权限匹配（如 `iot:device:*`）。

```cpp
srv.use(authorize({
    .authenticate = [](request_context& ctx)
        -> std::expected<authorization_principal, authorization_error> {
        auto token = ctx.get_header("Authorization");
        if (token.empty())
            return std::unexpected(authorization_error{
                .code = authorization_error_code::unauthenticated});
        return authorization_principal{.subject = "user1",
            .permissions = {"iot:device:read", "iot:device:write"}};
    },
    .requirement_for = [](const request_context& ctx)
        -> std::optional<authorization_requirement> {
        return authorization_requirement{.all_of = {"iot:device:read"}};
    },
}));
```

### 4. rate_limiter — 速率限制

**签名**: `auto rate_limiter(rate_limiter_options opts = {}) -> http::middleware_fn`

```cpp
struct rate_limiter_options {
    double rate = 10.0;              // 令牌桶速率（req/s）
    double burst = 20.0;             // 突发容量
    std::function<std::string(http::request_context&)> key_fn;
    std::chrono::seconds entry_ttl{300};
};
```

默认按 IP 限流；自定义 `key_fn` 可按用户/API Key 限流。

```cpp
srv.use(rate_limiter({.rate = 100.0, .burst = 200.0}));
```

### 5. compress — gzip 压缩

**签名**: `auto compress(compress_options opts = {}) -> http::middleware_fn`

```cpp
struct compress_options {
    std::size_t min_size = 1024;  // 小于此值不压缩
    int level = 6;                // 压缩级别 1-9
};
```

```cpp
srv.use(compress({.min_size = 512, .level = 6}));
```

### 6. body_limit — 请求体限制

**签名**: `auto body_limit(std::size_t max_bytes = 1024 * 1024) -> http::middleware_fn`

检查 `Content-Length` 头和实际 body 大小，超限返回 413。

```cpp
srv.use(body_limit(8 * 1024 * 1024)); // 8MB
```

### 7. request_id — 请求 ID

**签名**: `auto request_id(std::string_view header_name = "X-Request-ID") -> http::middleware_fn`

请求已有 `X-Request-ID` 则复用（反向代理传入），否则生成 128 位随机 hex。Handler 通过 `ctx.resp().get_header("X-Request-ID")` 读取。

```cpp
srv.use(request_id());
```

### 8. access_log — 访问日志

**签名**:
```cpp
auto access_log(access_log_options opts, std::source_location loc = ...) -> http::middleware_fn;
auto access_log(logger::level lv = logger::level::info, std::source_location loc = ...) -> http::middleware_fn;
```

```cpp
struct access_log_options {
    logger::level lv = logger::level::info;
    access_log_format format = access_log_format::brief;
    access_log_dump dump = access_log_dump::error_only;
    bool log_request_headers = true;
    bool log_request_body = true;
    bool log_response_headers = true;
    bool log_response_body = true;
    std::size_t max_body_bytes = 2048;
    bool redact_sensitive_headers = true;
};
```

```cpp
srv.use(access_log());  // 简单用法
srv.use(access_log({.format = access_log_format::http, .dump = access_log_dump::always}));
```

### 9. metrics — 指标采集

**签名**:
```cpp
auto metrics_middleware(metrics_collector& collector) -> http::middleware_fn;
auto metrics_handler(metrics_collector& collector) -> handler_fn;
```

`metrics_collector` 采集请求总数、状态码分布、延迟直方图、响应字节数。输出 Prometheus 格式。

同时提供 `cnetmod::metrics::registry` 自定义指标：
```cpp
auto openmetrics_middleware(registry& r = global_registry(),
    std::vector<double> buckets = {...}) -> http::middleware_fn;
auto openmetrics_handler(registry& r = global_registry()) -> handler_fn;
```

```cpp
metrics_collector mc;
srv.use(metrics_middleware(mc));
// ...
r.get("/metrics", metrics_handler(mc));
```

### 10. timeout — 超时控制

**签名**: `auto request_timeout(std::chrono::steady_clock::duration max_time) -> http::middleware_fn`

软超时：handler 执行完成后检测耗时，超限则覆盖响应为 504。应放在 `recover` 之后。

```cpp
srv.use(request_timeout(std::chrono::seconds{5}));
```

### 11. graceful_shutdown — 优雅关闭

```cpp
class shutdown_handler {
    void install() noexcept;
    [[nodiscard]] auto is_signaled() const noexcept -> bool;
    [[nodiscard]] auto in_flight() const noexcept -> std::int64_t;
    template <typename SleepFn> auto wait_for_signal(SleepFn sleep_fn) -> task<void>;
    template <typename SleepFn> auto drain(SleepFn sleep_fn, std::chrono::steady_clock::duration timeout) -> task<bool>;
    auto track_middleware() -> http::middleware_fn;
};
```

注册 SIGINT/SIGTERM 信号处理器，等待 in-flight 请求完成。shutdown 后新请求返回 503。

```cpp
shutdown_handler sh;
sh.install();
srv.use(sh.track_middleware());

// 主协程
co_await sh.wait_for_signal([&](auto d) { return async_sleep(*ctx, d); });
co_await sh.drain([&](auto d) { return async_sleep(*ctx, d); }, std::chrono::seconds{5});
srv.stop();
ctx->stop();
```

### 12. ip_firewall — IP 防火墙

**签名**:
```cpp
class ip_firewall {
    explicit ip_firewall(cache::cache_store& store, ip_firewall_options opts = {});
    auto check_middleware() -> http::middleware_fn;    // 链头：封禁检查
    auto track_middleware() -> http::middleware_fn;    // 链尾：违规追踪
    auto report_violation(std::string_view ip, int weight = 1) -> task<void>;
    auto ban(std::string_view ip) -> task<void>;
    auto unban(std::string_view ip) -> task<void>;
    auto is_banned(std::string_view ip) -> task<bool>;
};
```

```cpp
struct ip_firewall_options {
    int max_violations = 10;
    std::chrono::seconds violation_window{300};
    std::chrono::seconds ban_duration{3600};
    bool track_4xx = true;
    bool track_5xx = false;
    bool track_rate_limit = true;
};
```

需要 `cache_store` 后端（`memory_cache` 或 `redis_cache`）。

```cpp
cache::memory_cache store({.max_entries = 50000});
ip_firewall fw(store, {.max_violations = 10});
srv.use(fw.check_middleware());  // 链头
// ... 其他中间件和路由 ...
srv.use(fw.track_middleware());  // 链尾
```

管理 API handler：`firewall_status_handler(fw)`, `firewall_ban_handler(fw)`, `firewall_unban_handler(fw)`。

### 13. ip_filter — IP 过滤

**签名**: `auto ip_filter(ip_filter_options opts = {}) -> http::middleware_fn`

```cpp
struct ip_filter_options {
    std::vector<std::string> allow_list;
    std::vector<std::string> deny_list;
    std::vector<std::string> trusted_proxies;
    int denied_status = http::status::forbidden;
};
```

```cpp
srv.use(ip_filter({
    .allow_list = {"127.0.0.1", "10.0.0.0/8"},
    .deny_list = {"192.168.1.100"},
}));
```

### 14. cache_store — 缓存存储

抽象接口 `cache::cache_store`，具体实现：
- `memory_cache` — 内存 LRU 缓存
- `redis_cache` — Redis 后端（需 `CNETMOD_HAS_PROTOCOL_REDIS`）

```cpp
class memory_cache : public cache_store {
    explicit memory_cache(memory_cache_options opts = {});
    auto get/set/del/exists(...) -> task<...>;
};
```

**HTTP 缓存中间件**:
```cpp
auto make_cache_middleware(cache_store& store, global_cache_options opts = {},
    cache_group_registry* registry = nullptr) -> http::middleware_fn;
```

Per-route 缓存：`cacheable()`, `cache_put()`, `cache_evict()`, `cache_evict_group()`。

```cpp
cache::memory_cache store({.max_entries = 10000});
srv.use(cache::make_cache_middleware(store, {.ttl = std::chrono::seconds{60}}));
```

### 15. health_check — 健康检查

**签名**:
```cpp
auto health_check() -> handler_fn;
auto health_check(std::function<health_status()> check_fn) -> handler_fn;
auto readiness_check(std::function<health_status()> check_fn) -> handler_fn;
auto readiness_check(std::vector<std::pair<std::string, std::function<health_status()>>> checks) -> handler_fn;
```

```cpp
r.get("/health", health_check());
r.get("/ready", readiness_check({
    {"database", [&db]() -> health_status {
        return {db.is_connected(), db.connected() ? "ok" : "disconnected"};
    }},
    {"redis", [&redis]() -> health_status {
        return {redis.ping(), "ok"};
    }},
}));
```

### 16. upload — 文件上传

**签名**: `auto upload(upload_config cfg = {}) -> http::middleware_fn`

```cpp
struct upload_config {
    std::size_t max_file_size = 10 * 1024 * 1024;  // 10MB
    std::size_t max_total_size = 0;
    std::size_t max_files = 0;
    std::size_t max_fields = 0;
    std::vector<std::string> allowed_types;
    std::vector<std::string> allowed_exts;
};
```

自动解析 `multipart/form-data` 请求体，结果可通过 `ctx.parse_form()` 访问。

```cpp
srv.use(upload({.max_file_size = 5 * 1024 * 1024, .allowed_exts = {".jpg", ".png"}}));
```

### 17. recover — panic 恢复

**签名**: `auto recover(recover_options opts = {}) -> http::middleware_fn`

```cpp
struct recover_options {
    bool log_body = false;
    std::size_t max_body_bytes = 512;
    bool allow_env_override = true;
};
```

捕获 handler 中的异常，返回 500 而不是崩溃。**应始终放在中间件链最外层**。

```cpp
srv.use(recover());
```

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| `recover()` 放在最外层 | 不放 recover 导致异常崩溃 |
| `timeout` 在 `recover` 之后 | timeout 在 recover 之前（异常无法捕获） |
| `ip_firewall` 分两段注册（check + track） | 只在链头注册 check 忘记链尾 track |
| `body_limit` 在 `upload` 之前 | upload 在 body_limit 之前（大文件先解析后拒绝） |
| 用 `health_check()` 做 K8s 探针 | 在探针 handler 中做重计算 |

## 参考示例
- `examples/http/hight_http.cpp` — 中间件链完整示例（recover + access_log + cors + request_id + body_limit）
- `examples/http/http2_demo.cpp` — HTTP/2 + 中间件组合
- `examples/http/account_server_demo.cpp` — 认证、授权、防火墙综合示例
## W3C Trace Context / OpenTelemetry bridge

Use the optional tracing middleware when an HTTP service needs standard
`traceparent` propagation. It creates one server span, preserves an incoming
trace ID, generates a fresh span ID, and exposes a completed-span callback for
an application OpenTelemetry exporter. No tracing work is performed unless the
middleware is installed.

```cpp
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace trace = cnetmod::http::tracing;

server.use(trace::tracing_middleware({
    .on_end = [](const trace::completed_span& span) {
        // Forward span to the application's OpenTelemetry exporter or logger.
        // Never throw from this callback.
    },
}));

router.get("/orders/:id", [&client](cnetmod::http::request_context& ctx)
    -> cnetmod::task<void> {
    cnetmod::http::request upstream(cnetmod::http::http_method::GET,
        "https://inventory.internal/v1/stock");

    if (auto current = trace::context_from(ctx)) {
        auto child = trace::child_context(*current);
        trace::inject(upstream, child);
    }
    auto response = co_await client.send(upstream, ctx.request_deadline());
    // Handle response...
});
```

`tracestate` is propagated only when `tracing_options::accept_tracestate` is
enabled (the default). Disable it at an untrusted boundary when vendor state
must not cross that boundary. `baggage` is intentionally not propagated by
default because it needs application-specific allowlists and size limits.

### OTLP/HTTP exporter

`cnetmod.observability.otlp` provides a bounded asynchronous OTLP/HTTP JSON
exporter. `submit()` is lock-free on the request path and never waits for the
collector. During orderly shutdown, stop accepting new spans with `close()`
and await `flush()` before stopping the `io_context`; the latter waits until
already accepted spans have either been exported or accounted for as a failed
batch.

```cpp
import cnetmod.observability.otlp;

cnetmod::observability::otlp_http_exporter exporter{
    context,
    {.endpoint = "http://otel-collector:4318/v1/traces",
     .service_name = "orders-api"},
};

srv.use(trace::tracing_middleware({
    .on_end = [&exporter](const trace::completed_span& span) {
        (void)exporter.submit(span); // bounded queue: drops are counted
    },
}));

// Service shutdown coroutine, before context.stop().
exporter.close();
if (auto flushed = co_await exporter.flush(std::chrono::seconds{5}); !flushed) {
    // Use the application's cnetmod logger to record the timeout/failure.
}
```

The exporter deliberately has no retry loop on the request path. A collector
outage increments `failed_batches`; applications can inspect `statistics()` to
alert on drops or failed deliveries without amplifying an outage with retries.

### HTTP → SQL / Redis / gRPC trace chain

Trace context is an explicit value rather than thread-local state, so it is
safe across coroutine worker migration. Pass the handler's context to each
downstream operation; SQL and Redis create local child spans and report them to
the same bounded exporter, while gRPC also injects W3C headers for the remote
service.

```cpp
if (auto parent = trace::context_from(ctx)) {
    auto report = [&exporter](const trace::completed_span& span) {
        (void)exporter.submit(span);
    };

    auto user = co_await session.query(
        "SELECT id, name FROM users WHERE id = ?", *parent, report);
    auto cached = co_await redis.cmd({"GET", "user:42"}, *parent, report);

    grpc::unary_request rpc{/* ... */};
    auto profile = co_await grpc_client.unary(std::move(rpc), *parent);
}
```

SQL and Redis do not define a `traceparent` wire field, so their child spans
remain local telemetry. gRPC transmits the child `traceparent` and
`tracestate`; HTTP uses `trace::inject()` for the same purpose. Exporter
callbacks are isolated from business results: an exception in a callback is
ignored and never changes the database or Redis operation outcome.
<!-- END SOURCE: skill/http/http-middleware.md -->

<!-- BEGIN SOURCE: skill/http/http-server.md -->
# Source: `skill/http/http-server.md`

# HTTP Server

> 高性能异步 HTTP/HTTPS 服务器，支持路由、中间件、SSE、Swagger、HTTP/2 与文件上传。

**import**: `import cnetmod.protocol.http;`
**CMake**: `-DCNETMOD_ENABLE_HTTP=ON`
**源码**: `src/protocol/http/`

## 场景导航
- 我要启动一个 HTTP 服务器 → [看这里](#创建并启动服务器)
- 我要注册路由 → [看这里](#路由注册)
- 我要处理请求参数 → [看这里](#request_context--请求访问)
- 我要返回 JSON/HTML/文本 → [看这里](#response--响应构建)
- 我要推送实时事件 (SSE) → [看这里](#sse-server-sent-events)
- 我要生成 API 文档 → [看这里](#swaggeropenapi-文档)
- 我要处理文件上传 → [看这里](#multipartform-data--文件上传)
- 我要设置 Cookie → [看这里](#cookie-处理)
- 我要启用 HTTP/2 → [看这里](#http2-支持)
- 我要升级为 WebSocket → [参见 websocket.md](websocket.md)

## API 参考

### 创建并启动服务器

#### `server::server`
**签名**: `explicit server(io_context& ctx)` / `explicit server(server_context& sctx)`
**参数**:
- `ctx` — 单线程 I/O 上下文
- `sctx` — 多核服务器上下文（多线程模式）

#### `server::listen`
**签名**: `auto listen(std::string_view host, std::uint16_t port, socket_options opts = {.reuse_address = true}) -> std::expected<void, std::error_code>`
**参数**:
- `host` — 监听地址（如 `"0.0.0.0"`）
- `port` — 监听端口
- `opts` — 套接字选项

#### `server::set_router`
**签名**: `void set_router(router r)`

#### `server::use`
**签名**: `void use(middleware_fn mw)`
**说明**: 添加中间件，按调用顺序构成中间件管道。

#### `server::run`
**签名**: `auto run() -> task<void>`
**说明**: 启动接受循环，开始处理连接。

#### `server::stop`
**签名**: `void stop()`
**生命周期**: 在 accept 事件循环上调用，取消挂起的 accept。调用后必须继续运行
事件循环，直到持有的 `run()` task 完成，才能销毁 server/事件循环。
`stop()` 不等于连接排空；已经接受的连接仍须独立等待完成，不能调用后立即停止 I/O。
连接超限时，接收循环发送 429 后半关闭发送方向，并读取丢弃对端剩余数据直到 EOF。
发送和收尾共用 1 秒取消预算，`stop()` 可提前取消。等待 I/O 与定时器结束后才关闭
socket、复用接收取消令牌。这避免立即关闭未读请求使 Windows 客户端丢失 429。
收尾当前占用接收循环，超限对端不关闭时最多消耗上述预算；不改变正常获准连接的路径。

获准连接使用 `spawn_guarded` 派发：未被中间件处理的连接异常会结束该连接，记录错误
类别和数值，不记录异常文本，也不再经 detached promise 终止进程。包装协程创建前的
分配失败仍可向接收循环传播。此隔离机制与 OTEL 开关无关，不替代连接任务的显式等待
或 handler 的取消契约。

#### `server::abort_connections`
**签名**: `void abort_connections() noexcept`
**说明**: 停止接收后，可中止现有及已投递连接的 socket 读写。此操作对该 server
实例不可撤销，可重复调用；采用 socket shutdown，不销毁挂起的协程。
必须继续运行 worker 事件循环直到连接完成。它不能取消 handler 内任意非 socket
等待，也不能替代应用层取消协议。`active_connections()` 包含已投递但尚未执行的连接。

#### `server::set_max_connections`
**签名**: `void set_max_connections(std::size_t n)`

**示例**:
```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

auto main() -> int {
    auto ctx = make_io_context();

    router r;
    r.get("/", [](request_context& ctx) -> task<void> {
        ctx.text(status::ok, "Hello cnetmod!");
        co_return;
    });

    server srv(*ctx);
    auto result = srv.listen("0.0.0.0", 8080);
    if (!result) {
        std::println("Listen failed: {}", result.error().message());
        return 1;
    }
    srv.set_router(std::move(r));

    spawn(*ctx, srv.run());
    ctx->run();
}
```

---

### 路由注册

#### `router::get / post / put / del / patch / any`
**签名**:
```cpp
auto get(std::string_view pattern, handler_fn fn) -> router&;
auto post(std::string_view pattern, handler_fn fn) -> router&;
auto put(std::string_view pattern, handler_fn fn) -> router&;
auto del(std::string_view pattern, handler_fn fn) -> router&;
auto patch(std::string_view pattern, handler_fn fn) -> router&;
auto any(std::string_view pattern, handler_fn fn) -> router&;
```
**参数**:
- `pattern` — 路由模式，支持 `:name` 命名参数和 `*filepath` 通配符
- `fn` — 处理函数 `std::function<task<void>(request_context&)>`

**路由模式说明**:
| 模式 | 示例路径 | 说明 |
|------|---------|------|
| `/api/users` | `/api/users` | 精确匹配 |
| `/api/users/:id` | `/api/users/42` | 命名参数 |
| `/api/users/:id/posts/:pid` | `/api/users/7/posts/99` | 多命名参数 |
| `/static/*filepath` | `/static/css/main.css` | 通配符 |

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

router r;

r.get("/api/users/:id", [](request_context& ctx) -> task<void> {
    auto id = ctx.param("id");
    ctx.json(status::ok,
        std::format(R"({{"id":{},"name":"User_{}"}})", id, id));
    co_return;
});

r.post("/api/echo", [](request_context& ctx) -> task<void> {
    ctx.text(status::ok, std::format("Echo: {}", ctx.body()));
    co_return;
});

r.del("/api/users/:id", [](request_context& ctx) -> task<void> {
    auto id = ctx.param("id");
    ctx.json(status::ok, std::format(R"({{"deleted":{}}})", id));
    co_return;
});

// 通配符路由
r.get("/static/*filepath", [](request_context& ctx) -> task<void> {
    auto path = ctx.wildcard();
    ctx.text(status::ok, std::format("File: {}", path));
    co_return;
});
```

---

### `request_context` — 请求访问

#### `request_context::method`
**签名**: `[[nodiscard]] auto method() const noexcept -> std::string_view`
**返回**: HTTP 方法字符串（如 `"GET"`, `"POST"`）

#### `request_context::path`
**签名**: `[[nodiscard]] auto path() const noexcept -> std::string_view`
**返回**: 请求路径（不含查询字符串）

#### `request_context::query_string`
**签名**: `[[nodiscard]] auto query_string() const noexcept -> std::string_view`

#### `request_context::uri`
**签名**: `[[nodiscard]] auto uri() const noexcept -> std::string_view`
**返回**: 完整 URI（含路径和查询字符串）

#### `request_context::param`
**签名**: `[[nodiscard]] auto param(std::string_view name) const noexcept -> std::string_view`
**参数**: `name` — 路由中 `:name` 定义的参数名

#### `request_context::wildcard`
**签名**: `[[nodiscard]] auto wildcard() const noexcept -> std::string_view`

#### `request_context::get_header`
**签名**: `[[nodiscard]] auto get_header(std::string_view key) const -> std::string_view`

#### `request_context::headers`
**签名**: `[[nodiscard]] auto headers() const noexcept -> const header_map&`

#### `request_context::body`
**签名**: `[[nodiscard]] auto body() const -> std::string_view`

#### `request_context::parse_form`
**签名**: `[[nodiscard]] auto parse_form() -> std::expected<const form_data*, std::error_code>`
**说明**: 解析 `multipart/form-data` 或 `application/x-www-form-urlencoded` 请求体。

#### `request_context::resp`
**签名**: `[[nodiscard]] auto resp() noexcept -> response&`
**说明**: 获取底层 response 对象，用于高级操作（如设置 trailer、自定义 header）。

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto handler = [](request_context& ctx) -> task<void> {
    auto method = ctx.method();           // "POST"
    auto path   = ctx.path();             // "/api/data"
    auto query  = ctx.query_string();     // "page=1&size=10"
    auto id     = ctx.param("id");        // 路由参数
    auto token  = ctx.get_header("Authorization");
    auto body   = ctx.body();

    ctx.json(status::ok, R"({"ok":true})");
    co_return;
};
```

---

### `response` — 响应构建

#### `request_context::text`
**签名**: `void text(int status_code, std::string_view text_body)`

#### `request_context::json`
**签名**: `void json(int status_code, std::string_view json_body)`

#### `request_context::html`
**签名**: `void html(int status_code, std::string_view html_body)`

#### `request_context::redirect`
**签名**: `void redirect(std::string_view location, int code = 302)`

#### `request_context::not_found`
**签名**: `void not_found()`

#### `response::set_cookie`
**签名**:
```cpp
auto set_cookie(std::string_view name, std::string_view value,
    std::string_view domain = {}, std::string_view path = "/",
    std::optional<std::chrono::seconds> max_age = std::nullopt,
    bool secure = false, bool http_only = false) -> response&;

auto set_cookie(const cookie& c) -> response&;
```

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto handler = [](request_context& ctx) -> task<void> {
    // 设置 Cookie
    ctx.resp().set_cookie("session", "abc123", {}, "/",
        std::chrono::hours(24), false, true);

    ctx.json(status::ok, R"({"logged_in":true})");
    co_return;
};
```

---

### SSE (Server-Sent Events)

#### `request_context::sse_begin`
**签名**: `auto sse_begin(int status_code = status::ok) -> task<bool>`

#### `request_context::sse_started` / `sse_state`
**签名**:
```cpp
[[nodiscard]] auto sse_started() const noexcept -> bool;
[[nodiscard]] auto sse_state() const noexcept -> sse_stream_state;
```
**说明**: `sse_started()` 在开始尝试写出 SSE 响应头时即返回 true，包括部分写入后失败的情况；此后不得回退为普通 JSON 响应。`sse_state()` 区分 `not_started`、`committing`、`open`、`failed` 与 `closed`。

#### `request_context::sse_send`
**签名**: `auto sse_send(std::string_view data, std::string_view event = {}) -> task<bool>`

#### `request_context::sse_json`
**签名**: `auto sse_json(std::string_view json_payload, std::string_view event = {}) -> task<bool>`

#### `request_context::sse_comment` / `sse_heartbeat`
**签名**:
```cpp
auto sse_comment(std::string_view comment) -> task<bool>;
auto sse_heartbeat() -> task<bool>;
```
**说明**: 发送标准 SSE 注释帧。`sse_heartbeat()` 发送 `: keepalive\n\n`，不会被编码成 `data:` 事件。

#### `request_context::sse_done`
**签名**: `auto sse_done() -> task<bool>`

#### `request_context::with_sse`

**签名**:
```cpp
auto with_sse(sse_handler_fn handler,
    sse_stream_options options = {}) -> task<void>;
```

当是否启用 SSE 必须在请求期间决定时，先完成鉴权、参数解析和资源存在性检查；只有确认
进入流式响应后才调用 `with_sse()`。在调用前仍可返回普通 HTTP 4xx/5xx；调用后由框架以
结构化并发同时运行 stream handler 和总时限看门狗，并在 handler 结束时取消、等待看门狗，
不会留下失管协程。

```cpp
routes.post("/chat", [](http::request_context& request) -> task<void> {
    auto session = co_await find_session(request.param("id"));
    if (!session) {
        request.not_found();
        co_return;
    }
    if (request.query_string() != "stream=true") {
        request.json(http::status::ok, render_response(*session));
        co_return;
    }

    co_await request.with_sse(
        [session = std::move(*session)](http::request_context&,
            http::sse_stream& stream) mutable -> task<void> {
            co_await stream.send(render_delta(session), "delta");
            co_await stream.finish();
        },
        {.max_duration = std::chrono::seconds{60},
            .write_timeout = std::chrono::seconds{3}});
});
```

#### `sse_stream`

`sse_stream` 是绑定 `request_context` 的高层流对象，统一管理保守的开始状态、惰性
响应头写出、具名事件、注释、心跳和终止帧。应用负责序列化 payload，框架负责 SSE
帧编码和 socket 写出。

```cpp
routes.sse_post("/chat", [](http::request_context& request,
                            http::sse_stream& stream) -> task<void> {
    auto deltas = stream.callback("delta");
    if (!co_await stream.begin())
        co_return;
    if (!co_await deltas(R"({"text":"first"})"))
        co_return;
    co_await stream.send(R"({"result":"complete"})", "done");
    co_await stream.finish();
}, http::sse_stream_options{
    .max_duration = std::chrono::seconds{60},
    .write_timeout = std::chrono::seconds{3},
});
```

`started()` 在响应头开始提交时即返回 true，包括提交失败；一旦为 true，不得回退普通
HTTP 响应。`callback(event)` 借用流对象，不能超过 route handler、`sse_stream` 或请求
上下文的生命周期。`router::sse_get()` 和 `router::sse_post()` 会为每个请求创建独立流对象，
并在内部复用 `request_context::with_sse()`；它们适用于注册时即可确定为 SSE 的独立端点。
运行时才决定是否流式的同路径接口必须使用 `with_sse()`，不要只构造 `sse_stream`，否则
没有结构化的总时限看门狗。Application 的 recover 中间件发现 SSE 已
提交后不会再尝试普通 JSON 响应，而是尽力写出具名 `error` 帧和终止帧；业务可在异常前
自行写出更具体的错误契约。

`sse_stream_options::max_duration` 限制整条流的绝对生命周期，默认 120 秒；
`write_timeout` 限制每次响应头或事件帧的写出时间，默认 5 秒。二者必须为正数。超时会取消
当前 socket 操作、关闭连接并将状态置为 `failed`。普通 `request_timeout` 中间件检测到 SSE
已提交后不会写入 504；SSE 使用自己的总时限。生产者访问下游时应使用
`request_context::with_deadline()`，让下游操作同样受总时限和请求取消约束。

#### `sse::event`
```cpp
struct event {
    std::string event, data, id;
    std::optional<std::chrono::milliseconds> retry;
    std::string comment;
};
```

#### `sse::prepare`
**签名**: `void prepare(response&, response_options opts = {})`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

r.get("/events", [](request_context& ctx) -> task<void> {
    co_await ctx.sse_begin();
    co_await ctx.sse_heartbeat();
    for (int i = 0; i < 5; ++i) {
        auto ok = co_await ctx.sse_send(
            std::format("message {}", i), "update");
        if (!ok) break;
    }
    co_await ctx.sse_done();
});
```

---

### Swagger/OpenAPI 文档

#### `openapi_document`
```cpp
struct openapi_document {
    std::string title = "cnetmod API";
    std::string version = "1.0.0";
    std::string description;
    std::vector<openapi_server> servers;
    std::map<std::string, std::map<std::string, openapi_operation>> paths;
};
```

#### `openapi_json_handler`
**签名**: `[[nodiscard]] auto openapi_json_handler(openapi_document doc) -> handler_fn`
**说明**: 生成返回 OpenAPI JSON 的路由处理器。

#### `swagger_ui_handler`
**签名**: `[[nodiscard]] auto swagger_ui_handler(std::string openapi_url = "/openapi.json", std::string title = "API Docs") -> handler_fn`
**说明**: 生成 Swagger UI HTML 页面的路由处理器。

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

openapi_document doc;
doc.title = "My API";
doc.version = "1.0.0";
doc.servers.push_back({.url = "http://localhost:8080", .description = "dev"});

add_operation(doc, http_method::GET, "/api/users", {
    .tags = {"users"},
    .summary = "List all users",
});

r.get("/openapi.json", openapi_json_handler(std::move(doc)));
r.get("/docs", swagger_ui_handler());
```

---

### HTTP/2 支持

服务器自动检测 TLS 连接上的 ALPN 协商，透明支持 HTTP/2（RFC 9113）。客户端通过 `h2` 前缀连接时，服务器使用 `v2::session` 处理帧和多路复用。

**配置**: 启用 SSL 后，HTTP/2 自动通过 ALPN 协商。无需额外配置。

```cpp
import std;
import cnetmod.protocol.http;

// 服务器同时支持 HTTP/1.1 和 HTTP/2
// curl --http2 -k https://localhost:8443/
// curl --http1.1 -k https://localhost:8443/
```

---

### multipart/form-data — 文件上传

#### `form_data`
```cpp
class form_data {
    auto field(std::string_view name) const -> std::optional<std::string_view>;
    auto file(std::string_view name) const -> const form_file*;
    auto all_files() const noexcept -> const std::vector<form_file>&;
};
```

#### `multipart_builder`
```cpp
class multipart_builder {
    auto add_field(std::string_view name, std::string_view value) -> multipart_builder&;
    auto add_file(std::string_view field_name, std::string_view filename,
        std::string_view content_type, std::string_view data) -> multipart_builder&;
    [[nodiscard]] auto content_type() const -> std::string;
    [[nodiscard]] auto build() const -> std::string;
};
```

#### `save_upload`
**签名**: `auto save_upload(upload_options opts) -> handler_fn`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

r.post("/upload", [](request_context& ctx) -> task<void> {
    auto form = ctx.parse_form();
    if (!form) {
        ctx.json(status::bad_request, R"({"error":"invalid form"})");
        co_return;
    }
    for (auto& f : (*form)->all_files()) {
        std::println("file: {} ({} bytes)", f.filename, f.size());
    }
    ctx.json(status::ok, R"({"uploaded":true})");
});

// 或使用内置保存处理器
r.post("/save", save_upload({
    .save_dir = "uploads",
    .default_filename = "upload.bin",
    .max_size = 32 * 1024 * 1024,
}));
```

---

### 静态文件服务

#### `serve_dir`
**签名**: `auto serve_dir(static_file_options opts) -> handler_fn`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

router r;
r.get("/static/*filepath", serve_dir({
    .root = "./public",
    .index_file = "index.html",
}));
```

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 先 `use()` 注册中间件，再 `set_router()` | 在 `run()` 之后注册路由 |
| 用 `ctx.json()` 返回 JSON | 手动拼接 `Content-Type` header |
| 使用 `save_upload()` 处理大文件上传 | 在 handler 中手动读取整个 body 到内存 |
| SSE 时使用 `sse_begin()`、`sse_send()` 与 `sse_heartbeat()` | `sse_started()` 后回退使用 `ctx.json()` |
| 使用 `co_return` 结束 handler | 忘记 `co_return` 导致未定义行为 |

## 多核服务器部署（生产级用法）

### `server_context` — 多核上下文

**签名**（`cnetmod.executor.pool`）：
```cpp
class server_context {
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    [[nodiscard]] auto accept_io() noexcept -> io_context&;
    [[nodiscard]] auto next_worker_io() noexcept -> io_context&;
    [[nodiscard]] auto worker_count() const noexcept -> unsigned;
    [[nodiscard]] auto worker_ios() -> std::vector<io_context*>;
    [[nodiscard]] auto pool() noexcept -> thread_pool&;

    template <typename F>
    auto offload(io_context& return_to, F&& fn);

    void spawn_next(task<void> t);
    void run();   // 阻塞：启动 worker 线程，当前线程运行 accept_io
    void stop();
};
```

**架构**：
| 线程 | 角色 | 说明 |
|------|------|------|
| Thread 0（main） | `accept_io()` | 专用 accept 循环，不参与请求处理 |
| Thread 1..N | `next_worker_io()` | 每个 worker 独立 `io_context`，round-robin 分配连接 |
| Thread Pool | `pool()` | cnetmod `thread_pool`，用于 CPU 密集型任务卸载 |

**IOCP 特性**：accept 后的新 socket 尚未关联 IOCP，首次 `async_read/write` 时自动绑定到 worker 的 IOCP。

---

### 多核 HTTP 服务器完整示例

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.access_log;
import cnetmod.protocol.http.middleware.recover;
import cnetmod.protocol.http.middleware.cors;
import cnetmod.protocol.http.middleware.request_id;
import cnetmod.protocol.http.middleware.body_limit;

namespace cn = cnetmod;
namespace http = cnetmod::http;

auto main() -> int {
    cn::net_init net;

    // 创建多核上下文：4 worker 线程 + 4 个 CPU 线程
    constexpr unsigned WORKERS = 4;
    cn::server_context sctx(WORKERS, WORKERS);

    // 构建路由
    http::router router;

    router.get("/", [](http::request_context& ctx) -> cn::task<void> {
        ctx.json(http::status::ok, std::format(
            R"({{"message":"Hello from multi-core!","thread":"{}"}})",
            std::this_thread::get_id()));
        co_return;
    });

    router.get("/api/users/:id", [](http::request_context& ctx) -> cn::task<void> {
        auto id = ctx.param("id");
        ctx.json(http::status::ok, std::format(
            R"({{"id":{},"name":"User_{}"  }})", id, id));
        co_return;
    });

    // CPU 密集型路由：卸载到 cnetmod 线程池
    router.get("/compute/:n", [&sctx](http::request_context& ctx) -> cn::task<void> {
        int n = 30;
        auto n_str = ctx.param("n");
        if (!n_str.empty())
            std::from_chars(n_str.data(), n_str.data() + n_str.size(), n);

        auto io_tid = std::this_thread::get_id();

        // 切换到 cnetmod 线程池执行 CPU 密集计算
        co_await cn::pool_post_awaitable{sctx.pool()};
        auto pool_tid = std::this_thread::get_id();

        // 模拟计算
        std::uint64_t fib = 0, a = 0, b = 1;
        for (int i = 2; i <= n; ++i) { auto c = a + b; a = b; b = c; }
        fib = (n <= 1) ? static_cast<std::uint64_t>(n) : b;

        // 切回 worker io_context 线程响应
        co_await cn::post_awaitable{ctx.io_ctx()};

        ctx.json(http::status::ok, std::format(
            R"({{"n":{},"fibonacci":{},"io_thread":"{}","pool_thread":"{}"}})",
            n, fib, io_tid, pool_tid));
        co_return;
    });

    // 创建多核 HTTP 服务器
    http::server srv(sctx);
    auto listen_r = srv.listen("0.0.0.0", 8080);
    if (!listen_r) {
        std::println("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    // 注册中间件（顺序：recover → access_log → cors → request_id → body_limit）
    srv.use(cn::recover());
    srv.use(cn::access_log());
    srv.use(cn::cors());
    srv.use(cn::request_id());
    srv.use(cn::body_limit(2 * 1024 * 1024));
    srv.set_router(std::move(router));

    // 在 accept_io 上启动 accept 循环
    cn::spawn(sctx.accept_io(), srv.run());

    std::println("Multi-core server on 0.0.0.0:8080 ({} workers)", WORKERS);

    // 阻塞：当前线程运行 accept_io，后台启动 N 个 worker 线程
    sctx.run();
    return 0;
}
```

### Do's & Don'ts（多核模式）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用 `server_context` 构造 `server` 启用多核 | 在多核场景手动创建多个 `server` 实例 |
| CPU 密集任务通过 `pool_post_awaitable` 卸载到 `pool()` | 在 handler 中直接执行耗时计算阻塞 worker |
| 卸载后用 `post_awaitable{ctx.io_ctx()}` 切回 worker | 在 pool 线程上直接调用 `ctx.json()` |
| `spawn(sctx.accept_io(), srv.run())` 启动 accept | 在 worker 线程上运行 accept 循环 |

---

## 参考示例
- `examples/http/hight_http.cpp` — 路由、中间件、文件上传完整示例
- `examples/http/http_demo.cpp` — 底层 HTTP 请求/响应解析
- `examples/http/hight_plus_http.cpp` — 高级功能（Cookie、SSE 等）
- `examples/http/http2_demo.cpp` — HTTP/2 TLS + ALPN 示例
- `examples/http/websocket_upgrade_demo.cpp` — HTTP 升级至 WebSocket
- `examples/http/multicore_http.cpp` — 多核 server_context + pool 卸载完整示例
- `examples/http/tfb_benchmark.cpp` — TechEmpower 基准测试（多核 + 数据库连接池）
<!-- END SOURCE: skill/http/http-server.md -->

<!-- BEGIN SOURCE: skill/http/http3-quic.md -->
# Source: `skill/http/http3-quic.md`

# HTTP/3 / QUIC

基于 UDP、QUIC 和 TLS 1.3 的 HTTP/3 客户端与服务端，使用 BoringSSL QUIC API。

```cpp
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.server;
import cnetmod.coro;
```

配置需要：

```text
-DCNETMOD_ENABLE_SSL=ON
-DCNETMOD_ENABLE_QUIC=ON
-DCNETMOD_ENABLE_BORINGSSL_QUIC=ON
```

`CNETMOD_ENABLE_QUIC` 需要 BoringSSL QUIC 后端；不要用 OpenSSL 代替它。

## 使用边界

- HTTP/3 listener 使用 UDP，不能与 HTTP/1.1/HTTP/2 的 TCP listener 混用。
- HTTP/3 使用 ALPN `h3`；生产环境保持 `verify_certificate = true`。
- 同一个 QUIC 连接可以并发承载多个 HTTP/3 request stream。
- `RESET_STREAM`、`STOP_SENDING` 或连接关闭会取消异步 handler 的 token；handler
  必须把 token 继续传给 MySQL、Redis、gRPC 等下游 I/O，不能阻塞线程。

## 服务端

```cpp
import std;
import cnetmod.core.address;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.protocol.http.v3.server;

namespace cn = cnetmod;
namespace h3 = cn::http::v3;

auto server = h3::make_http3_server(io, tls,
    cn::endpoint{cn::ipv4_address::any(), 443},
    [](h3::http3_request& request,
        h3::http3_response& response) -> std::error_code {
        if (request.path != "/health") {
            response.status = cn::http::status::not_found;
            response.body = R"({"code":404})";
            return {};
        }
        response.headers.emplace("content-type", "application/json");
        response.body = R"({"ok":true})";
        return {};
    });
```

同步 handler 适合纯内存、快速响应；涉及数据库、缓存或 RPC 时使用协程 handler：

```cpp
h3::async_server_request_handler handler =
    [&profiles](h3::http3_request& request, h3::http3_response& response,
        cn::cancel_token& token) -> cn::task<std::expected<void, std::error_code>> {
        auto profile = co_await profiles.fetch(request.path, token);
        if (!profile)
            co_return std::unexpected(profile.error());
        response.status = cn::http::status::ok;
        response.headers.emplace("content-type", "application/json");
        response.body = encode_profile_json(*profile);
        co_return {};
    };
```

### 请求体流式处理

普通 `async_server_request_handler` 保持兼容语义：handler 启动时
`request.body` 已经完整。大文件上传使用显式 `streaming_server_request_handler`；
它在 HEADERS 后启动，并通过有界 `request_body_stream` 提供背压：

```cpp
h3::streaming_server_request_handler handler =
    [](h3::http3_request& request, h3::http3_response& response,
        cn::http::request_body_stream& body, cn::cancel_token& token)
        -> cn::task<std::expected<void, std::error_code>> {
        while (!token.is_cancelled()) {
            auto chunk = co_await body.receive();
            if (!chunk)
                break; // peer FIN、取消或 stream 错误
            process_chunk(*chunk);
        }
        response.status = cn::http::status::ok;
        co_return {};
    };
```

handler 提前返回会关闭 body pump；消费者慢时，传输层不会无限制缓存上传数据。

### 响应体流式发送

服务端可设置 `response.body_source`。服务端先发送 HEADERS，再按 producer 发送
DATA，最后发送 trailers 和 FIN：

```cpp
auto index = std::make_shared<std::size_t>(0);
response.body_source = std::make_shared<cn::http::response_body_source>(
    [index](cn::cancel_token& token)
        -> cn::task<std::optional<cn::http::request_body_chunk>> {
        if (token.is_cancelled() || *index == 3)
            co_return std::nullopt;
        constexpr std::array parts{"part-a", "part-b", "part-c"};
        const auto text = parts[(*index)++];
        cn::http::request_body_chunk chunk;
        chunk.insert(chunk.end(),
            reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data()) + text.size());
        co_return chunk;
    }, 18);
```

`body` 与 `body_source` 不能同时设置。声明的长度必须与实际发送长度一致；HEAD、
1xx、204、304 响应禁止发送 DATA。

### 显式 Server Push

Server Push 默认关闭，客户端必须在 `http3_client_options` 中设置 `max_push_id`，并提供
`on_server_push` 回调。服务端把 promise 与响应放进父响应的 `pushes`；promise 在父响应
HEADERS 前发送，push 响应可使用静态 `body` 或带背压的 `body_source`。回调只在完整 push
响应通过 QPACK、Content-Length 和 trailers 语义校验后执行。

```cpp
options.max_push_id = 0U;
options.on_server_push = [](h3::http3_request promise,
    h3::http3_response response)
    -> cn::task<std::expected<void, std::error_code>> {
    cache_asset(promise.path, response.body);
    co_return {};
};

h3::http3_push asset;
asset.request.method = cn::http::http_method::GET;
asset.request.scheme = "https";
asset.request.host = request.host;
asset.request.path = "/app.js";
asset.response = std::make_shared<h3::http3_response>(
    h3::http3_response{.status = 200, .body = "..."});
response.pushes.push_back(std::move(asset));
```

客户端可在 Push body producer 开始前检查 `PUSH_PROMISE`。回调返回错误会自动
发送 RFC 9114 `CANCEL_PUSH`，不会影响父响应；需要自行决定时，可保存回调给出的
`push_id` 并调用 `client.cancel_server_push(push_id)`。取消同时会停止本地交付，并
取消服务端 `body_source` 的 token。

```cpp
options.max_push_id = 8U;
options.on_server_push_promise = [](h3::server_push_promise promise)
    -> cn::task<std::expected<void, std::error_code>> {
    if (!cache_wants(promise.request.path))
        co_return std::unexpected(std::make_error_code(
            std::errc::operation_canceled)); // automatic CANCEL_PUSH
    co_return {};
};
```

## 客户端

```cpp
h3::http3_client client(io, tls, {
    .connect_timeout = std::chrono::seconds{5},
    .request_timeout = std::chrono::seconds{30},
    .verify_certificate = true,
    .tls_sni_host = "api.example.com",
});

if (auto connected = co_await client.connect("api.example.com", 443); connected) {
    h3::http3_request request;
    request.method = cn::http::http_method::GET;
    request.scheme = "https";
    request.host = "api.example.com";
    request.path = "/health";
    if (auto response = co_await client.send_request(request); response)
        std::println("HTTP/3 status={}, body={}", response->status, response->body);
}
co_await client.close();
```

普通 `send_request()` 在返回前收集完整 `response.body`，保持旧 API 和小响应热路径。

### 客户端响应体流式消费

大响应或文件下载使用显式 `send_request_streaming()`。收到 HEADERS 后立即调用
handler，DATA 通过有界 `request_body_stream` 交付；消费者变慢会形成 QUIC 背压：

```cpp
h3::http3_request request;
request.host = "api.example.com";
request.port = 443;
request.path = "/large-export";

std::uint64_t bytes = 0;
auto result = co_await client.send_request_streaming(
    request,
    [&bytes](h3::http3_response& response,
        cn::http::request_body_stream& body, cn::cancel_token& token)
        -> cn::task<std::expected<void, std::error_code>> {
        if (response.status != 200)
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        while (!token.is_cancelled()) {
            auto chunk = co_await body.receive();
            if (!chunk)
                break;
            bytes += chunk->size();
            consume_chunk(*chunk);
        }
        co_return {};
    });
```

`Content-Length` 会逐块校验，trailer 保留在 `result->trailers`。handler 返回错误或
取消后，当前 stream 会被取消；流式 handler 不会在 0-RTT 被拒绝后自动重放，避免
应用已经消费响应前缀后再次执行回调。

### deadline 与取消

```cpp
cn::cancel_token token;
auto by_token = co_await client.send_request(request, token);
auto by_deadline = co_await client.send_request(request,
    cn::deadline::after(std::chrono::seconds{2}));
```

deadline 会发送 QUIC `RESET_STREAM` / `STOP_SENDING`，连接本身仍可复用。普通
`send_request(request)` 不注册取消回调，适合高并发热路径。

## WebTransport 与 HTTP Datagram

WebTransport 需要两端同时启用 QUIC Datagram、Extended CONNECT 和 WebTransport
SETTINGS。服务端使用 `async_webtransport_handler`；客户端调用
`connect_webtransport()`，然后通过 `webtransport_session` 管理双向/单向子流、
Datagram 和 `close()`。不要直接调用底层 QUIC Datagram API 绕过 session context ID。

## 0-RTT 与持久化

客户端在 TLS 实际安装 0-RTT write key 后才会创建 HTTP/3 控制、QPACK 和 request
streams；这避免 Initial flight 与应用帧抢占同一个握手阶段。该等待不需要应用层
轮询，也不会把 0-RTT 降级为先完成 1-RTT 再发送。

握手完成后延迟抵达的 QUIC Initial/Handshake UDP 包会按 RFC 9001 丢弃；它们不能
重新初始化 TLS 或覆盖已建立的 1-RTT 状态。对于被 replay cache 拒绝的 0-RTT，
仅幂等的完整响应 API 会在新的 1-RTT 连接上重放一次。

客户端可以通过 `resumption_ticket` 和 `enable_early_data` 显式启用受控 0-RTT；
自动重试仅适用于幂等请求。服务端必须配置应用拥有的 ticket AEAD 与共享 replay
cache，默认拒绝 0-RTT。Alt-Svc、ticket 文件均为 opt-in，并应放在权限受限目录。

## 性能与验证

1. 复用同一 origin 的 `http3_client`，避免每个请求重新握手。
2. 控制 header 体积；默认 QPACK 动态表为 64 KiB、阻塞流为 100。
3. 使用 `send_batch` 时同一连接上的 request stream 会并发提交；冷连接只串行化
   初始连接建立。
4. HTTP/3 E2E 覆盖 GET/POST、请求/响应流式传输、HEAD/OPTIONS、batch、取消、
   `close_async()`、0-RTT ticket 和 trailer/Content-Length 语义。

## 连接迁移与 Multipath 边界

当前实现保留 RFC 9000 的单活跃路径迁移，并提供受显式配置保护的
`draft-ietf-quic-multipath-12` 实验实现。它不是最终 RFC，浏览器和通用 QUIC 对端
通常不会协商它；未同时启用的对端始终走原有单路径语义。

Multipath 已实现并只在双端均宣告 `initial_max_path_id` 后启用：

- 每个 Path ID 独立持有 1-RTT packet number、ACK、RTT/loss、拥塞控制与 pacing；
  AEAD nonce 也包含 Path ID，Path 0 保持 RFC 9001 既有字节语义。
- 服务端对未验证的非零路径执行逐路径 3× anti-amplification 额度；PATH_CHALLENGE
  与 PATH_RESPONSE 同样受该额度约束，验证完成后才解除。
- 短头包在认证前以本地 DCID 选择路径；每条路径的 CID 序列独立，支持
  `PATH_NEW_CONNECTION_ID`、`PATH_RETIRE_CONNECTION_ID`、`MAX_PATH_ID`、
  `PATH_ACK`、状态帧和 `PATH_ABANDON` 的控制面规则。
- `quic_connection::async_probe_path(path_id, endpoint)` 只在匹配 CID 已交换后发起
  PATH_CHALLENGE，并在最多 3 PTO 内对同一 challenge 重传；其 task 只会在收到匹配的
  PATH_RESPONSE 后成功，超时返回 `timed_out`。PATH_RESPONSE 前不会在新路径调度应用数据。验证成功后调度器在
  可用、非 backup 路径间轮转；备路径仍可作为故障切换候选。`async_abandon_path()`
  发送 PATH_ABANDON、退役该路径的 peer CID，并回退到仍存活的路径；本地 CID 路由和
  未确认包保留 3 PTO 后才回收，期间未确认应用帧会重传到存活路径。
- `PATH_ACK` 始终携带被确认的 Path ID。若该路径已进入 `PATH_ABANDON` 的 3 PTO
  回收期，确认帧会由另一条存活、已验证路径承载，避免对端仅因路径切换而等待 PTO
  才释放可确认数据。
- 若当前路径的拥塞窗口暂时不足，发送器会在同一轮尝试其余已验证路径；帧会保留原有
  RFC 9218 优先级，不会因路径切换降级为 FIFO。
- 对端的 early `PATH_ABANDON` 会建立不可复用的路径 tombstone 并回送确认；迟到的
  PATH CID/ACK 帧不会重新打开路径或关闭整个连接。Path ID 的消费状态独立于 3 PTO
  资源回收而永久保留，符合 nonce 不可复用要求。
- HTTP/3 可通过 `http3_client_options::multipath_initial_max_path_id` 和
  `http3_server::set_multipath_initial_max_path_id()` 协商；客户端随后调用
  `http3_client::async_probe_path()` 指定额外远端 UDP endpoint。
- 若需要真实多本地网卡/端口路径，使用三参数重载：
  `co_await client.async_probe_path(path_id, remote, local)`。`local` 会绑定独立
   UDP socket；HTTP/3 client 持有该 socket、接收协程、取消令牌和 completion，并在
   `close()` 中先取消、关闭、等待所有 receiver 退出后才释放 QUIC connection。应用层
  不需要也不应自行启动捕获连接裸指针的 receive loop。正在进行的
  `async_probe_path()` 也持有 connection 所有权；`close()` 会使其以取消结果收束，
  不会让悬挂 probe 访问已释放的 transport。多个协程同时调用 `close()` 会由 client
  内部协程锁串行化：只有第一个调用回收 driver/receiver，后续调用在回收完成后幂等返回。
  请求、流式响应、Push 取消和 WebTransport 建立同样会在跨 `co_await` 前保留 session
  与 transport 所有权；因此可与 `close()` 并发执行并以连接关闭错误收束。这个保障不会把
  HTTP/3 request stream 串行化，正常多路复用的吞吐不受生命周期锁影响。
- 逐路径 Datagram PLPMTUD 为显式 opt-in：客户端设置
  `http3_client_options::enable_path_mtu_discovery`，服务端在 `start()` 前调用
  `set_path_mtu_discovery()`。每条已验证路径从 1200 字节开始，只有 padded PING
  被 ACK 后才以 128 字节步长提升发送上限；探测丢失不会降低其他路径的上限。
- Windows IOCP 上，未监听 UDP endpoint 的 ICMP port-unreachable 会作为异步完成错误返回。
  cnetmod 将其归类为候选路径丢失，不会关闭整条 QUIC 连接；`async_probe_path()` 仍会在
  3 PTO 后返回 `timed_out`，已验证的其他路径继续服务。E2E 覆盖成功验证、独立本地
  socket、PMTU 提升、PMTU 黑洞（维持 1200 字节并继续传输），以及验证超时后的 Path 0
  回退。
- `http3_client_options::path_mtu_initial_probe_delay` 与服务端
  `set_path_mtu_discovery(maximum_payload, probe_interval, initial_probe_delay)` 可为新验证
  路径延后第一次 PMTU 探测；默认 `0ms`，维持立即探测语义。延迟只影响首个 padded-PING
  探测，后续成功或失败重试仍由 `path_mtu_probe_interval` 控制。纯 PMTU 探测和不可靠
  DATAGRAM 在 PTO 后都会从恢复状态中退役，避免没有可重传帧时重复触发同一个已到期
  deadline。

当前实现支持同一 socket 的多远端路径，也支持通过三参数 `async_probe_path()` 创建并
管理独立本地 UDP socket 的真实多四元组路径。自动网卡选择策略、长期弱网 soak 及第三方
Multipath 草案对端互操作仍需独立完成，不能宣称为通用浏览器互操作能力。
<!-- END SOURCE: skill/http/http3-quic.md -->

<!-- BEGIN SOURCE: skill/infra/application.md -->
# Source: `skill/infra/application.md`

# Application 应用运行框架

> 提供配置、显式自动装配、依赖生命周期、任务监管、健康检查、OTEL 和优雅停机的生产级组合根。

**import**: `import cnetmod.application;`

**源码**: `src/application/`

## 核心原则

1. 使用 `application_builder` 构建，使用 `application_host` 运行；不存在旧 `http_application` 兼容层。
2. 外部组件只有在配置中 `enabled: true` 且调用 `enable_auto_configuration()` 时才会装配。
3. 所有长生命周期组件实现 `managed_service`，关键后台协程交给 `task_supervisor`。
4. `build()` 完成严格配置校验、服务注册冻结和依赖环检查；失败时不启动网络监听。
5. required 组件启动失败会精确回滚；optional 组件进入降级恢复。运行期 required 恢复预算耗尽会请求应用停机。
6. HTTP handler 只读取 `health_registry` 的缓存，不同步探测 Redis、数据库或消息代理。

启动任务组使用整体启动截止时间，每个组件独立执行单组件截止时间。
optional 组件的单组件超时只触发降级与恢复，不能由共享计时器升级为整层失败；
整体启动预算耗尽或 required 组件失败仍触发回滚。真实 MySQL 适配器的本地
拒绝连接用例验证 optional 降级时 live=200、ready=503，并可正常主动停机。

## 最小入口

```cpp
#include <cnetmod/config.hpp>

import std;
import cnetmod.application;

auto configure_routes(cnetmod::http::router& routes) -> void
{
    routes.get("/orders", [](cnetmod::http::request_context& request)
        -> cnetmod::task<void>
    {
        request.json(cnetmod::http::status::ok, R"({"orders":[]})");
        co_return;
    });
}

auto main() -> int
{
auto host = cnetmod::application::application_builder{"order-service"}
    .configuration_file("application.yaml")
    .enable_auto_configuration()
    .routes(configure_routes)
    .middleware(cnetmod::cors())
    .build();
    if (!host)
        return EXIT_FAILURE;
    return host->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

`application_host` 自己创建 `net_init`、`io_context`、CPU `thread_pool`、HTTP 服务、Telemetry Hub、健康缓存和任务监管器。`request_stop()` 可由其他线程重复调用，所有调用汇入同一条幂等停机路径。CPU 线程数通过 `application.cpu_threads` 配置，默认取硬件并发数且至少为 1；也可由 `CNETMOD_CPU_THREADS` 覆盖。该值运行时变更需要重启。

`application_host::runtime()` 返回受控的 `application_runtime` 门面，而不是公开原始
`io_context`。业务用 `spawn_managed()` 注册可取消、可等待、可恢复的后台任务；回调得到
独立 `cancel_token`。短时 CPU 工作使用 `offload()`，完成后自动回到 Application 事件
循环。`cancellation()` 返回可复制、可注册回调的 `std::stop_token`，
`stop_requested()` 提供轻量查询；`tasks()` 与 `telemetry()` 分别提供既有任务监管和观测
组合根。停机先排空请求、广播取消、等待监管任务、逆序关闭服务，最后停止 CPU 池。

Route 中优先使用 `offload()` 包装一段纯 CPU callable：它会在 Application CPU 池运行，
无论正常返回还是抛异常，等待方都会恢复到当前 Application 事件循环。只有算法必须跨
多个异步步骤持续驻留 CPU 池时才成对使用 `schedule_on_cpu()` 与
`resume_to_event_loop()`；切回事件循环前不得读写 `request_context`。两种方式都不需要
业务保存或传递裸 `io_context&`：

```cpp
builder.routes([](http::router& routes, application_runtime& runtime) {
    routes.post("/score", [&runtime](http::request_context& request)
        -> task<void> {
        auto input = std::string{request.body()};
        auto score = co_await runtime.offload(
            [input = std::move(input)] { return calculate_score(input); });
        request.text(http::status::ok, std::to_string(score));
    });

    routes.post("/pipeline", [&runtime](http::request_context& request)
        -> task<void> {
        auto input = std::string{request.body()};
        co_await runtime.schedule_on_cpu();
        auto result = run_cpu_pipeline(input);
        co_await runtime.resume_to_event_loop();
        request.text(http::status::ok, std::move(result));
    });
});
```

`parse_offloaded(runtime, text)` 与 `dump_offloaded(runtime, value)` 在 Host CPU 池执行
JSON 解析和序列化，避免 route 协程阻塞事件循环。两者拥有输入直到执行完成并返回
`std::expected`；语法错误为 `invalid_argument`，内存不足保持 `not_enough_memory`。

`application_runtime::files()` 返回 `async_file_template`，其
`open/read/write/close/stat/read_all/write_all/remove` 内部使用 Host 的事件循环，业务和
领域端口无需传递 `io_context&`。涉及请求超时的调用应使用带独立 `cancel_token&` 的
重载；`remove()` 对不存在的目标幂等成功。

`application_runtime::rest()` 返回 `rest_template`，用于业务出站 HTTP 调用。它组合既有
HTTP client pool 与 OTEL instrumented client，统一提供 `exchange/get/post/put/patch/remove`；
成功请求的 client 才会归还复用池，传输失败或取消的 client 会关闭并丢弃。业务代码不得为
普通 HTTP 调用自行创建 `http::client`，也不得为了完成一次请求手工停止事件循环。需要请求头
或其他高级选项时构造 `http::request` 后调用 `exchange()`；需要操作级取消时使用接收
`cancel_token&` 的重载。`rest_template_options::default_headers` 注入模板级请求头，
`rest_request_options::headers` 注入单次请求头；名称按 HTTP 规则忽略大小写，单次值覆盖默认值。
Template 的接口与实现统一位于 `src/application/template/`，不在 Application 根目录堆放实现。

启用任一 Chat Model provider 自动装配后，
`application_runtime::chat_model(instance, options)` 返回具名
`chat_model_template`。Application 只依赖 `cnetmod.ai` 与
`chat_model_service`，不依赖 OpenAI 客户端类型；OpenAI-compatible、Claude、Gemini
或本地推理后端通过同一 provider Strategy 接入。每个 managed provider 拥有固定容量
连接池，一次 invoke/stream 独占一个 lease 到终态，避免 keep-alive 响应交错。
文本重载复制 `chat_model_template_options::request`，按 system、默认消息、历史、当前
user 输入的顺序构造请求；显式 `chat_request` 重载不改写调用方消息。模板必须在
`build()` 完成后或 route handler 执行时解析，因为自动装配服务是在 Host 构建期间注册的。

运行时端点、凭据或池容量变更通过
`application_runtime::reconfigure_chat_model(instance, configuration, cancellation)`
提交完整 provider 配置。具体 provider 先校验配置并建立整代新连接，全部成功后才原子发布；
失败时旧代保持服务。已借出的 model lease 持有旧客户端所有权，会在请求结束后自然退休，
新请求只进入新代。不要把 `chat_model_pool::reset()` 暴露给业务代码，否则会绕过 provider
校验、连接建立、Telemetry 和生命周期边界。OpenAI 配置支持热更 `base_url`、`api_key`、
`tls_verify`、`timeout_seconds` 与 `pool_size`，传入属性是完整替换而不是局部 patch。

`chat_model_template::conversation(session_id, store)` 提供显式会话边界。
同一 session 的调用通过共享协程门串行化，不同 session 可占用不同池连接并行运行；
持久层仍是唯一真相。成功回合才用 `append_batch(user, assistant)` 原子追加，失败或取消
不产生半回合。`history_limit` 控制每次读取的最近消息数，零表示读取全部。

需要在 route handler 捕获 Runtime 时，使用双参数路由配置器：

```cpp
builder.routes([](http::router& routes, application_runtime& runtime) {
    auto* application = &runtime;
    routes.post("/archive", [application](http::request_context& request)
        -> task<void> {
        auto saved = co_await request.with_deadline(
            [application](cancel_token& token) {
                return application->files().write_all("archive.json", "{}", token);
            });
        request.text(saved ? http::status::ok : http::status::internal_server_error,
            saved ? "saved" : "failed");
        co_return;
    });
    routes.get("/upstream", [application](http::request_context& request)
        -> task<void> {
        auto response = co_await application->rest().get(
            "https://service.internal/health",
            {.headers = {{"Authorization", "Bearer runtime-token"}}});
        request.text(response ? http::status::ok
                              : http::status::bad_gateway,
            response ? std::string{response->body()} : "upstream failed");
        co_return;
    });
    routes.post("/chat", [application](http::request_context& request)
        -> task<void> {
        auto model = application->chat_model("assistant",
            {.request = {.model = "gpt-4o-mini"},
                .system_prompt = "Answer concisely."});
        if (!model) {
            request.text(http::status::service_unavailable,
                "model unavailable");
            co_return;
        }
        auto response = co_await model->invoke(std::string{request.body()});
        request.text(response ? http::status::ok : http::status::bad_gateway,
            response ? std::string{response->content()} : response.error());
    });
});
```

```cpp
application::chat_model_reconfiguration next;
next.properties = {
    {"base_url", "https://new-gateway.example/v1"},
    {"api_key", rotated_key},
    {"tls_verify", true},
    {"timeout_seconds", 30},
    {"pool_size", 8},
};
auto reloaded = co_await runtime.reconfigure_chat_model(
    "assistant", std::move(next), &cancellation);
```

双参数配置器在 Host Runtime 构造完成后、`build()` 返回前执行。handler 可在 Host 生命周期
内安全捕获 Runtime 引用。HTTP 底层不反向依赖 Application，也不提供线程局部的
`current_io_context()`。

Application 的 SSE 接口直接在同一个 routes 配置器中使用 `router::sse_get()` 或
`router::sse_post()` 声明。框架按请求注入 `sse_stream&`，业务只序列化事件 payload；
SSE 响应头、具名帧编码、心跳、断线返回值和终止帧由框架负责：

```cpp
builder.routes([](http::router& routes) {
    routes.sse_post("/chat", [](http::request_context& request,
                                http::sse_stream& stream) -> task<void> {
        if (!co_await stream.send(R"({"text":"hello"})", "delta"))
            co_return;
        co_await stream.send(R"({"tokens":1})", "done");
        co_await stream.finish();
    }, http::sse_stream_options{
        .max_duration = std::chrono::seconds{60},
        .write_timeout = std::chrono::seconds{3},
    });
});
```

`stream.started()` 为 false 时，业务仍可返回普通 HTTP 错误；一旦为 true，只能继续写 SSE
错误/结束帧或结束连接。Application recover 中间件对未捕获异常遵守该边界，不会在 SSE
已经提交后错误地回退 JSON 响应。

Application 默认从 `http.sse` 为所有 SSE 路由注入超时：整条流最长 120 秒，单次响应头或
事件帧写出最长 5 秒。路由尾部的 `sse_stream_options` 可按接口缩短或延长，但两个值都必须
为正数。达到总时限或写超时后，当前写操作会被取消、流进入 `failed`、socket 被关闭；不会
继续占用连接或回退普通 HTTP。调用 OpenAI、数据库等下游操作时应通过
`request.with_deadline()` 继承同一个总预算，避免业务生产者在连接结束后继续运行。

自定义基础设施使用 `application_builder::service_factory()` 在构建阶段创建。该 API 是
协议适配器的基础设施扩展点，不是业务执行入口；普通业务只能从 Host 获得 Runtime 门面。
工厂通过
`application_service_context` 获得 host 所有的 `io_context`、Telemetry Hub、
`task_supervisor` 和只读配置，返回一个 `managed_service`。工厂错误、空服务或重复
服务身份都会让 `build()` 失败；所有工厂完成后 registry 才冻结。工厂还可以保存
`application_service_context::runtime` 的非拥有引用，但不得超过 Host 生命周期。框架不
公开运行期 `application_host::io()`，避免下游在生命周期监管之外派发关键协程。

业务 HTTP 中间件通过 `application_builder::middleware()` 按注册顺序装配，不影响独立的
管理端点。执行顺序固定为：框架异常恢复、停机跟踪、请求 ID、追踪、指标和请求超时位于
业务中间件外层，访问日志位于业务中间件内层。这样自定义认证、CORS、限流等逻辑仍受到
框架取消、排空和遥测边界监管，同时不能绕过管理端点隔离。空中间件在构建配置阶段拒绝。

host 显式持有预先创建的顶层编排协程，以协程帧内队列节点进入事件循环，不使用 detached 包装派发该主任务。编排异常进入清理边界；`run()` 在事件循环返回后检查主任务已结束。

编排异常的清理入口停止监听后，立即取消被跟踪请求并中止业务和管理连接的 socket I/O，
进入收尾并先等待被跟踪 handler 退出，再取消、等待监管任务，然后关闭服务；不等待再次
走正常请求排空阶段，也不覆盖最早的原始错误。收尾本身失败的紧急回退仍请求监管任务停止。
故障注入测试覆盖活跃 HTTP handler 等待期间的编排分配失败、请求取消和原始错误保留。
该测试同时挂载受管理依赖及后台任务，检查 handler 异步完成取消收尾后才通知后台任务停止，
依赖只启动、关闭一次。
这不等于可强制终止未响应取消的业务协程，也不覆盖任意多层依赖和后台任务的全部竞争。

业务及管理 HTTP 接收循环均注册为必需的受监管任务，监听异常不自动重启，而是进入统一停机路径。host 在接收事件循环上调用 `server::stop()`，清理阶段等待 supervisor；尚未开始执行就被取消的监听任务不会重新启动监听。监听及健康任务注册失败均进入回滚，不允许静默忽略。已接受的连接仍有独立生命周期，不能由接收循环已结束推断连接已排空。

## 配置

优先级固定为：框架默认值 < YAML/JSON < 环境变量 < builder `configure()` 显式覆盖。

新项目推荐使用 YAML（`.yaml` 或 `.yml`）作为 Application 配置格式；JSON 继续作为
兼容输入格式，并仍用于 HTTP/消息负载的类型化编解码，两者不是同一层职责。

`application_builder::configuration_file()` 根据 `.json`、`.yaml` 或 `.yml`
扩展名选择解析器。YAML 由独立的 `yaml_cpp` C++23 Modules 门面和
`cnetmod.application.yaml_configuration` 适配器转换为同一个 JSON 文档模型；因此
两种格式有完全相同的字段校验、`${ENV_VAR}` 注入、脱敏及热重载语义。YAML 映射键
必须为字符串且不得重复，文档根必须为映射；锚点展开限制为 64 层以拒绝循环或病态输入。
解析器与模块门面分别以固定提交收录在 `3rdparty/yaml-cpp` 和
`3rdparty/yaml-cpp-modules` Git submodule 中。cnetmod 构建不会再通过
`FetchContent` 隐式下载 YAML 依赖；克隆后应执行
`git submodule update --init --recursive`。开发时可分别用
`CNETMOD_YAML_CPP_SOURCE_DIR` 和 `CNETMOD_YAML_CPP_MODULES_SOURCE_DIR`
指向本地版本。

下面的 JSON 仅用于展示与 YAML 等价的完整兼容字段；可直接使用仓库中的
`examples/application/application.yaml` 作为推荐配置模板。

```json
{
  "application": {
    "name": "order-service",
    "install_signal_handlers": true,
    "cpu_threads": 4
  },
  "crash_dump": {
    "directory": "crash"
  },
  "logging": {
    "level": "info",
    "format": "json"
  },
  "http": {
    "address": "0.0.0.0",
    "port": 8080,
    "request_timeout_ms": 30000,
    "sse": {
      "max_duration_ms": 120000,
      "write_timeout_ms": 5000
    }
  },
  "management": {
    "enabled": true,
    "address": "127.0.0.1",
    "port": 8081,
    "same_port": false
  },
  "observability": {
    "tracing": true,
    "metrics": true,
    "logs": true,
    "sampling_ratio": 1.0,
    "otlp": {
      "traces_endpoint": "http://127.0.0.1:4318/v1/traces",
      "metrics_endpoint": "http://127.0.0.1:4318/v1/metrics",
      "logs_endpoint": "http://127.0.0.1:4318/v1/logs"
    }
  },
  "orm": {
    "sharding": {
      "enabled": false,
      "topologies": {
        "orders": {
          "logical_table": "orders",
          "table_count": 64,
          "databases": ["orders-0", "orders-1"],
          "scatter_gather": true,
          "distributed_transactions": true
        }
      }
    }
  },
  "services": {
    "primary-cache": {
      "type": "redis",
      "instance": "primary",
      "enabled": true,
      "required": true,
      "host": "redis.internal",
      "password": "${REDIS_PASSWORD}",
      "recovery": {
        "initial_delay_ms": 500,
        "maximum_delay_ms": 30000,
        "budget_ms": 120000,
        "multiplier": 2.0,
        "jitter": 0.2
      }
    }
  }
}
```

`orm.sharding.enabled` 默认关闭，因此普通 ORM 和现有单库配置不受影响。开启后，每个
`topologies` 条目会注册一个同名 `mysql_sharded_session_gateway`；`databases` 必须引用
已经启用的具名 MySQL 服务。启动前会验证表标识符、分表数量、重复数据库实例和服务引用。
全分片读取只能通过显式 `scatter_read()` / `scatter_gather()` 发起；跨库写事务只能通过
显式 `distributed_transaction()` 发起，多库路径使用 MySQL XA 两阶段提交。

服务条目的对象键只是配置绑定名；`type + instance` 才是服务身份，因此同一接口可配置多个具名实例。未知框架字段、未知集成属性、非法端口、非法超时、重复服务身份和缺失的必要凭据都会使 `build()` 失败。

HTTP client 的 `connect_timeout_ms`、`request_timeout_ms` 和 OpenAI 的
`timeout_seconds` 在创建对应服务前检查整数类型及正数范围，不接受布尔、小数、
字符串、null、零、负数或超范围整数，避免 JSON 转换产生截断或回绕。
缺省字段仍使用原有默认值。此校验不表示其他所有集成参数均已完成边界审计。

Redis、MySQL、PostgreSQL、MongoDB、Kafka、MQTT、AMQP 0-9-1 和 AMQP 1.0
的独立 `port` 属性同样在转换前检查，必须为 1～65535 的整数；缺省保持协议默认端口。
不能依赖无符号转换后的端口值做合法性判断，否则 65537 等值可能回绕为另一个端口。

MySQL 服务还支持 `ssl`（`disable`、`enable`、`require`）、`tls_verify`、
`tls_ca_file`，以及 `connect_timeout_ms`、`pool_timeout_ms`、
`retry_interval_ms`、`ping_interval_ms`、`ping_timeout_ms`。所有显式超时必须是
1～86400000 毫秒的整数；未知 TLS 模式在 `build()` 阶段拒绝。

四类数据库连接池的 `minimum_size` / `maximum_size` 使用共享校验：前者可为零，
后者必须为正整数，按缺省值补齐后仍必须满足 minimum ≤ maximum。转换前拒绝
错误 JSON 类型、负数和超出可表示范围的数值，不静默截断或调整用户配置。
该检查不承诺机器有足够内存或数据库连接配额，实际资源获取仍在生命周期内处理。

采样率必须是 [0, 1] 的有限值，builder 提供的 NaN/无穷值同样拒绝。HTTP 请求超时
未设置时保持原语义；显式设置则必须为正数。框架的正时长参数以及 HTTP client 超时
不超过 steady_clock 可表示时长的一半，与恢复策略的保守上限一致；这不是对所有
运行期截止时间运算的溢出安全证明。

`${ENV_VAR}` 在解析阶段展开。`password`、`secret`、`token`、`api_key`、凭据和连接串会由 `redact_configuration()` 脱敏；框架不把请求正文、提示词、SQL 参数、凭据或消息载荷写入健康响应和遥测。

每个 `application_host` 默认在构建完成后、打开监听端口前安装进程级崩溃转存。Windows
生成 `.dmp` 和文本报告；Unix 开启系统 core dump 并写入最小崩溃记录。该安全网不依赖
日志或 OTEL，二者关闭、阻塞或故障时仍保持工作。`crash_dump.directory` 只允许在下次
进程启动时变更，因此运行时重载会报告需要重启。

支持的环境变量包括：

- `CNETMOD_APPLICATION_NAME`、`CNETMOD_HTTP_ADDRESS`、`CNETMOD_HTTP_PORT`、`CNETMOD_LOG_LEVEL`
- `OTEL_EXPORTER_OTLP_ENDPOINT`、`OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`
- `OTEL_EXPORTER_OTLP_METRICS_ENDPOINT`、`OTEL_EXPORTER_OTLP_LOGS_ENDPOINT`
- `OTEL_SERVICE_NAME`、`OTEL_SERVICE_VERSION`
- `OTEL_RESOURCE_ATTRIBUTES`、`OTEL_EXPORTER_OTLP_HEADERS`

## managed_service

```cpp
class order_worker final : public cnetmod::application::managed_service
{
public:
    auto key() const -> cnetmod::application::service_key override;
    auto dependencies() const
        -> std::vector<cnetmod::application::service_key> override;
    auto requirement() const noexcept
        -> cnetmod::application::service_requirement override;
    auto start(cnetmod::application::service_context& context)
        -> cnetmod::task<std::expected<void, std::error_code>> override;
    auto stop(cnetmod::application::service_context& context)
        -> cnetmod::task<std::expected<void, std::error_code>> override;
    auto probe(cnetmod::application::service_context& context)
        -> cnetmod::task<cnetmod::application::health_report> override;
};
```

`service_context` 提供 `io_context`、Telemetry Hub、Task Supervisor、取消令牌和本阶段截止时间。实现必须响应取消和截止时间，禁止在协程中同步阻塞。

`managed_service::cleanup_required() const noexcept` 默认返回 false。若组件启动失败
或抛异常后仍持有未完成清理的资源，必须覆盖该查询并返回 true；它不是运行状态查询。
生命周期在操作收尾后、记录遥测前登记这些资源，回滚失败时连同依赖保留，允许后续
`stop()` 重试。恢复尝试先清理 pending 状态，再启动，二者共用本次尝试截止时间。
成功关闭后组件必须清除 pending 状态。MongoDB 适配器已将其 cleanup_pending 状态
接入此契约；其他适配器仍须遵守失败启动自行清理或显式报告残留的约定。

`shutdown_required() const noexcept` 单独报告停机资源归属，默认委托
`cleanup_required()`。MySQL、Redis 在维护任务登记后即报告 shutdown_required，
即使首次借用尚未成功也参与有序关闭；它们不因此报告 cleanup_required，恢复仍复用
原连接池。前者决定是否必须 stop，后者决定重试 start 前是否必须先清理，两者不能混用。

## 服务注册与依赖图

```cpp
registry.add_named<user_repository>("primary", repository);
auto& users = registry.require<user_repository>("primary");
```

- Registry 模式提供接口绑定和具名实例。
- `add_managed_named()` 以事务方式同时注册类型绑定和生命周期所有权。
- 重复注册直接失败，不静默覆盖。
- 构建后 registry 冻结并保持只读。
- 启动前使用拓扑排序检查依赖缺失与循环；同一拓扑层并行启动，停止严格逆拓扑顺序。
- `last_failure()` 保留失败组件、生命周期阶段和原始 `std::error_code`。
- 组件原始 `start()` 返回成功时立即登记资源所有权，再进行截止时间结果转换；
  即便该成功发生在取消后，调用方仍收到超时，但回滚不能漏掉该组件的 `stop()`。
- 回滚不会覆盖发起回滚的启动失败；`last_rollback_failure()` 单独提供回滚组件的关闭失败及 `rollback` 阶段。
- `last_rollback_error()` 还保留无法定位组件的回滚准备错误。回滚内部捕获清理异常并恢复原始启动错误；尚未关闭的资源保持登记，可由生命周期调用者后续重试 `stop()`。

## 自动装配清单

| 配置 `type` | 注册服务 | 说明 |
|---|---|---|
| `http_client` | `http_client_service` | 带 W3C Trace Context 的出站 HTTP 客户端 |
| `openai` | `openai_service` + `chat_model_service` | OpenAI adapter、模型连接池、`chat_model_template` 和 GenAI telemetry listener；`pool_size` 默认 4 |
| `redis` | `redis_service` / `redis_cluster_service` | `mode=standalone` 连接池；`mode=cluster` 槽路由、seed failover 与健康检查 |
| `mysql` | `mysql_service` | MySQL 连接池 |
| `postgresql` | `postgresql_service` | PostgreSQL 连接池 |
| `mongodb` | `mongodb_service` | MongoDB 连接池与维护任务 |
| `kafka` | `kafka_service` | Kafka 客户端 |
| `mqtt` | `mqtt_service` | MQTT 客户端与重连 |
| `amqp091` | `amqp091_service` | AMQP 0-9-1 客户端与受监管帧泵 |
| `amqp10` | `amqp10_service` | AMQP 1.0 客户端 |
| `grpc` | `grpc_client_service` | gRPC 客户端 |
| `grpc_server` | `grpc_server_service` | 挂载到业务 HTTP/2 路由的 gRPC 服务路由器 |

未启用相应 CMake 协议开关时，启用该服务会在构建阶段返回 `not_supported`，不会拖到运行期失败。

Standalone Redis 服务通过 `redis_service::make_template(options, parent)` 创建业务门面。
它借用服务拥有的连接池并自动使用 Application Telemetry Hub 的 span exporter；parent
仍由请求协程显式传入，框架不使用 thread-local 活动 span。返回的 `redis_template`
不能超过 `redis_service` 生命周期。Cluster 服务继续使用 `cluster_client`，当前模板不
隐式跨 slot 路由或拆分 multi-key 操作。

OpenAI 服务通过 `openai_service::make_template(options)` 或
`application_runtime::openai(instance, options)` 创建业务门面。模板借用服务拥有的模型、
共享请求门和 telemetry listener，因此不能超过 Host 生命周期；同一服务创建的多个模板
仍共享串行化边界。业务代码优先使用模板的 `invoke()` / `stream()`，不直接操作
`openai::client`，只有协议扩展端点尚未进入模板时才使用底层客户端。

AMQP 0-9-1 帧泵只监管单次连接会话，任务自身的重试预算为零，不单独触发 required
恢复耗尽通知；错误保留在任务状态中。连接重建由服务生命周期的健康恢复任务发起，
required/optional 要求及恢复预算仍来自 managed_service，而不是帧泵任务标志。
重连成功后仍需健康缓存连续成功确认，不能直接恢复 readiness。已有 open 连接只有
同时存在活动帧泵登记时才算幂等启动成功；否则必须补登记，失败进入回滚。
启动通过受监管的 `async_run_session()` 等待读取任务接管和已记录拓扑重放完成，
健康探测同时检查会话 ready、连接和帧泵；重放尚未完成时不能仅凭 socket=open 报告 up。
启动等待将阶段取消传递给会话，但成功返回后不再借用启动上下文；帧泵仍由 supervisor 拥有。
required/optional 生命周期脚本 TCP 回归扣住 Exchange.DeclareOk，验证恢复任务仍运行、
probe 为 down，释放确认后还需两次健康成功才恢复 readiness。
required/optional 的重放等待停机用例还验证：不发送交换机确认时，生命周期 stop 和
supervisor join 完成，帧泵及恢复任务均 stopped，连接 disconnected，服务登记清空，
且不触发恢复预算耗尽通知。此测试在单事件循环上发起停机，不覆盖跨线程取消竞争。
另有 200ms 恢复总预算的静默重放回归：等待交换机确认耗尽预算后，恢复错误保留
timed_out，帧泵失败、连接关闭、probe 为 down；required 升级一次，optional 不升级。
总预算与单次启动截止时间取较早者。独立阶段超时另用 200ms 启动上限、1 秒恢复预算验证：
首轮重放无确认，下一连接可观察到上轮 timed_out，再次重放参数与原声明逐字节一致；
required/optional 均恢复成功且不升级，readiness 仍需两次健康确认。范围限脚本交换机声明，
不代表队列、消费者和真实 broker 的超时恢复都已验收。
随后生命周期脚本扩展到交换机→服务端命名队列→绑定→消费者：每次连接返回不同
队列名，恢复请求检查新名称，原消费者回调接收一次带重投标志的消息并校验标签、
投递编号及正文。普通恢复及单次超时后再次恢复均走该链路；手动 ACK、完整字段表、
全部声明标志和真实 broker 仍未覆盖，不能称为完整消息可靠性验收。
AMQP 传输报告内存不足时，适配器保留通用 not_enough_memory，启动等待与帧泵状态
使用同一失败原因，不再统一返回 connection_aborted。握手、会话结果转换仅在失败
分支执行；这不代表其他协议错误类别均已完成映射审计。
这不是完整订阅链及真实 RabbitMQ 的 Application 恢复验收；启动派发失败和取消竞争仍需补齐。

## 健康与管理端点

MySQL、Redis、PostgreSQL 和 MongoDB 在启动入口拒绝已经取消或过期的
`service_context`，不先注册维护任务或进行预热。取消和过期同时发生时优先返回
`operation_canceled`。此入口检查不代替操作进行中的取消和截止时间处理。

MongoDB 预热失败保留 `cnetmod.mongodb` 错误类别，不再统一映射为连接拒绝。
类别值为协议 `error_code` 的整数值加一（协议枚举从零开始且没有成功项），
超时和取消分别转换为通用 `timed_out` 与 `operation_canceled`。
错误消息只含固定前缀及编号，不转发服务端诊断、连接凭据或命令内容。
此转换位于 Application 启动失败路径，不改变数据库查询或关闭 OTEL 的路径。

MongoDB 管理探测通过 `connection_pool::health_check(cancel_token&)` 执行实际 PING，
并使用服务截止时间取消等待。失败连接归还前标记废弃；取消通知在连接所属事件循环
执行，探测返回前等待通知收尾。无空闲连接时，在原有容量与并发建连上限内创建
候选连接，hello 和 PING 成功后才报告健康；下次可复用该连接，零初始连接池不会
仅因没有候选而一直失败。全忙时复用借用队列，受池排队超时与服务探测截止时间约束；
取消移除本次等待，不取消业务方持有的连接。业务归还后才发送 PING。
取消通知投递到所属事件循环并在探测返回前收尾；等待后需要新建连接时也传递停止信号。
池锁等待仍未支持取消，任意排队竞争、持续分配失败及真实认证恢复尚未完整验证。
池关闭将空闲连接从登记表移除，由关闭结果持有到锁外关闭；借出连接继续保留登记，
直到归还，不能通过提前清空整个登记表隐藏仍在途的借用者。

MongoDB 同步关闭复用连接槽、等待者的关闭链表，不创建临时容器或错误诊断对象。
等待者先标记为关闭，恢复后才构造原有错误。锁竞争时使用池持有的通知节点与共享
状态投递，不创建 detached 协程；`async_close()` 会等待这次已登记通知。
异步等待任务本身仍可能分配失败，调用方应保留池并重试等待；不要把同步入口无分配
理解为任意低内存状态下的全部异步清理都不会失败。事件循环必须运行到通知收尾。

MongoDB 池关闭会向所有已登记的在途建连发送取消，不再依赖 hello 命令超时。
登记节点由建连协程帧持有，完成后在池锁内移除并归还配额。`async_close()` 发出
取消但本身不等待所有建连结束；`mongodb_service::stop()` 仍检查 `connecting_count()`，
未收尾时保留清理状态，允许随后重试。启动使用同一服务截止时间与取消令牌执行
`warm_up(cancel_token&)`，取消仅转发到该次预热创建的连接；等待其他调用方建连时，
退出本次等待而不取消其他建连。Application 启动失败后的池关闭仍按资源所有权取消
全池在途建连。无令牌重载直接进入无取消特化，不增加包装协程。
本地无 hello 回复测试覆盖命令超时关闭、启动截止时间与跨线程取消，并检查可再次启动。
池锁等待仍未支持取消，真实 TLS/认证取消也尚未完成验证，不能据此宣称所有启动路径已闭合。

Kafka 服务启动把生命周期取消令牌传给客户端连接，并使用 `operation_deadline` 约束操作。
已启动服务通过同一取消/截止时间契约发起元数据刷新作为健康探测，不再仅凭启动标志报告 up。
提前取消、已过期启动，以及本地 TCP 对端收到 Kafka 请求后不响应时的超时/主动取消已覆盖回归。
真实 broker 的完整协议交换、断线恢复及其他部分连接清理仍需验证。
Kafka 取消和超时按生命周期语义返回通用错误；其他 Kafka 失败保留原始编号及
`cnetmod.kafka` 类别。配置、传输、协议格式及授权错误可与对应的 `std::errc` 条件比较。
错误消息仅包含固定前缀和编号，不转发 broker 的诊断文本。

- `/actuator/live`：进程及事件循环是否存活。
- `/actuator/ready`：应用已启动且所有已启用组件可用；optional 故障也会呈现降级 readiness。
- `/actuator/health`：聚合状态、各组件状态、错误类别和最近探测时间。
- `/actuator/prometheus`：OpenMetrics 文本指标。

默认管理地址为 `127.0.0.1:8081`。健康探测按周期并行执行并缓存结果，默认 3 次失败进入 down、2 次成功恢复 up。

运行期重连成功只进入等待健康确认状态，不计作一次成功探测；readiness 恢复仍需满足配置的连续成功探测次数。

每次受监管恢复尝试在 `start()` 成功后先执行一次 `probe()`；探测非 up 仍作为本次恢复失败处理，继续消耗该次恢复任务的预算。这次恢复内部探测不替代健康注册表的连续成功确认。

一次恢复的启动与内部探测共享 `service_start_timeout`，使用独立尝试令牌。超时只取消本次尝试，允许后续重试；应用停机取消则向当前尝试转发。上述取消仍要求服务协程协作退出。

服务恢复在任务派发前建立绝对恢复截止时间，首次连接、内部探测、后续尝试和退避均消耗同一预算；单次尝试使用该截止时间与 `service_start_timeout` 中更早者。重连成功但尚未确认健康时保留截止时间，后续恢复任务继承它。`reconcile_health()` 仅在健康缓存确认 up 且恢复任务不再运行后清除此故障周期；健康循环也检查 starting/degraded 状态下的预算到期，因此确认阶段的到期通知可能延迟一个健康刷新周期。

required 服务同一故障周期耗尽预算后不重复派发或通知停机；optional 服务保持降级，后续健康循环可在上一周期已经失败后开启新的恢复周期。预算到期仍等待被取消的协程安全退出，不强制销毁协程。

健康快照携带缓存 `revision`，每次结果更新（包括探测准备失败）都会递增。`reconcile_health()` 在修改故障周期前检查版本，忽略已过期的快照，防止延迟投递的旧 up 清除新故障预算。`is_current()` 仅提供瞬时版本检查，不会在返回后锁住健康注册表。

异步健康探测保存派发时的缓存版本，结果提交在注册表锁内比较版本并更新；期间出现更新则丢弃旧结果，不推进连续成功/失败计数。未完成探测的失败补记也使用同一版本条件。停机中的注册表不提交探测结果，stopping/stopped 组件不再被后续刷新探测。

通用 `task_supervisor::supervise()` 可选接收 `recovery_deadline`。未提供时保留后台长任务首次失败后开始恢复计时的语义；提供时用于限制重试派发和退避，操作自身仍须将同一截止时间传入其取消感知 I/O。服务生命周期已完成这一传递。

## 运行期更新

`reload_configuration()` 只原位更新日志级别、OTEL 采样率、健康策略和恢复策略。监听地址、端口、中间件、线程、连接参数、凭据、OTLP 出口或队列参数变化会设置 `restart_required`，不会偷偷重建连接。

底层 `reload_safe_configuration(active, candidate)` 返回
`std::expected<configuration_reload_result, std::error_code>`。它先校验候选并在私有副本中
准备变更清单，准备失败不修改 active；提交使用已静态验证的不抛异常移动赋值。
分配失败映射为 `not_enough_memory`，非法候选返回校验错误。调用方必须检查 expected，
不能把失败当作“没有字段变化”。这保证配置函数的提交边界，不等于 Host 向所有运行组件
传播配置已具备跨组件事务性。

同一配置绑定名若更换服务类型或实例名，旧服务的恢复策略保持不变；新身份及其策略
需要重启生效。Host 在配置锁内准备服务键及候选副本，先完成恢复策略表的批量更新，
再应用已校验的健康策略、采样率和日志级别，最后移动提交配置。底层配置函数不再
修改全局日志状态。各组件仍使用独立读锁，这不是跨组件读快照的线性一致性保证。

生命周期的 `update_recovery_policies(span<pair<service_key, recovery_policy>>)` 返回
expected：先验证策略，再复制现有覆盖表并应用整批修改，成功后锁内交换。分配失败
保留整张旧表，不再逐项提交；`recovery_policy_override(key)` 可读取指定覆盖项。
Host 检查批量更新错误，失败时不提交配置副本。临时 JSON 文档使用局部 RAII 清理器：
迭代删除叶节点后再释放空容器，不申请遍历栈、不递归，避免依赖库析构非空容器时
申请内存导致 terminate。清理需要 O(节点数 × 深度) 的最坏时间，只用于配置路径。
回归覆盖 Host 重载的 256 个分配位置，以及包含嵌套数组/对象的非法根节点的 128 个位置。
这不是任意 JSON 解析内部状态、任意集成属性副本及持续内存耗尽的完整恢复证明。

`load_configuration()` 在函数入口统一转换可传播异常：分配失败返回
`not_enough_memory`，其他未分类异常返回 `io_error`。解析和字段转换不能把
`bad_alloc` 吞成 `invalid_argument`。调用前的参数构造不在此边界内；故障注入
需先准备 `optional<filesystem::path>`，再覆盖加载函数本身。

## 停机顺序

停机先撤销 readiness，然后停止接收、排空在途 HTTP、取消并等待受监管任务、逆拓扑关闭已成功启动的服务，最后 flush Trace/Metric/Log 队列。

HTTP 请求排空返回超时时，host 保留 `timed_out`（不覆盖更早的错误），并立即中止业务和
管理连接的 socket I/O；后续 handler 完成不会把此次停机改报成功。socket 中止不能取消
任意 handler 等待，也不代表 handler 已释放服务引用。host 随后在已有整体停机截止时间内
等待被跟踪的 handler 结束，再关闭后台任务和服务；服务清理重试也不会在 handler 仍在途时
调用服务 `stop()`。预算耗尽仍有 handler 时保留服务登记。此保护不等于已解决残留协程和
资源的最终安全析构，也不覆盖未经过请求跟踪中间件的自定义工作。

排空超时还调用 `shutdown_handler::cancel_requests()`，取消被跟踪请求现有的
`request_context::cancel_pending_operations()`，同时取消直接令牌和已登记的
`request.with_deadline()` 子操作。登记节点位于协程帧中，退出时以 RAII 移除。
每个子操作仍有独立令牌；工厂按值保存在包装协程中，支持临时及仅可移动工厂。
取消是请求级终态，之后启动的子操作收到已取消令牌。请求必须活到所有子操作退出。
直接调用 `cancellation_token().cancel()` 仅取消直接令牌，不向子操作广播；请求级取消
应调用 `cancel_pending_operations()`。未传递令牌的等待不会自动停止。
取消后的子操作与被跟踪 handler 在移除登记前投递回事件循环，异常退出也保留这一边界并
重新传播原始异常，避免同步恢复的完成路径在取消调用栈中重入登记锁。未取消的完成路径
不增加这次投递。请求取消先原子发布终态，重复请求级取消直接返回；取消后创建的子操作
不进入登记链表，直接得到已取消令牌。锁内再次检查终态以处理并发登记竞争。
`shutdown_handler::cancel_requests()` 也先发布停止接收和取消终态，再广播取消；重复调用
（包括取消回调重入）直接返回，不代表首次广播已完成。新请求沿原有停机入口返回 503，
不新增正常请求入口的状态读取。登记过程中补发取消在释放登记锁后执行。
回归覆盖同步恢复后再次取消整个 shutdown handler、拒绝新请求、再次取消请求并启动
嵌套子操作。任意自定义回调及跨线程生命周期仍需进一步验证；广播期间仍持有登记锁，
不应将这一回归解释为允许任意同步重入事件循环或销毁请求。

host 在现有清理截止时间内重试仍登记的服务，退避从 1ms 增至最多 20ms；已成功关闭的服务不重复关闭。最终若仍有服务登记或活动 HTTP 连接，状态为 `cleanup_failed` 而非 `stopped`，保留原始运行错误。该状态是失败诊断，不保证任意未协作资源可安全析构；这部分仍需调用者和组件的取消/关闭契约配合。

最终连接收尾同时检查 telemetry 的取消投递是否完成，在既有预算内继续驱动完成通知；
未完成的 telemetry 工作也会阻止状态变为 stopped。单纯投递失败仍不覆盖业务结果。
该检查只发生在停机路径，不增加正常业务请求的检查或协程。

`host.retry_cleanup(timeout)` 在 `run()` 返回 cleanup_failed 后，由拥有线程独占调用。
调用方先解除可解除的占用（如归还连接租约），再传入正数预算；Host 重启同一个事件循环，
只重试清理，不重新启动监听或服务。已停止时重复调用成功，已成功关闭的依赖不会重复关闭。
返回值表示本次清理是否完成，不替换原始 run 结果；最初的业务失败仍应由调用方处理。
每次显式重试使用新预算并预留其中 20% 给最终连接收尾。顶层协程尚未结束时拒绝重试，
不能借此重入正在运行的生命周期；不协作操作、逃逸引用和并发析构仍须遵守所有权契约。

`service_lifecycle::stop(deadline budget = {})` 接收绝对截止时间，每个组件使用独立取消令牌。到期请求取消后仍等待组件协程安全退出；组件必须响应取消，框架不会强行销毁悬挂协程。关闭失败的组件及其传递依赖保留所有权，无关组件继续关闭，后续调用可重试清理。已成功关闭的组件不再重复关闭。

组件原始 `stop()` 返回成功时先移除资源登记，再转换截止时间结果。迟到完成仍向
调用者返回超时，并保留错误诊断，但组件状态为 stopped，后续收尾不重复调用 stop。
只有原始关闭失败的组件才继续保留登记；不能把超时报告等同于资源一定尚未释放。

`service_lifecycle::start(std::chrono::milliseconds rollback_reserve = {})` 可为调用方的后续收尾保留回滚预算；默认零，不改变独立生命周期调用的预算。预留必须非负且不超过整体关闭超时，否则启动前返回 `invalid_argument`。Application 传入整体关闭预算的 20%；内部回滚使用较早截止时间，`rollback_deadline()` 仍返回原始整体回滚截止时间，供后续清理共享。预留不能强制终止不响应取消的服务。

当前 HTTP 排空、服务关闭及 telemetry flush 的投递预算共享正常停机截止时间；flush 不会重新获得完整配置时长。HTTP 绑定失败的清理以及服务启动失败的内部回滚，也将已建立的清理截止时间传给 host 的 flush；`rollback_deadline()` 可读取最近一次回滚预算，每次新启动前重置。投递时间归零后仍保留至少 1ms 取消收尾机会。最终连接等待及中止后的等待也受同一整体截止时间约束，不会重新获得完整 drain/stop 时长；等待保留亚毫秒精度。任务等待、预算耗尽后的残留资源析构及异常清理路径仍需进一步验证；不得将配置的超时解释为已经验证的进程级硬退出上限。

正常停机的请求排空、handler 收尾、服务关闭及其重试、telemetry 投递使用较早的清理截止时间，为最终连接收尾保留配置整体预算的 20%；各阶段不会重新计算出一段完整预算。最终连接等待又为 socket 中止后的完成回调预留进入该阶段时剩余预算的 20%，而不是等整体预算耗尽才中止连接。50ms 整体预算、200ms 阶段上限、依赖关闭等待 10 秒并响应取消的半截 HTTP 请求回归覆盖该路径。未协作的任务等待、启动回滚和异常路径仍可能耗尽预算，不能据此推断所有路径已具备硬退出上限。

请求排空每次等待取 50ms 与剩余预算中的较小值，零预算不投递等待。`drain()` 的睡眠
回调应接受 `std::chrono::steady_clock::duration`（推荐泛型 `auto duration`），避免截断
亚毫秒剩余时间。预算限制的是投递等待时长，不保证操作系统调度不会产生额外延迟。

## 设计模式与边界

- `application_builder`：Builder；负责声明配置和组合。
- `application_host`：Facade / Composition Root；独占运行时资源。
- `service_registry`：Registry；只在构建阶段可变。
- `managed_service`：Adapter；统一异构基础设施生命周期。
- `service_lifecycle`：依赖图协调器与补偿事务。
- `task_supervisor`：Supervisor；负责重启、预算和错误传播。
- `health_registry`：状态机和缓存视图。
- 自动装配 registry：显式启用的 Strategy 集合。

业务模块应继续通过构造函数依赖明确接口，不要把 `service_registry` 当作全局 Service Locator。
<!-- END SOURCE: skill/infra/application.md -->

<!-- BEGIN SOURCE: skill/infra/architecture.md -->
# Source: `skill/infra/architecture.md`

# 项目架构

> cnetmod v2.0.0 — 基于 C++23 Modules 的跨平台异步网络库。

## 项目概述

| 属性 | 值 |
|------|------|
| 名称 | cnetmod |
| 版本 | 2.0.0（`CNETMOD_VERSION_STRING "2.0.0"`） |
| 语言标准 | C++23（`CMAKE_CXX_STANDARD 23`） |
| 构建系统 | CMake 3.28+，`CMAKE_CXX_SCAN_FOR_MODULES ON` |
| 库类型 | 静态库 `cnetmod_core`（别名 `cnetmod::core`） |
| 描述 | Cross-platform asynchronous network library with C++23 modules |

## 目录结构

```
cnetmod/
├── include/cnetmod/        # 传统头文件（仅 config.hpp, version.hpp, orm.hpp）
├── src/                    # C++23 模块源码
│   ├── core/               # 核心模块（error, buffer, address, socket, log, dns...）
│   ├── coro/               # 协程模块（task, spawn, timer, channel, mutex...）
│   ├── io/                 # I/O 上下文（io_context, io_operation）
│   ├── executor/           # 执行器（async_op, scheduler, pool）
│   ├── protocol/           # 协议模块（http, mqtt, grpc, redis, mysql...）
│   ├── database/           # 数据库通用模块
│   ├── security/           # 安全模块
│   ├── utils/              # 工具模块
│   ├── core.cppm           # core 聚合模块
│   ├── coro.cppm           # coro 聚合模块
│   ├── io.cppm             # io 聚合模块
│   ├── executor.cppm       # executor 聚合模块
│   └── main.cpp            # 主程序入口
├── examples/               # 示例程序
├── testing/                # 测试、基准测试
│   ├── tests/
│   ├── bench/
│   ├── messaging/
│   └── database/
├── 3rdparty/               # 第三方依赖
│   ├── json/               # nlohmann/json
│   ├── jwt-cpp/            # JWT 编解码
│   ├── leveldb/            # LevelDB 嵌入式存储
│   ├── pugixml/            # XML 解析
│   ├── spdlog/             # 日志（内部使用）
│   └── stdexec/            # P2300 std::execution 实现
├── cmake/                  # CMake 辅助模块
│   ├── Protocols.cmake     # 协议开关注册
│   ├── 3rdparty/           # 第三方依赖配置
│   └── utils/              # 工具函数
└── skill/                  # AI 辅助开发文档
```

## 模块依赖层次

模块按层次从低到高组织，上层依赖下层：

```
core (基础)
  ↓
coro (协程原语)
  ↓
io (I/O 上下文)
  ↓
executor (异步执行器)
  ↓
protocol (协议实现)
```

### core 层（11 个子模块）

`error` → `buffer` → `buffer_pool` → `address` → `socket` → `net_init` → `file` → `serial_port` → `log` → `dns` → `crash_dump`

提供错误码、缓冲区管理、网络地址、套接字、文件 I/O、日志等基础能力。

### coro 层（13 个子模块）

`task` → `spawn` → `timer` → `cancel` → `awaitable` → `bridge` → `channel` → `mutex` → `shared_mutex` → `semaphore` → `wait_group` → `retry` → `circuit_breaker`

提供协程调度、同步原语、重试与熔断等并发控制。

### io 层（2 个子模块）

`io_context` → `io_operation`

封装平台 I/O 多路复用后端。

### executor 层（3 个子模块）

`async_op` → `scheduler` → `pool`

公开层是 cnetmod 自有的协程调度与线程池接口；P2300 实现仅保留在普通 `.cpp` 的内部后端。

### protocol 层（18 个协议模块）

HTTP、WebSocket、gRPC、MQTT、Redis、MySQL、PostgreSQL、MongoDB、Kafka、AMQP091、AMQP10、Modbus、CoAP、DNS、Mail、OpenAI、RAFT、SOCKS5

## 平台 I/O 后端

| 平台 | 后端 | CMake 检测变量 | 宏定义 |
|------|------|----------------|--------|
| Windows | IOCP | 自动（`WIN32`） | `CNETMOD_HAS_IOCP` |
| Linux | io_uring | `check_include_file_cxx("liburing.h")` | `CNETMOD_HAS_IO_URING` |
| Linux | io_uring buffer ring | 编译测试 | `CNETMOD_HAS_IO_URING_BUFFER_RING` |
| Linux | epoll | 自动（`UNIX`） | `CNETMOD_HAS_EPOLL` |
| macOS | kqueue | 自动（`APPLE`） | `CNETMOD_HAS_KQUEUE` |

链接依赖：Windows 链接 `ws2_32 mswsock`，Linux io_uring 链接 `uring`。

## CMake 协议开关

所有协议通过 `cmake/Protocols.cmake` 统一注册，每个协议对应一个 CMake option：

```cmake
-DCNETMOD_ENABLE_ALL_PROTOCOLS=ON|OFF   # 全部协议的默认值
-DCNETMOD_ENABLE_ORM=ON|OFF             # SQL ORM 和 XML mapper 支持
```

### 18 个协议开关一览

| CMake Option | 目录 | 依赖 |
|-------------|------|------|
| `CNETMOD_ENABLE_HTTP` | `http` | — |
| `CNETMOD_ENABLE_WEBSOCKET` | `websocket` | HTTP |
| `CNETMOD_ENABLE_GRPC` | `grpc` | HTTP |
| `CNETMOD_ENABLE_MQTT` | `mqtt` | HTTP, WEBSOCKET |
| `CNETMOD_ENABLE_REDIS` | `redis` | — |
| `CNETMOD_ENABLE_MYSQL` | `mysql` | — |
| `CNETMOD_ENABLE_POSTGRESQL` | `postgresql` | — |
| `CNETMOD_ENABLE_MONGODB` | `mongodb` | — |
| `CNETMOD_ENABLE_KAFKA` | `kafka` | — |
| `CNETMOD_ENABLE_AMQP091` | `amqp091` | — |
| `CNETMOD_ENABLE_AMQP10` | `amqp10` | — |
| `CNETMOD_ENABLE_MODBUS` | `modbus` | — |
| `CNETMOD_ENABLE_COAP` | `coap` | — |
| `CNETMOD_ENABLE_DNS` | `dns` | HTTP |
| `CNETMOD_ENABLE_MAIL` | `mail` | — |
| `CNETMOD_ENABLE_OPENAI` | `openai` | HTTP |
| `CNETMOD_ENABLE_RAFT` | `raft` | — |
| `CNETMOD_ENABLE_SOCKS5` | `socks5` | — |

CMake 会自动验证依赖关系：若启用了某协议但未启用其依赖，构建会报 `FATAL_ERROR`。

## 构建命令

```bash
# 配置（WSL/Linux）
cmake -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCNETMOD_ENABLE_ALL_PROTOCOLS=ON

# 构建
cmake --build build

# 运行测试
ctest --test-dir build

# 安装
cmake --install build --prefix install
```

MSVC 构建使用 `rebuild_install.bat` 脚本。

## 第三方依赖

| 依赖 | 目录 | 用途 |
|------|------|------|
| nlohmann/json | `3rdparty/json` | JSON 序列化 |
| jwt-cpp | `3rdparty/jwt-cpp` | JWT 令牌编解码 |
| LevelDB | `3rdparty/leveldb` | 嵌入式键值存储 |
| pugixml | `3rdparty/pugixml` | XML 解析（ORM mapper） |
| spdlog | `3rdparty/spdlog` | 日志后端 |
| stdexec | `3rdparty/stdexec` | P2300 std::execution 实现 |

可选系统依赖：OpenSSL（`CNETMOD_HAS_SSL`）、zlib（`CNETMOD_HAS_ZLIB`）、LZ4（`CNETMOD_HAS_LZ4`）、ICU（`CNETMOD_HAS_ICU`）。

## 性能优化

| 优化项 | CMake Option | 说明 |
|--------|-------------|------|
| mold 链接器 | `CNETMOD_USE_MOLD=ON` | Linux 下 10-20x 更快链接 |
| mimalloc 分配器 | `CNETMOD_USE_MIMALLOC=ON` | 2-3x 更快内存分配 |

## 参考源码
- `CMakeLists.txt` — 根构建文件（项目配置、平台检测、目标定义）
- `include/cnetmod/config.hpp` — 平台/功能宏定义
- `include/cnetmod/version.hpp` — 版本信息（`CNETMOD_VERSION_STRING "2.0.0"`）
- `cmake/Protocols.cmake` — 18 个协议开关注册与依赖验证
<!-- END SOURCE: skill/infra/architecture.md -->

<!-- BEGIN SOURCE: skill/infra/code-style.md -->
# Source: `skill/infra/code-style.md`

# 代码风格指南

> cnetmod 项目的代码格式与命名规范，基于 `.clang-format` 配置。

## 缩进与空白

- **缩进宽度**：4 空格（`IndentWidth: 4`）
- **Tab**：禁止使用，全部转换为空格（`UseTab: Never`）
- **Tab 显示宽度**：4（`TabWidth: 4`）
- **续行缩进**：4 空格（`ContinuationIndentWidth: 4`）
- **构造函数初始化列表缩进**：4 空格（`ConstructorInitializerIndentWidth: 4`）
- **访问修饰符缩进**：与类体对齐，不额外缩进（`AccessModifierOffset: -4`）

## 大括号风格

使用 **Allman 风格**（`BreakBeforeBraces: Custom`）——左大括号独占一行：

```cpp
if (condition)
{
    do_something();
}
else
{
    do_other();
}

class my_class
{
public:
    my_class()
    {
    }
};
```

具体规则（`BraceWrapping`）：

| 场景 | 行为 |
|------|------|
| `AfterClass` | 换行 |
| `AfterFunction` | 换行 |
| `AfterStruct` | 换行 |
| `AfterControlStatement` | Always（换行） |
| `AfterEnum` | 换行 |
| `AfterNamespace` | **不换行** |
| `BeforeCatch` | 换行 |
| `BeforeElse` | 换行 |
| `BeforeWhile` | **不换行**（do-while） |

## 命名约定

| 类别 | 风格 | 示例 |
|------|------|------|
| 类 / 结构体 | snake_case | `dynamic_buffer`, `buffer_reader` |
| 枚举类 | snake_case | `byte_order`, `open_mode` |
| 函数 | snake_case | `read_u16_be()`, `set_level()` |
| 变量 | snake_case | `read_pos_`, `initial_capacity` |
| 类成员变量 | snake_case + `_` 后缀 | `data_`, `size_`, `alignment_` |
| 命名空间 | snake_case | `cnetmod`, `detail`, `logger` |
| 宏 | UPPER_SNAKE_CASE | `CNETMOD_HAS_SSL`, `CNETMOD_PLATFORM_WINDOWS` |

```cpp
export class aligned_buffer
{
public:
    explicit aligned_buffer(std::size_t size,
        std::size_t alignment = 4096);

    [[nodiscard]] auto data() noexcept -> std::byte*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    std::byte* data_ = nullptr;       // 成员后缀 _
    std::size_t size_ = 0;
    std::size_t alignment_ = 0;
};
```

## `[[nodiscard]]` 标注

对以下函数**必须**添加 `[[nodiscard]]`：

- 返回资源句柄或指针的函数（`data()`, `writable()`, `readable()`）
- 可能失败的函数（返回 `bool`、`std::expected`、`std::optional`）
- 返回查询结果的 `const` 成员函数（`size()`, `remaining()`, `position()`）

```cpp
[[nodiscard]] auto data() noexcept -> std::byte*;
[[nodiscard]] auto size() const noexcept -> std::size_t;
[[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer;
[[nodiscard]] auto remaining() const noexcept -> std::size_t;
```

## 类结构顺序

按 `public → protected → private` 排列，访问修饰符与类体同级缩进：

```cpp
export class dynamic_buffer
{
public:
    explicit dynamic_buffer(std::size_t initial_capacity = 4096);
    [[nodiscard]] auto prepare(std::size_t n) -> mutable_buffer;
    void commit(std::size_t n) noexcept;
    [[nodiscard]] auto data() const noexcept -> const_buffer;
    void consume(std::size_t n) noexcept;
    [[nodiscard]] auto readable_bytes() const noexcept -> std::size_t;

private:
    std::vector<std::byte> data_;
    std::size_t read_pos_ = 0;
    std::size_t write_pos_ = 0;
};
```

## 指针与引用对齐

- **指针**：左对齐（`PointerAlignment: Left`）

```cpp
std::byte* data_ = nullptr;
const void* data = nullptr;
```

## 行宽限制

- **ColumnLimit: 0** — clang-format 不强制行宽限制
- 建议保持合理的行宽（约 100-120 字符），以提高可读性

## import / include 顺序

1. `module;` 全局片段中的 `#include`（仅 config.hpp 和平台头）
2. `export module` 声明
3. `import std;`
4. `export import :分区;` 或其他模块导入
5. 代码正文

```cpp
module;

#include <cnetmod/config.hpp>          // 1. 配置头

export module cnetmod.core.error;

import std;                             // 3. 标准库

namespace cnetmod {
// 5. 代码正文
}
```

- `IncludeBlocks: Preserve` — 保持原有 include 分组
- `SortIncludes: CaseSensitive` — include 按大小写敏感排序

## 其他格式规则

| 规则 | 值 | 说明 |
|------|------|------|
| `AllowShortFunctionsOnASingleLine` | Empty | 仅空函数可单行 |
| `AllowShortBlocksOnASingleLine` | Empty | 仅空块可单行 |
| `AllowShortIfStatementsOnASingleLine` | Never | if 不允许单行 |
| `AllowShortLoopsOnASingleLine` | false | 循环不允许单行 |
| `BinPackArguments` | true | 函数参数尽量紧凑 |
| `BinPackParameters` | true | 函数形参尽量紧凑 |
| `MaxEmptyLinesToKeep` | 1 | 最多保留 1 个空行 |
| `SeparateDefinitionBlocks` | Always | 定义块之间用空行分隔 |
| `BreakBeforeTernaryOperators` | true | 三元运算符前换行 |
| `BreakConstructorInitializers` | BeforeColon | 初始化列表在冒号前换行 |
| `NamespaceIndentation` | Inner | 命名空间内部缩进 |
| `FixNamespaceComments` | true | 自动添加命名空间结束注释 |

## 返回类型风格

项目使用**后置返回类型**（trailing return type）风格：

```cpp
auto data() noexcept -> std::byte*;
auto read_u16_be() noexcept -> std::optional<std::uint16_t>;
auto prepare(std::size_t n) -> mutable_buffer;
```

简单函数也可直接声明返回类型：

```cpp
void commit(std::size_t n) noexcept;
bool skip(std::size_t n) noexcept;
```

## 参考源码
- `.clang-format` — 完整的格式化配置
- `src/core/buffer.cppm` — 类结构、命名、`[[nodiscard]]` 示例
- `src/core/log.cppm` — 命名空间、函数签名示例
- `src/core/error.cppm` — 枚举定义、全局片段示例
<!-- END SOURCE: skill/infra/code-style.md -->

<!-- BEGIN SOURCE: skill/infra/module-conventions.md -->
# Source: `skill/infra/module-conventions.md`

# C++23 模块规范

> cnetmod 项目中 `.cppm` 模块接口与 `.cpp` 实现文件的编写约定。

## 文件结构

每个模块由两部分组成：

| 文件 | 后缀 | 作用 |
|------|------|------|
| 模块接口 | `.cppm` | 声明 `export module`，导出公开 API |
| 模块实现 | `.cpp` | 以 `module cnetmod.xxx;`（无 `export`）开始，提供具体定义 |

实现文件命名惯例：`<name>_impl.cpp` 或 `<name>.cpp`，放在对应 `.cppm` 同目录下。

## 模块声明语法

### 基本模块

```cpp
export module cnetmod.core.buffer;
```

命名层次为 `cnetmod.<层级>.<模块名>`，层级包括 `core`、`coro`、`io`、`executor`、`protocol` 等。

### 协议内部分区

协议模块可使用冒号分区组织子功能：

```cpp
export module cnetmod.protocol.http:server;
```

### 聚合模块

聚合模块通过 `export import` 将多个子模块组合为一个入口，用户只需一次 import 即可获得全部功能。

以 `src/core.cppm` 为例：

```cpp
export module cnetmod.core;

export import cnetmod.core.error;
export import cnetmod.core.buffer;
export import cnetmod.core.buffer_pool;
export import cnetmod.core.address;
export import cnetmod.core.socket;
export import cnetmod.core.net_init;
export import cnetmod.core.file;
export import cnetmod.core.serial_port;
export import cnetmod.core.log;
export import cnetmod.core.dns;
export import cnetmod.core.crash_dump;
```

当前项目中的聚合模块：

| 聚合模块 | 子模块数量 | 说明 |
|----------|-----------|------|
| `cnetmod.core` | 11 | 错误码、缓冲区、地址、套接字、日志等 |
| `cnetmod.coro` | 13 | task、spawn、timer、channel、mutex 等 |
| `cnetmod.executor` | 3 | async_op、scheduler、pool |
| `cnetmod.io` | 2 | io_context、io_operation |

## `import std;` 规则

**所有模块接口和实现文件中，标准库一律使用 `import std;`，禁止 `#include` 标准库头文件。**

```cpp
// 正确
import std;

// 错误 — 禁止在模块中使用
#include <vector>
#include <string>
#include <format>
```

使用 `std::println` / `std::format` 替代 iostream。

### Clang 22 `std::format` visibility caveat

Every translation unit that calls a standard-library facility must import
`std` directly and before cnetmod aggregate modules:

```cpp
import std;
import cnetmod.protocol.openai;

auto text = std::format("request-{}", request_id);
```

Do not rely on a private `import std;` from an imported cnetmod module. With
Clang 22 module visibility, importing `cnetmod.protocol.openai` without an
earlier direct `import std;` can leave an incomplete overload set at a later
`std::format` call. A narrow string literal may then be diagnosed against the
wide-character overload. Replacing `std::format` with string concatenation is
only a local workaround; it does not repair the translation unit's standard
library visibility.

## 全局片段（Global Module Fragment）

当需要引入平台头文件或项目配置头时，使用 `module;` 开头的全局片段：

```cpp
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
    #include <WinSock2.h>
#endif

export module cnetmod.core.error;

import std;
```

### 头文件例外

仅以下两个传统头文件允许通过 `#include` 引入：

| 头文件 | 用途 |
|--------|------|
| `cnetmod/config.hpp` | 平台检测宏、I/O 后端检测、系统头配置 |
| `cnetmod/orm.hpp` | ORM 层的传统宏定义 |

其余所有标准库功能必须通过 `import std;` 获取。

## `export` 规则

- **`export`**：标记需要对外暴露的类型、函数、变量、命名空间
- **无 `export`**：仅限模块内部使用的实现细节

```cpp
export namespace cnetmod {

// 公开 API — 使用 export
export struct const_buffer
{
    const void* data = nullptr;
    std::size_t size = 0;
};

// 内部辅助 — 不加 export
namespace detail {
    constexpr auto bswap16(std::uint16_t v) noexcept -> std::uint16_t
    {
        return static_cast<std::uint16_t>((v >> 8) | (v << 8));
    }
} // namespace detail

} // namespace cnetmod
```

`export namespace` 可以包裹整个公开 API 区域，其内部的所有声明自动导出。

## 子模块导出

聚合模块内部可以使用 `export import :分区名;` 导入自身分区：

```cpp
export module cnetmod.core.log;

import std;
export import :config;   // 导入同模块的 :config 分区并重新导出
```

## namespace 约定

所有公开 API 放在 `namespace cnetmod { ... }` 内：

```cpp
export module cnetmod.core.buffer;

import std;

namespace cnetmod {

export struct const_buffer { /* ... */ };
export struct mutable_buffer { /* ... */ };

} // namespace cnetmod
```

内部实现细节放在 `namespace detail { ... }` 子命名空间中。

日志模块是例外——它使用独立的 `namespace logger { ... }`。

## 条件编译

可选依赖使用宏保护：

```cpp
#ifdef CNETMOD_HAS_SSL
    // SSL 相关 API
#endif
```

常用条件编译宏：

| 宏 | 含义 |
|----|------|
| `CNETMOD_HAS_SSL` | OpenSSL 可用 |
| `CNETMOD_HAS_IOCP` | Windows IOCP 后端 |
| `CNETMOD_HAS_EPOLL` | Linux epoll 后端 |
| `CNETMOD_HAS_KQUEUE` | macOS kqueue 后端 |
| `CNETMOD_HAS_IO_URING` | Linux io_uring 可用 |
| `CNETMOD_HAS_PROTOCOL_XXX` | 特定协议模块已启用 |

## 参考源码
- `src/core.cppm` — 聚合模块示例（export import 子模块）
- `src/coro.cppm` — 协程聚合模块
- `src/executor.cppm` — 执行器聚合模块
- `src/io.cppm` — I/O 聚合模块
- `src/core/log.cppm` — 典型 .cppm 接口（含分区导出）
- `src/core/buffer.cppm` — 典型 .cppm 接口（含 namespace、export 规则）
- `src/core/error.cppm` — 全局片段 + 平台头文件引入示例
<!-- END SOURCE: skill/infra/module-conventions.md -->

<!-- BEGIN SOURCE: skill/infra/new-module-guide.md -->
# Source: `skill/infra/new-module-guide.md`

# 新增模块指南

> 在 cnetmod 项目中添加核心模块、协议模块、示例和测试的完整步骤。

## 新增核心模块

核心模块位于 `src/core/`、`src/coro/`、`src/io/`、`src/executor/` 等目录下。

### 步骤 1：创建模块接口文件

```cpp
// src/core/my_feature.cppm
module;

#include <cnetmod/config.hpp>

export module cnetmod.core.my_feature;

import std;

namespace cnetmod {

export class my_feature
{
public:
    explicit my_feature(std::string name);
    [[nodiscard]] auto name() const noexcept -> std::string_view;

private:
    std::string name_;
};

} // namespace cnetmod
```

### 步骤 2：创建实现文件

```cpp
// src/core/my_feature_impl.cpp
module cnetmod.core.my_feature;

import std;

namespace cnetmod {

my_feature::my_feature(std::string name)
    : name_(std::move(name))
{
}

auto my_feature::name() const noexcept -> std::string_view
{
    return name_;
}

} // namespace cnetmod
```

### 步骤 3：注册到聚合模块

编辑 `src/core.cppm`，添加 `export import` 行：

```cpp
export module cnetmod.core;

export import cnetmod.core.error;
// ... 已有子模块 ...
export import cnetmod.core.my_feature;   // ← 新增
```

### 步骤 4：CMake 自动收集

根 `CMakeLists.txt` 使用 `file(GLOB_RECURSE ... "src/*.cppm")` 和 `file(GLOB_RECURSE ... "src/*.cpp")` 自动收集源文件，**无需手动注册**。重新 configure 即可识别新文件。

## 新增协议模块

协议模块需要额外的 CMake 注册和条件编译配置。

### 步骤 1：在 Protocols.cmake 注册

编辑 `cmake/Protocols.cmake`，在 `CNETMOD_PROTOCOLS` 列表中添加协议名：

```cmake
set(CNETMOD_PROTOCOLS
    # ... 已有协议 ...
    MY_PROTOCOL              # ← 新增
)
```

设置目录映射和依赖：

```cmake
set(CNETMOD_PROTOCOL_MY_PROTOCOL_DIRECTORY "my_protocol")
# 可选依赖声明：
# set(CNETMOD_PROTOCOL_MY_PROTOCOL_DEPENDS HTTP)
```

这一步自动生成：
- CMake option `CNETMOD_ENABLE_MY_PROTOCOL`
- 编译宏 `CNETMOD_HAS_PROTOCOL_MY_PROTOCOL`

### 步骤 2：创建协议目录与文件

```
src/protocol/my_protocol/
├── my_protocol.cppm          # 协议接口
├── my_protocol_impl.cpp      # 协议实现
└── client.cppm               # 客户端/服务端子模块（可选）
```

接口文件格式与核心模块一致（`module;` → `#include <cnetmod/config.hpp>` → `export module` → `import std;`），额外 import 所依赖的聚合模块（`cnetmod.core`、`cnetmod.coro`、`cnetmod.io`）。

### 步骤 3：创建聚合模块（可选）

如果协议有多个子模块，在 `src/protocol/` 下创建聚合文件：

```cpp
// src/protocol/my_protocol.cppm
export module cnetmod.protocol.my_protocol;

export import cnetmod.protocol.my_protocol.client;
export import cnetmod.protocol.my_protocol.server;
```

### 步骤 4：条件编译

`cnetmod_filter_disabled_protocol_sources()` 会自动过滤被禁用协议的源文件。当 `CNETMOD_ENABLE_MY_PROTOCOL=OFF` 时，该目录下所有文件不参与编译。

可选依赖的链接在 `cmake/3rdparty/ThirdPartyDependencies.cmake` 中配置。

## 添加示例

示例程序放在 `examples/` 目录下，按类别分子目录。

### 创建示例文件

```cpp
// examples/my_protocol/my_demo.cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.my_protocol;

auto main() -> int
{
    std::println("my_protocol demo");
    return 0;
}
```

### 在 examples/CMakeLists.txt 注册

在条件块中创建新列表，并加入 `ALL_EXAMPLES`：

```cmake
set(MY_PROTOCOL_EXAMPLES)
if(CNETMOD_ENABLE_MY_PROTOCOL)
    list(APPEND MY_PROTOCOL_EXAMPLES my_protocol/my_demo)
endif()

set(ALL_EXAMPLES
    ${CORE_EXAMPLES}
    # ... 已有列表 ...
    ${MY_PROTOCOL_EXAMPLES}     # ← 新增
)
```

CMake 自动为每个示例创建 `example_<name>` 目标，链接 `cnetmod_core`。

## 添加测试

测试放在 `testing/tests/`、`testing/bench/`、`testing/messaging/`、`testing/database/` 等目录下。

### 测试文件示例

```cpp
// testing/tests/test_my_feature.cpp
import std;
import cnetmod.core;

auto main() -> int
{
    auto feature = cnetmod::my_feature("test");
    assert(feature.name() == "test");
    std::println("test_my_feature: PASSED");
    return 0;
}
```

### CMake 注册

在对应子目录的 `CMakeLists.txt` 中添加：

```cmake
add_executable(test_my_feature test_my_feature.cpp)
target_link_libraries(test_my_feature PRIVATE cnetmod_core)
set_property(TARGET test_my_feature PROPERTY CXX_MODULE_GENERATION_MODE "SEPARATE")
add_test(NAME test_my_feature COMMAND test_my_feature)
list(APPEND CNETMOD_TEST_TARGETS test_my_feature)
set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
```

## CMake 注册细节总结

| 操作 | 手动注册 | 说明 |
|------|----------|------|
| 新增 core/coro/io/executor 子模块 | 否（GLOB 自动收集） | 需更新聚合模块 `export import` |
| 新增协议模块 | 是（`Protocols.cmake`） | 自动生成 option 和编译宏 |
| 新增示例 | 是（`examples/CMakeLists.txt`） | 按类别分组，条件编译 |
| 新增测试 | 是（`testing/` 子目录） | 追加到 `CNETMOD_TEST_TARGETS` |

### 构建验证

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target cnetmod_core
cmake --build build --target example_my_demo
ctest --test-dir build -R test_my_feature
```

## 参考源码
- `CMakeLists.txt` — 根构建文件（GLOB 自动收集、目标定义）
- `cmake/Protocols.cmake` — 18 个协议注册、依赖声明、源文件过滤
- `src/core.cppm` — 聚合模块（添加子模块的 export import）
- `examples/CMakeLists.txt` — 示例注册（按类别分组、条件编译）
- `testing/CMakeLists.txt` — 测试顶层配置
<!-- END SOURCE: skill/infra/new-module-guide.md -->

<!-- BEGIN SOURCE: skill/infra/observability.md -->
# Source: `skill/infra/observability.md`

# Observability 与 OpenTelemetry

> 通过一个 Telemetry Hub 低侵入地统一 Trace、Metric、Log、W3C 上下文传播和有界 OTLP/HTTP 导出。

**import**: `import cnetmod.observability;`

**源码**: `src/application/monitoring/telemetry.cppm`、`src/application/monitoring/otlp/otlp_http_exporter.cppm`、`src/application/monitoring/integration/http_client.cppm`

监控源码统一归属 `src/application/monitoring/`：`core/` 放协议无关的上下文、
操作结果和指标契约，`otlp/` 放编码与投递，`integration/` 放协议观测适配器。
目录归属不改变模块依赖方向：协议只依赖中立契约，不导入 Application Host 或 OTLP。
模块名保留 `cnetmod.instrumentation.*` / `cnetmod.observability.*`，避免目录整理
同时改变调用方 API。HTTP 关闭时 CMake 仍编译 `monitoring/core/`。

## 核心原则

1. 应用组合根只创建一个 `telemetry_hub`；协议与业务代码不依赖具体 OTEL SDK。
2. 协程上下文以值显式传递，禁止用 thread-local 保存活动 span。
3. Trace、Metric、Log 共用非阻塞提交、有限队列、批量、退避重试、丢弃计数和有界 flush。
4. 遥测失败不改变业务结果；队列满时只增加 dropped 计数。
5. 默认不记录 HTTP 正文、提示词、模型输出、工具参数、SQL 参数、凭据和消息载荷。

## 初始化

```cpp
cnetmod::net_init net;
auto io = cnetmod::make_io_context();
cnetmod::observability::telemetry_hub telemetry{*io, {
    .endpoint = "http://127.0.0.1:4318/v1/traces",
    .metrics_endpoint = "http://127.0.0.1:4318/v1/metrics",
    .logs_endpoint = "http://127.0.0.1:4318/v1/logs",
    .service_name = "orders",
    .service_version = "1.4.0",
    .resource_attributes = {{"service.owner", "platform"}},
    .headers = {{"Authorization", "Bearer token"}},
    // Optional: bridge completed framework logger events into OTLP logs.
    .capture_framework_logs = true,
}};
telemetry.set_sampling_ratio(0.25);
```

不设置 Trace endpoint 时导出器处于关闭状态，本地 OpenMetrics registry 仍可使用。只设置 Trace endpoint 时，Metric 和 Log endpoint 自动从同一 OTLP 基础地址派生。

## Trace

OpenAI 的 `telemetry_listener` 由独立模块 `cnetmod.observability.openai`
提供，使用时显式导入该模块。其公开命名空间仍为 `cnetmod::openai`。
`cnetmod.protocol.openai` 不再导出观测适配器或依赖 OTLP；协议保留通用
`run_listener` 事件接口，Application 在启用观测时装配适配器。
这是模块依赖隔离，不代表整个库已经按 OTEL 开关裁剪，也不是零性能损耗证明。

Application 的 OpenAI 监听器使用 `telemetry.measurements()`，将操作计数、活动数、
耗时、token、重试、拒绝和配置成本送入统一指标出口，由 Hub 执行本地聚合及 OTLP
投递。独立使用可构造 `telemetry_listener{telemetry.measurements(), telemetry.spans()}`。
原有接收 `metrics::registry&` 的构造方式仍只向该 registry 写指标；传入 span
exporter 不会使这些本地指标自动上报。不要同时把同一事件交给两种监听器，否则会重复计数。
内置 wire 回归通过本地 HTTP 接收器验证 Application 模型事件的 token 与耗时指标，
不代表已验证真实供应商请求或所有 GenAI 指标的端到端上报。

HTTP 服务端：

```cpp
server.use(cnetmod::http::tracing::tracing_middleware(
    telemetry.server_tracing()));
```

HTTP 客户端：

```cpp
cnetmod::observability::instrumented_http_client client{
    raw_client, telemetry.spans()};
auto response = co_await client.send(request, parent_context);
```

Redis 和 SQL API 接受 `trace_context + span_exporter`，gRPC metadata 自动注入/提取 `traceparent` 与 `tracestate`。`redis_template` 在统一命令入口产生 CLIENT span；Application 的 `redis_service::make_template()` 自动注入 Hub exporter，调用方只传当前协程的 parent。Pipeline 仍按命令分别结束 span，而网络层只执行一次 exchange。属性不包含 key、value、服务端错误正文或凭据。OpenAI Agent 使用 `telemetry_listener` 记录 GenAI span、token、重试、耗时和估算成本；详细提示词与输出默认关闭。

### Kafka producer 装饰器

`import cnetmod.observability.kafka_producer;` 提供拥有原始 producer 的
`instrumented_kafka_producer`。`send(topic, record, parent, cancellation)` 的 parent
按调用显式提供；空 sink 直接返回原始发送任务。事务、flush、身份查询和 close
委托原始 producer，不改变事务提交边界。移动或析构前必须等待其任务结束。

Application 自动装配把 telemetry sink 注入 `kafka_service`；使用
`service.make_producer(options)` 获得带该 sink 的 producer，原始
`service.client().make_producer()` 仍是不带观测的协议入口。服务须比 producer
及其操作活得更久。当前工厂不自动监管 producer 的任务，也不自动进行停机 flush；
消费者处理范围、事务观测和消息指标仍是独立工作，不应理解为已全部自动接入。

## Metric

Prometheus/OpenMetrics 使用进程内 registry：

```cpp
telemetry.metrics().counter_add("orders_created_total");
router.get("/actuator/prometheus",
    cnetmod::metrics::openmetrics_handler(telemetry.metrics()));
```

需要推送到 OTLP 时提交结构化测量：

```cpp
(void)telemetry.submit_metric({
    .name = "queue.depth",
    .value = 12.0,
    .kind = cnetmod::observability::otel_metric_kind::gauge,
    .unit = "{message}",
    .attributes = {{"messaging.system", "kafka"}},
});
```

Application 框架会自动为服务启动、停止、恢复和健康探测产生本地指标及 OTLP 测量。

## Log

```cpp
(void)telemetry.submit_log({
    .severity = "WARN",
    .body = "dependency recovering",
    .trace_id = trace.trace_id,
    .span_id = trace.span_id,
    .attributes = {{"service.name", "redis"}},
});
```

HTTP access log 在 tracing middleware 启用时自动附加 `trace_id` 与 `span_id`。Application 生命周期日志同时作为 OTLP LogRecord 发送。不要把凭据或业务载荷放进 `body`/attributes。

普通 Logger 默认不注册 OTLP 回调，因而不引入观察者复制、OTLP 队列操作或
thread-local 查找。需要将框架日志导出时才设置 `capture_framework_logs: true`；回调运行在
Logger 的 worker 上，提交失败或队列满不会影响日志落盘。业务代码已经持有调用链上下文时，
使用显式值传递关联日志：

```cpp
import cnetmod.core.log;

logger::log(logger::level::info,
    {.trace_id = trace.trace_id, .span_id = trace.span_id},
    "order persisted");
```

Logger 把 ID 视为不透明字符串，不导入 OTEL，也不维护全局或 thread-local 的 active span；
未关联的 `logger::info` / `warn` 等既有调用路径保持不变。

## 上下文传播

- HTTP 使用 W3C `traceparent`/`tracestate`。
- gRPC 使用 metadata 中的相同字段。
- Kafka、AMQP 等支持头部的消息协议应在 producer 注入，在 consumer 提取；不支持头部的 Redis/SQL 只生成本地 CLIENT span。
- 传入可信边界前会校验 Trace Context，非法值不会继承。
- 消息载体复用时，注入会替换已有追踪头；新上下文的 `tracestate` 为空时删除旧值，避免沿用上一条调用链的供应商状态。
- 消息注入准备失败不会抛出异常或留下半更新的上下文。Kafka 和 MQTT 先准备追踪字段并预留容器空间，再以不抛异常的移动提交；AMQP 预备独立追踪节点，使用相同分配器及不抛异常的字符串比较转移节点。业务字段缓冲区和业务载荷不参与复制；开启传播时仍有追踪字段分配和容器操作成本，关闭路径不应调用注入适配器。

## 导出可靠性

- 每类信号使用有界 MPMC 队列；提交不等待网络。
- 单次 drain 按 `max_batch_size` 批量发送。
- 网络错误及 HTTP 429/502/503/504 指数退避，支持 `Retry-After`。
- 2xx 还必须通过 OTLP JSON acknowledgement 校验；不能只凭 HTTP 状态认为全部接收。
- `partialSuccess` 的拒收数量按 spans、log records、metric data points 分别统计，部分接收禁止整批重试；零拒收警告不扣减成功数。Collector 的诊断文本不进入日志或遥测。
- 导出器显式使用 HTTP/1.1、关闭重定向和 Cookie，网络接收阶段将响应体限制为 64 KiB，
  并限制 chunk framing。超大或非法 framing 会关闭连接并计入无效响应，不重试。
  原始 HTTP client 默认不启用这项额外限制；该选项不覆盖 HTTP/2、HTTP/3。
- 响应 SAX 解析另有限制：64 KiB、16 层容器；无效响应计入 `invalid_responses` 和
  `failed_batches`，不重试。64 KiB 是响应体上限，不是整个连接的总内存上限。
- `statistics()` 提供总 accepted/dropped/exported/retry/failed batch，并分别统计三类信号的 accepted/dropped。
- `exported` 表示 Collector 确认的 wire spans/log records/metric data points；累计指标快照可重复发送旧数据点，不能用 `accepted - exported` 推断丢失。拒收分别查看 `rejected_spans`、`rejected_metric_points`、`rejected_logs`；另有 `partial_batches`、`warning_batches`。
- 资源属性和鉴权头由三个 signal 共享，但不会出现在健康响应或普通日志中。

## 有序关闭

```cpp
// Run on the telemetry hub's owning io_context.
auto settled = co_await telemetry.shutdown(
    std::chrono::seconds{5}, std::chrono::milliseconds{500});
if (settled)
    io->stop();
else
    logger::error{"Telemetry shutdown has not settled: {}", settled.error().message()};
```

`flush()` 只等待已接收工作；超时不取消投递，也不关闭事件接收。`close()` 只停止接收，
不是后台任务 join。停机使用 `shutdown(delivery_timeout, cancellation_timeout)`：
先关闭接收并尝试排空，再取消未完成投递并等待收尾。成功表示任务已空闲、连接已关闭，
不表示每条记录都成功送达；仍需检查失败、拒收、丢弃统计。

`shutdown()` 返回错误时，不能仅因等待超时就停止或销毁事件循环。必须保持 hub 和
io_context 存活，继续处理完成通知并等待收尾；示例中的错误分支故意不调用 `stop()`。
它也不承诺强制终止不协作操作。Application Host 使用自己的整体停机预算安排收尾，
不能把示例的两段独立预算直接解释成 Host 的进程硬退出上限。

`try_settle_shutdown()` 供拥有事件循环的收尾协调器使用：在所属执行线程上取消投递，
不分配内存、不等待；返回 false 时必须继续驱动事件循环，返回 true 表示投递已空闲且
连接已关闭。调用是终态操作，不是普通空闲查询；不得用它检查仍在正常运行的 exporter。
Host 在最终连接收尾阶段也检查此状态，使用已有剩余预算，不因一次 shutdown 等待失败
就立即停止事件循环。这仍不保证预算耗尽后未协作资源的安全析构。
<!-- END SOURCE: skill/infra/observability.md -->

<!-- BEGIN SOURCE: skill/infra/windows-build.md -->
# Source: `skill/infra/windows-build.md`

# Windows 构建与 bundled ICU

使用 Visual Studio 的 CMake generator 构建；Debug 与 Release 必须分别构建，不能混用产物。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug --target cnetmod_build_all
cmake --build build --config Release --target cnetmod_build_all
```

## PostgreSQL 的 ICU 依赖

启用 `CNETMOD_ENABLE_POSTGRESQL=ON` 时，Windows 优先使用 `3rdparty/icu` 的 bundled ICU。ICU 的 Visual Studio 项目按当前 CMake 配置增量构建：

| 配置 | 导入库 | DLL |
|---|---|---|
| Debug | `icuucd.lib`、`icuind.lib` | `icuuc78d.dll`、`icuin78d.dll` |
| Release / RelWithDebInfo / MinSizeRel | `icuuc.lib`、`icuin.lib` | `icuuc78.dll`、`icuin78.dll` |

不要把 Release 的 ICU 库复制或映射给 Debug。这样会在链接测试或示例时出现 `LNK1104`，或引入运行库配置不匹配。若发生缺库，构建依赖目标 `cnetmod_icu`（或直接重建目标）即可由 ICU 的 `allinone.sln` 生成匹配配置的文件。
<!-- END SOURCE: skill/infra/windows-build.md -->

<!-- BEGIN SOURCE: skill/integration/c-api.md -->
# Source: `skill/integration/c-api.md`

# C ABI：Rust / Python 原生扩展边界

`cnetmod_c` 是不依赖 C++ module BMI 的静态 C ABI 库。它应被 Rust crate、
CPython extension 或其他原生扩展链接到最终宿主进程中；不要把完整的模块核心
包装成 Windows DLL 后再通过 `ctypes` 动态加载。

公共头文件：`include/cnetmod/c_api.h`。

## 运行模型

调用方创建 `cnetmod_runtime`，并在所属线程持续调用：

```c
cnetmod_runtime_poll(runtime);    /* 不阻塞，适合外部 event loop */
/* 或 */
cnetmod_runtime_run_one(runtime); /* 等待一个 I/O 或已投递任务 */
```

HTTP 完成回调一定运行在该 runtime 所在线程。除
`cnetmod_http_request_cancel()` 外，ABI 调用必须由同一线程发起；取消可由任意
线程调用，并会传播到 DNS/TCP/TLS/HTTP/2/HTTP/3 的可取消 I/O。

`request_cancel()` 不是仅设置一个逻辑标记：它会请求终止正在等待的底层 I/O，并且
仍然只触发一次 completion（失败时 `response == NULL`）。调用者可在 completion
之后销毁 request/client/runtime；正在执行的请求内部保留所需状态，不会悬空访问。

## 所有权

- `runtime/client/request` 都通过各自 `*_destroy()` 释放；已开始的请求内部持有
  所需状态，因此可以先释放外层 request handle。
- 回调成功时收到的 `cnetmod_http_response*` 由回调拥有，必须调用
  `cnetmod_http_response_free()`。
- 回调失败时 `response == NULL`；`error_message` 仅在该次回调期间有效，若要
  保存必须复制。
- `body` 是字节序列，使用 `cnetmod_http_response_body()` 返回的长度处理，不能
  假设 NUL 结尾。

## C 风格最小请求

```c
static void completed(void* user, cnetmod_http_response* response,
                      int error_code, const char* error_message) {
    if (response) {
        size_t size = 0;
        const uint8_t* bytes = cnetmod_http_response_body(response, &size);
        /* consume bytes[0..size) */
        cnetmod_http_response_free(response);
        return;
    }
    /* copy error_message here if it is needed after this callback */
}

cnetmod_runtime* runtime = cnetmod_runtime_create();
cnetmod_http_client_options options;
cnetmod_http_client_options_default(&options);
cnetmod_http_client* client = cnetmod_http_client_create(runtime, &options);
cnetmod_http_request* request = cnetmod_http_request_start(
    client, CNETMOD_HTTP_GET, "https://example.com/", NULL, 0,
    completed, NULL);
```

驱动 runtime 直到回调完成后，依次销毁 request、client、runtime。
<!-- END SOURCE: skill/integration/c-api.md -->

<!-- BEGIN SOURCE: skill/protocols/amqp091.md -->
# Source: `skill/protocols/amqp091.md`

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
<!-- END SOURCE: skill/protocols/amqp091.md -->

<!-- BEGIN SOURCE: skill/protocols/amqp10.md -->
# Source: `skill/protocols/amqp10.md`

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
<!-- END SOURCE: skill/protocols/amqp10.md -->

<!-- BEGIN SOURCE: skill/protocols/coap.md -->
# Source: `skill/protocols/coap.md`

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
<!-- END SOURCE: skill/protocols/coap.md -->

<!-- BEGIN SOURCE: skill/protocols/grpc.md -->
# Source: `skill/protocols/grpc.md`

# gRPC

> 基于 HTTP/2 的高性能 gRPC 框架，支持 unary/streaming 调用、protobuf 编解码、健康检查、反射及服务治理。

**import**: `import cnetmod.protocol.grpc;`
**CMake**: `-DCNETMOD_ENABLE_GRPC=ON`
**依赖**: `cnetmod.protocol.http`、`cnetmod.io.io_context`、`cnetmod.coro.task`
**源码**: `src/protocol/grpc/`

## 场景导航

- 我要做 gRPC 服务端 → [看这里](#场景1grpc-服务端)
- 我要做 gRPC 客户端 → [看这里](#场景2grpc-客户端)
- 我要做流式调用 → [看这里](#场景2grpc-客户端)
- 我要做认证拦截器 → [看这里](#场景3拦截器)
- 我要做服务治理 → [看这里](#场景4governance-治理)

## 核心类型

**`grpc::status_code`** — 标准 gRPC 状态码：`ok(0)`、`cancelled(1)`、`invalid_argument(3)`、`not_found(5)`、`unavailable(14)`、`unauthenticated(16)` 等 17 种

**`grpc::byte_buffer`** — `std::vector<std::byte>` 类型别名

**`grpc::metadata`** — `std::multimap<std::string, std::string>`，gRPC 请求/响应头

**`grpc::status`** — 调用状态：
```cpp
struct status {
    status_code code; std::string message; metadata trailers;
    auto ok() const noexcept -> bool;
};
```

**`grpc::call_kind`** — 调用类型：`unary`、`client_streaming`、`server_streaming`、`bidi_streaming`

**`grpc::compression_algorithm`** — 压缩算法：`identity`、`gzip`

**`grpc::call_options`** — 调用选项：`headers`、`timeout`、`compression`

**`grpc::call_context`** — 服务端调用上下文：`service`、`method`、`path`、`headers`、`timeout`、`started`、`deadline_exceeded()`

**`grpc::unary_request` / `unary_response`** — Unary 请求/响应载体

**`grpc::streaming_request` / `streaming_response`** — Streaming 请求/响应载体（多消息）

## 客户端超时与取消

`unary_request::timeout` 会写入标准 `grpc-timeout` 请求头，让服务端可从 `call_context` 感知剩余调用时间。该字段是协议级 deadline；客户端如需主动中止本地 HTTP/2 I/O，则使用带 `cancel_token` 的 unary 重载：

```cpp
import cnetmod.coro;
import cnetmod.protocol.grpc.client;

cnetmod::cancel_token token;
cnetmod::grpc::unary_request request{
    .service = "profile.ProfileService",
    .method = "Get",
    .timeout = std::chrono::milliseconds{800},
};
auto response = co_await grpc_client.unary(std::move(request), token);
```

当请求存在统一 `deadline` 时，先用其 `remaining()` 填充 `timeout`，再把同一业务取消源传给 `unary()`；不要只发送 `grpc-timeout` 而让本地 I/O 无限等待。由 deadline 导致的取消映射为 gRPC `deadline_exceeded`，调用方取消映射为 `cancelled`。

## API 参考

### protobuf 编解码

```cpp
// varint 编码
auto encode_varint(std::uint64_t value) -> byte_buffer;
auto decode_varint(std::span<const std::byte> data, std::size_t& pos) -> std::optional<std::uint64_t>;
auto zigzag_encode(std::int64_t value) noexcept -> std::uint64_t;
auto zigzag_decode(std::uint64_t value) noexcept -> std::int64_t;

// 字段序列化
void append_key(byte_buffer& out, std::uint32_t number, wire_type type);
void append_uint64(byte_buffer& out, std::uint32_t number, std::uint64_t value);
void append_string(byte_buffer& out, std::uint32_t number, std::string_view value);
void append_bytes(byte_buffer& out, std::uint32_t number, std::span<const std::byte> value);

// proto schema 解析
auto parse_schema(std::string_view proto_text) -> std::expected<file_def, std::error_code>;
auto decode_message(std::span<const std::byte> data) -> std::expected<std::vector<field>, std::error_code>;
```

### gRPC 帧编解码

```cpp
auto encode_frame(std::span<const std::byte> payload, bool compressed = false) -> std::expected<byte_buffer, std::error_code>;
auto decode_frames(std::span<const std::byte> data) -> std::expected<std::vector<message_frame>, std::error_code>;

// 增量流式解码器
class stream_decoder {
    auto feed(std::span<const std::byte> bytes) -> std::expected<std::vector<message_frame>, std::error_code>;
    auto buffered_bytes() const noexcept -> std::size_t;
};

// 高级编解码器
class message_stream_encoder {
    explicit message_stream_encoder(compression_algorithm compression = compression_algorithm::identity);
    auto encode(std::span<const std::byte> message) -> std::expected<byte_buffer, status>;
};

class message_stream_decoder {
    explicit message_stream_decoder(codec_options options = {});
    auto feed(std::span<const std::byte> bytes) -> std::expected<std::vector<byte_buffer>, status>;
};
```

### `grpc::service_router` — 服务端路由

```cpp
explicit service_router(server_options options);
void add_unary(std::string service, std::string method, unary_handler handler);
void add_client_streaming(std::string service, std::string method, streaming_handler handler);
void add_server_streaming(std::string service, std::string method, server_streaming_handler handler);
void add_bidi_streaming(std::string service, std::string method, streaming_handler handler);
auto make_http_handler() const -> http::handler_fn;
```

**handler 签名**:
```cpp
using unary_handler = std::function<task<std::expected<byte_buffer, status>>(std::span<const std::byte>, const call_context&)>;
using streaming_handler = std::function<task<std::expected<std::vector<byte_buffer>, status>>(std::span<const byte_buffer>, const call_context&)>;
using server_streaming_handler = std::function<task<std::expected<std::vector<byte_buffer>, status>>(std::span<const std::byte>, const call_context&)>;
using server_interceptor = std::function<std::expected<void, status>(const call_context&)>;
```

**`server_options`**: `max_receive_message_bytes`、`max_send_message_bytes`、`accept_gzip`、`interceptors`、`governance`

### `grpc::client` — 客户端

```cpp
explicit client(io_context& ctx, std::string base_url, client_options opts);
auto unary(unary_request req) -> task<std::expected<unary_response, status>>;
auto client_streaming(streaming_request req) -> task<std::expected<unary_response, status>>;
auto server_streaming(unary_request req) -> task<std::expected<streaming_response, status>>;
auto bidi_streaming(streaming_request req) -> task<std::expected<streaming_response, status>>;
void close() noexcept;
```

**`client_options`**: `http`（HTTP/2 选项）、`accept_gzip`、`default_compression`、`request_interceptors`、`response_interceptors`

### 健康检查

```cpp
namespace grpc::health {
    enum class serving_status : std::uint64_t { unknown, serving, not_serving, service_unknown };
    class registry {
        void set(std::string service, serving_status status);
        auto get(std::string_view service) const -> serving_status;
        void set_default(serving_status status);
    };
    void register_service(service_router& router, registry& registry);
}
```

### 反射服务

```cpp
namespace grpc::reflection {
    void install_service(service_router& router, std::vector<std::string> services);
    auto decode_request(std::span<const std::byte> payload) -> std::expected<reflection_request, status>;
    auto encode_list_services_response(std::span<const std::byte> original_request, std::span<const std::string> services) -> byte_buffer;
}
```

### 服务治理（`cnetmod.protocol.grpc.governance.*`）

| 组件 | 类/结构 | 关键 API |
|---|---|---|
| **endpoint** | `endpoint` | `id`, `url`, `weight`, `priority`, `state`, `is_available()` |
| **discovery** | `static_discovery` | `snapshot()`, `replace_snapshot()` |
| **load_balancer** | `load_balancer` | `pick(const static_discovery&) -> std::optional<endpoint>` (加权轮询) |
| **retry** | `retry_policy` | `max_attempts`, `initial_backoff`, `backoff_multiplier`, `allows_retry()`, `backoff_for()` |
| | `retry_budget` | `try_acquire()`, `record_success()`, `available_tokens()` |
| **circuit_breaker** | `circuit_breaker` | `try_acquire()`, `record_success()`, `record_failure()`, `state()` (closed/open/half_open) |
| **admission** | `concurrency_limiter` | `try_acquire() -> std::optional<guard>`, `limit()`, `in_flight()` |
| | `token_bucket` | `try_consume(double tokens)` |
| | `rate_limit_registry` | `set_limit(service, method, rate_limit)`, `try_consume()` |
| **observability** | `call_statistics` | `record_started()`, `record_completed(latency, failed)`, `snapshot()` |
| | `call_statistics_registry` | `for_method(service, method) -> shared_ptr<call_statistics>` |
| **server_policy** | `server_policy` | `begin(call_context) -> expected<call_guard, status>`, `concurrency()`, `rate_limits()`, `statistics()` |
| **governed_client** | `governed_client` | `governed_client(ctx, discovery, balancer, retry, budget, breaker, options)`, `unary(req, idempotent)` |

## 场景 1：gRPC 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;

auto echo_handler(std::span<const std::byte> payload, const grpc::call_context&)
    -> cnetmod::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    co_return grpc::byte_buffer(payload.begin(), payload.end());
}

int main() {
    auto ctx = cnetmod::make_io_context();
    grpc::service_router grpc_router(grpc::server_options{.accept_gzip = true});
    grpc_router.add_unary("example.Echo", "Say", echo_handler);

    cnetmod::http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    cnetmod::http::server srv(*ctx);
    srv.listen("0.0.0.0", 50051);
    srv.set_router(std::move(router));
    cnetmod::spawn(*ctx, srv.run());
    ctx->run();
}
```

## 场景 2：gRPC 客户端

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;

auto run(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    grpc::client cli(ctx, "http://127.0.0.1:50051");
    auto resp = co_await cli.unary({
        .service = "example.Echo",
        .method = "Say",
        .payload = std::vector<std::byte>{std::byte{'h'}, std::byte{'i'}},
        .timeout = std::chrono::milliseconds(5000),
    });
    if (resp && resp->st.ok()) {
        std::println("got {} bytes", resp->payload.size());
    }
}
```

## 场景 3：拦截器

```cpp
import std;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;

auto require_bearer(std::string token) -> grpc::server_interceptor {
    return [token](const grpc::call_context& call) -> std::expected<void, grpc::status> {
        if (grpc::metadata_value(call.headers, "authorization") != token)
            return std::unexpected(grpc::make_status(grpc::status_code::unauthenticated, "bad token"));
        return {};
    };
}

auto inject_bearer(std::string token) -> grpc::client_request_interceptor {
    return [token](grpc::client_call& call) -> std::expected<void, grpc::status> {
        call.headers.emplace("authorization", token);
        return {};
    };
}
```

## 场景 4：governance 治理

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;
namespace gov = cnetmod::grpc::governance;

void governance_demo(cnetmod::io_context& ctx) {
    gov::static_discovery discovery({
        gov::endpoint("svc-1", "http://10.0.0.1:50051", 3),
        gov::endpoint("svc-2", "http://10.0.0.2:50051", 1),
    });
    gov::load_balancer balancer;
    gov::retry_policy retry{.max_attempts = 3, .retryable_status_codes = {grpc::status_code::unavailable}};
    gov::circuit_breaker breaker(gov::circuit_breaker_config{.failure_threshold = 5});

    gov::governed_client client(ctx, discovery, balancer, retry, nullptr, &breaker);
}
```

## 连接池（生产级用法）

### 替代方案：governed_client 多端点管理

gRPC 基于 HTTP/2 多路复用，单个 `grpc::client` 已复用底层 HTTP/2 连接，**无需传统连接池**。生产环境通过 `governed_client` + `static_discovery` + `load_balancer` 实现多端点负载均衡。

**`governed_client` API**（来自 `governed_client.cppm`）：

```cpp
class governed_client {
    governed_client(io_context& ctx, static_discovery& discovery,
        load_balancer& balancer, retry_policy retry = {},
        retry_budget* budget = nullptr,
        circuit_breaker* breaker = nullptr,
        client_options options = {});
    auto unary(unary_request req, bool idempotent = false)
        -> task<std::expected<unary_response, status>>;
};
```

**生产级配置示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;
namespace gov = cnetmod::grpc::governance;

auto run_production_client(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    // 1. 服务发现 — 配置多后端端点
    gov::static_discovery discovery({
        gov::endpoint("svc-1", "http://10.0.0.1:50051", /*weight=*/3),
        gov::endpoint("svc-2", "http://10.0.0.2:50051", /*weight=*/1),
        gov::endpoint("svc-3", "http://10.0.0.3:50051", /*weight=*/2),
    });

    // 2. 加权轮询负载均衡
    gov::load_balancer balancer;

    // 3. 重试策略
    gov::retry_policy retry{
        .max_attempts = 3,
        .initial_backoff = std::chrono::milliseconds(100),
        .max_backoff = std::chrono::milliseconds(5000),
        .backoff_multiplier = 2.0,
        .jitter = 0.2,
        .retryable_status_codes = {grpc::status_code::unavailable},
    };

    // 4. 重试预算 — 防止重试风暴
    gov::retry_budget budget(gov::retry_budget_config{
        .max_tokens = 10,
        .token_ratio = 1,
    });

    // 5. 熔断器 — 连续失败后熔断
    gov::circuit_breaker breaker(gov::circuit_breaker_config{
        .failure_threshold = 5,
        .success_threshold = 2,
        .open_duration = std::chrono::milliseconds(30'000),
        .half_open_max_requests = 1,
    });

    // 6. 创建治理客户端
    gov::governed_client client(ctx, discovery, balancer,
        retry, &budget, &breaker);

    // 7. 发起调用 — 自动负载均衡 + 重试 + 熔断
    auto resp = co_await client.unary({
        .service = "example.Echo",
        .method = "Say",
        .payload = std::vector<std::byte>{std::byte{'h'}, std::byte{'i'}},
        .timeout = std::chrono::milliseconds(5000),
    });

    if (resp && resp->st.ok())
        std::println("got {} bytes", resp->payload.size());
}
```

## 多核服务器部署

### server_context + http::server 模式

gRPC 运行在 HTTP/2 之上，通过 `http::server(server_context&)` 实现多核部署。

**API 签名**（来自 `http_server.cppm` + `grpc_server.cppm`）：

```cpp
// http::server 多核构造
explicit http::server(server_context& sctx);

// gRPC service_router
explicit service_router(server_options options);
void add_unary(std::string service, std::string method, unary_handler handler);
auto make_http_handler() const -> http::handler_fn;
```

**`server_policy` 服务端治理**（来自 `server_policy.cppm`）：

```cpp
class server_policy {
    explicit server_policy(server_policy_options options = {});
    auto begin(const call_context& context) -> std::expected<call_guard, status>;
    auto concurrency() const noexcept -> const concurrency_limiter&;
    auto rate_limits() noexcept -> rate_limit_registry&;
    auto statistics() noexcept -> call_statistics_registry&;
};

struct server_policy_options {
    std::size_t max_concurrent_calls = SIZE_MAX;
};
```

**生产级多核 gRPC 服务器**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

namespace cn = cnetmod;
namespace grpc = cnetmod::grpc;
namespace gov = cnetmod::grpc::governance;

auto echo_handler(std::span<const std::byte> payload, const grpc::call_context&)
    -> cn::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    co_return grpc::byte_buffer(payload.begin(), payload.end());
}

int main() {
    cn::net_init net;

    // 4 worker 线程
    cn::server_context sctx(4, 4);

    // 服务端治理 — 并发限制 + 速率限制 + 可观测性
    auto policy = std::make_shared<gov::server_policy>(gov::server_policy_options{
        .max_concurrent_calls = 1000,
    });

    // 按方法设置速率限制
    policy->rate_limits().set_limit(
        "example.Echo", "Say",
        gov::rate_limit{.tokens_per_second = 500.0, .burst = 100.0});

    // gRPC 路由器
    grpc::service_router grpc_router(grpc::server_options{
        .max_receive_message_bytes = 4 * 1024 * 1024,
        .max_send_message_bytes = 4 * 1024 * 1024,
        .accept_gzip = true,
        .governance = policy,
    });
    grpc_router.add_unary("example.Echo", "Say", echo_handler);

    // HTTP 路由器挂载 gRPC
    cn::http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    // 多核 HTTP 服务器
    cn::http::server srv(sctx);
    auto lr = srv.listen("0.0.0.0", 50051);
    if (!lr) {
        std::println("listen failed: {}", lr.error().message());
        return 1;
    }
    srv.set_router(std::move(router));

    cn::spawn(sctx.accept_io(), srv.run());

    // 定期输出可观测性统计
    cn::spawn(sctx.accept_io(), [&policy](cn::io_context& io) -> cn::task<void> {
        while (true) {
            co_await cn::async_sleep(io, std::chrono::seconds(30));
            auto snap = policy->statistics().snapshot("example.Echo", "Say");
            if (snap) {
                std::println("Echo/Say: started={} completed={} failed={} inflight={}",
                    snap->started, snap->completed, snap->failed, snap->in_flight);
            }
        }
    }(sctx.accept_io()));

    sctx.run();
}
```

## Do's & Don'ts

| Do | Don't |
|---|---|
| 服务端 handler 检查 `call_context::deadline_exceeded()` | 不要在 handler 中执行阻塞操作 |
| 使用 `request_interceptors` 注入认证头 | 不要硬编码 token 到业务逻辑 |
| 生产环境启用 `accept_gzip` 减少带宽 | 不要设置过大的 `max_message_bytes` |
| 使用 `governed_client` 集成治理 | 不要手动实现重试/熔断逻辑 |
| 注册健康检查和反射便于调试 | 不要忽略 `status_code::unavailable` 重试 |
| 多核部署使用 `http::server(server_context&)` | 不要在单线程 server 上跑 CPU 密集型处理 |
| 配置 `server_policy` 限制并发和速率 | 不要无限制接受请求导致过载 |

## 参考示例

- `examples/grpc/security_interceptor.cpp` — mTLS + Bearer Token 拦截器
<!-- END SOURCE: skill/protocols/grpc.md -->

<!-- BEGIN SOURCE: skill/protocols/kafka.md -->
# Source: `skill/protocols/kafka.md`

# Kafka 协议模块

> 完整的 Apache Kafka 协议客户端，支持生产者、消费者、消费组、事务与 SASL 认证。

**import**: `import cnetmod.protocol.kafka;`
**CMake**: `-DCNETMOD_ENABLE_KAFKA=ON`
**源码**: `src/protocol/kafka/`

## 场景导航

| 场景 | 关键类型 |
|------|---------|
| 异步生产消息 | `producer`, `producer_options`, `record` |
| 消费组消费 | `consumer`, `consumer_options`, `consumed_record` |
| 连接 Broker | `client_facade`, `client_options`, `broker_connection` |
| 分区策略 / SASL | `partitioner`, `sasl_authenticator` |
| 偏移量 / 消费组 | `offset_manager`, `group_coordinator` |
| 协议编解码 | `encoder`, `decoder`, `broker_request_codec`, `record_batch` |

## API 参考

### 协议常量与基础类型

**签名**:
```cpp
namespace cnetmod::kafka {
using bytes = std::vector<std::byte>;
enum class error_code : std::int16_t { none = 0, unknown_server_error = -1, ... };
struct error { error_code code; std::string message; bool retriable; };
template <typename T> using result = std::expected<T, error>;
struct topic_partition { std::string topic; std::int32_t partition; };
struct record { std::optional<bytes> key, value; std::vector<header> headers; };
struct consumed_record {
    topic_partition source; std::int64_t offset, timestamp;
    std::optional<bytes> key, value; std::vector<header> headers;
};
enum class compression : std::int8_t { none, gzip, snappy, lz4, zstd };
enum class acknowledgement : std::int16_t { none = 0, leader = 1, all = -1 };
enum class sasl_mechanism { none, plain, scram_sha_256, scram_sha_512 };
}
```

### `client_options` — 连接配置

**签名**:
```cpp
struct client_options {
    std::vector<client_endpoint> bootstrap_servers;
    std::string client_id = "cnetmod";
    authentication_credentials credentials;
    sasl_mechanism sasl = sasl_mechanism::none;
    std::chrono::milliseconds request_timeout{30000};
    std::size_t retries = 5;
    std::shared_ptr<scram_crypto_provider> scram_crypto;
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.kafka;

kafka::client_options opts;
opts.bootstrap_servers.push_back({.host = "kafka-broker", .port = 9092});
opts.client_id = "my-service";
opts.credentials = {.username = "user", .password = "pass"};
opts.sasl = kafka::sasl_mechanism::plain;
```

### `request_header` / `response_header` — 请求响应头

**签名**:
```cpp
namespace cnetmod::kafka::protocol {
enum class api_key : std::int16_t {
    produce = 0, fetch = 1, metadata = 3, join_group = 11,
    sasl_handshake = 17, api_versions = 18, sasl_authenticate = 36, ...
};
struct request_header {
    api_key key; std::int16_t version;
    std::int32_t correlation_id; std::string client_id;
};
struct response_header { std::int32_t correlation_id; };
}
```

### `protocol_value_codec` — 协议值编解码

**签名**:
```cpp
namespace cnetmod::kafka::protocol {
class encoder {
    void int8/int16/int32/int64(...); void string(std::string_view);
    void varint(std::int32_t); void varlong(std::int64_t);
    auto take() && -> bytes;
};
class decoder {
    explicit decoder(std::span<const std::byte> input) noexcept;
    auto int8/int16/int32/int64() -> result<...>;
    auto string() -> result<std::string>;
    auto remaining() const noexcept -> std::size_t;
};
auto encode_request(request_header, std::span<const std::byte>) -> bytes;
auto decode_response_header(decoder&) -> result<response_header>;
auto crc32c(std::span<const std::byte>) noexcept -> std::uint32_t;
}
```

### `record_batch` — 消息批次编解码

**签名**:
```cpp
class compression_codec {
    virtual auto compress(std::span<const std::byte>) -> result<bytes> = 0;
    virtual auto decompress(std::span<const std::byte>, std::size_t) -> result<bytes> = 0;
};
class compression_registry {
    void install(std::shared_ptr<compression_codec>);
    auto find(compression) const -> std::shared_ptr<compression_codec>;
};
auto encode_record_batch(std::span<const record>, const record_batch_options&,
    const compression_registry&) -> result<bytes>;
auto decode_record_batch(std::span<const std::byte>, const topic_partition&,
    const compression_registry&) -> result<decoded_record_batch>;
```

### `broker_request_codec` — Broker 请求编解码

**签名**:
```cpp
auto encode_api_versions() -> bytes;
auto decode_api_versions(std::span<const std::byte>, std::int16_t) -> result<api_versions_response>;
auto encode_produce(const produce_request&, std::int16_t) -> bytes;
auto decode_produce(std::span<const std::byte>, std::int16_t, const produce_request&)
    -> result<std::vector<produce_result>>;
auto encode_fetch(const fetch_request&, std::int16_t) -> bytes;
auto decode_fetch(std::span<const std::byte>, std::int16_t) -> result<fetch_response>;
auto encode_join_group(const join_group_request&, std::int16_t) -> bytes;
auto encode_heartbeat(const group_identity&, std::int16_t) -> bytes;
auto encode_offset_commit(const group_identity&,
    const std::map<topic_partition, std::int64_t>&, std::int16_t) -> bytes;
```

### `broker_connection` — Broker 传输连接

带取消令牌的连接和请求路径将令牌传入 Happy Eyeballs、握手及响应前缀/正文读写。
Application 回归通过本地不响应 TCP 对端验证请求等待的超时和主动取消；这不代表真实
TLS、认证、请求锁等待和所有重连竞争已完成验证。

连接事件按观察者分别隔离异常，单个观察者抛出异常不会跳过后续观察者或中断传输清理。
单线程回调中新增观察者不会使当前遍历失效：本轮通知范围在开始时固定，新增登记从后续
事件开始接收通知。当前观察者在回调期间由强引用保活，不复制整份登记表。
嵌套事件派发仅在最外层通知结束后清理失效登记，避免使外层索引越界；本地 TCP 回归
覆盖连接回调中嵌套连接通知。该保障不代表支持回调中销毁连接对象或跨线程共享连接。

**签名**:
```cpp
class broker_connection {
    broker_connection(io_context&, broker_endpoint, client_options);
    auto connect() -> task<result<void>>;
    auto request(protocol::api_key, std::int16_t, std::span<const std::byte>) -> task<result<bytes>>;
    void close() noexcept;
    auto is_open() const noexcept -> bool;
    void add_observer(std::weak_ptr<connection_observer>);
};
```

### `sasl_authenticator` — SASL 认证

**签名**:
```cpp
class sasl_authenticator {
    virtual auto mechanism_name() const noexcept -> std::string_view = 0;
    virtual auto initial_response() -> result<bytes> = 0;
    virtual auto challenge(std::span<const std::byte>) -> result<bytes> = 0;
    virtual auto complete() const noexcept -> bool = 0;
};
auto make_plain_authenticator(std::string, std::string) -> std::unique_ptr<sasl_authenticator>;
auto make_scram_authenticator(sasl_mechanism, std::string, std::string,
    std::shared_ptr<scram_crypto_provider>) -> result<std::unique_ptr<sasl_authenticator>>;
```

### `metadata_cache` — 元数据缓存

元数据更新与观察者通知分离：无有效观察者时不复制通知快照；通知准备分配失败不会撤销
已提交的缓存更新，当前通知会跳过，后续更新可继续通知。单个观察者异常不会传播给调用方
或跳过其他观察者；回调在缓存锁外执行。观察者不应作为必须成功的业务处理入口。

**签名**:
```cpp
class metadata_cache {
    void update(protocol::metadata_response);
    auto leader(const topic_partition&) const -> result<broker_endpoint>;
    auto partitions(std::string_view) const -> std::vector<std::int32_t>;
    auto broker(std::int32_t) const -> std::optional<broker_endpoint>;
    void add_observer(std::weak_ptr<metadata_observer>);
};
```

### `partitioner` — 分区策略

**签名**:
```cpp
class partitioner {
    virtual auto select(std::string_view, std::span<const std::byte>,
        std::span<const std::int32_t>) -> result<std::int32_t> = 0;
};
class murmur2_partitioner final : public partitioner { /* ... */ };
class uniform_sticky_partitioner final : public partitioner { /* ... */ };
```

### `producer` — 异步生产者

**签名**:
```cpp
struct producer_options {
    acknowledgement acks = acknowledgement::all;
    compression compression_type = compression::none;
    std::size_t batch_bytes = 1024 * 1024;
    std::chrono::milliseconds linger{5};
    bool idempotent = true;
    std::optional<std::string> transactional_id;
};
class producer {
    auto send(std::string topic, record) -> task<result<record_metadata>>;
    auto send(std::string topic, record, cancel_token&) -> task<result<record_metadata>>;
    auto flush() -> task<result<void>>;
    auto begin_transaction(cancel_token* = nullptr) -> task<result<void>>;
    auto commit_transaction(cancel_token* = nullptr) -> task<result<void>>;
    auto abort_transaction(cancel_token* = nullptr) -> task<result<void>>;
    void close() noexcept;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.kafka;

auto produce(client_facade& client) -> task<void> {
    auto producer = *client.make_producer({
        .acks = kafka::acknowledgement::all,
        .compression_type = kafka::compression::gzip,
        .idempotent = true
    });
    kafka::record rec;
    rec.key = bytes_from("order-123");
    rec.value = bytes_from(R"({"orderId":123})");
    auto result = co_await producer.send("orders", std::move(rec));
    co_await producer.flush();
    producer.close();
}
```

### `offset_manager` — 偏移量管理

**签名**:
```cpp
struct offset_and_metadata { std::int64_t offset; std::string metadata; };
class offset_manager {
    explicit offset_manager(std::shared_ptr<offset_backend>);
    void stage(topic_partition, offset_and_metadata);
    auto commit(std::string_view, std::int32_t, std::string_view,
        cancel_token* = nullptr) -> task<result<void>>;
    auto fetch(std::string_view, std::span<const topic_partition>, cancel_token* = nullptr)
        -> task<result<std::map<topic_partition, offset_and_metadata>>>;
};
```

### `group_coordinator` — 消费组协调

**签名**:
```cpp
class rebalance_listener {
    virtual auto on_partitions_revoked(std::span<const topic_partition>) -> task<void> = 0;
    virtual auto on_partitions_assigned(std::span<const topic_partition>) -> task<void> = 0;
};
class range_assignment final : public assignment_strategy { /* ... */ };
class cooperative_sticky_assignment final : public assignment_strategy { /* ... */ };
class group_coordinator {
    group_coordinator(std::string, std::shared_ptr<group_backend>,
        std::unique_ptr<assignment_strategy>, std::optional<std::string> = {});
    auto join(std::span<const std::string>, cancel_token*) -> task<result<group_state>>;
    auto heartbeat(cancel_token*) -> task<result<void>>;
    auto leave(cancel_token*) -> task<result<void>>;
    void set_listener(std::weak_ptr<rebalance_listener>);
};
```

### `consumer` — 消费组消费者

消费者必须在所属 executor 上串行访问。`close()` 任务开始执行后，
`subscribe/assign/poll/commit/seek` 返回 `configuration` 错误，不再调用后端；
关闭前创建但尚未执行的任务也遵守此规则。清理失败可再次调用 `close()`，
但不会重新开放业务操作。多个 `close()` 任务串行清理，首次成功后不再重复调用后端。
调用方仍须等待已经运行的操作结束；等待清理的 `close()` 任务必须保留并等待完成，
不能直接销毁挂起的任务。

**签名**:
```cpp
struct consumer_options {
    std::string group_id;
    std::size_t max_poll_records = 500;
    consumer_assignment_policy assignment_policy;
    offset_reset_policy auto_offset_reset = offset_reset_policy::earliest;
    bool enable_auto_commit = true;
};
class consumer {
    auto subscribe(std::vector<std::string>, cancel_token*) -> task<result<void>>;
    auto poll(cancel_token*) -> task<result<std::vector<consumed_record>>>;
    auto commit(const consumed_record&, cancel_token*) -> task<result<void>>;
    auto seek(topic_partition, std::int64_t, cancel_token*) -> task<result<void>>;
    auto close(cancel_token*) -> task<result<void>>;
};
```

**示例**:
```cpp
auto consume(client_facade& client) -> task<void> {
    auto consumer = *client.make_consumer({
        .group_id = "order-processors",
        .enable_auto_commit = false,
        .assignment_policy = kafka::consumer_assignment_policy::cooperative_sticky
    });
    co_await consumer.subscribe({"orders"});
    while (true) {
        auto batch = co_await consumer.poll();
        for (const auto& rec : *batch)
            co_await consumer.commit(rec); // 业务处理后提交
    }
}
```

### `client_facade` — 客户端门面

`close()` 在关闭传输前封闭当前运行时，拒绝该运行时上的工厂创建和元数据重连。
随后显式调用 `connect()` 会创建新的运行时；旧句柄不会自动绑定到新运行时。
同步 `close()` 不会等待消费者维护任务或在途 I/O，不能替代消费者的异步清理。
生命周期操作必须在所属 executor 上执行。

`co_await client.async_close(token)` 禁止新建消费者/生产者，取消并等待已登记的
消费者维护任务，再清理消费组、fetch session 与连接；失败时保留登记以便重试。
仅维护任务失败但资源已全部清理时，首次关闭仍返回任务错误，同时释放登记和传输；
后续关闭幂等成功。资源清理本身失败时才保留待清理登记。
登记使用弱消费者引用和独立维护任务所有权，因此提前释放消费者也不会跳过任务等待。
`requires_async_close()` 表示是否还有消费者登记需要清理；Application Kafka 服务据此
选择空登记的同步关闭或带 deadline 的异步关闭。调用方仍须自行取消并等待已经运行的
业务操作。创建消费者或查询 `requires_async_close()` 时会回收已成功完成且消费者
已清理/释放的登记；仍在运行或失败的登记不会被丢弃，关闭迭代期间不回收。
维护失败的自动恢复及取消等待中的锁尚需进一步验证。

`background_error()` 无分配地读取首个已结束的消费者维护任务错误，不清除错误。
Application 的 Kafka probe 在元数据请求前后检查此结果，失败时报告 `down`，
而不是用元数据成功覆盖后台任务失败。`restart_failed_maintenance()` 只替换仍开放的
消费者上已失败的维护任务；先登记替代任务的所有权，再派发，分配异常保留原失败登记。
Application 对已启动服务的恢复调用会使用此入口，重试节奏与预算仍由生命周期监管器负责。
本地协议对端已验证维护任务分配失败、健康 down、恢复分配失败保留原错误，
以及生命周期监管器触发恢复后，连续两次成功探测才恢复 readiness，再完成停机。
真实 Broker 中断与监管器多次退避/预算耗尽的联合验证仍未完成。
维护循环内部尚在重试的协议错误不属于
“任务已结束”错误。

**签名**:
```cpp
class client_facade {
public:
    client_facade(io_context&, client_options);
    auto connect(cancel_token* = nullptr) -> task<result<void>>;
    auto refresh_metadata(std::vector<std::string> = {}, cancel_token* = nullptr)
        -> task<result<void>>;
    auto metadata() const -> std::shared_ptr<metadata_cache>;
    auto make_producer(producer_options = {}, std::unique_ptr<partitioner> = {})
        -> result<producer>;
    auto make_consumer(consumer_options) -> result<consumer>;
    void close() noexcept;
};
```

**示例**:
```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.kafka;

auto main() -> int {
    namespace cn = cnetmod;
    cn::net_init network;
    auto context = cn::make_io_context();
    kafka::client_options opts;
    opts.bootstrap_servers.push_back({.host = "127.0.0.1", .port = 9092});
    kafka::client_facade client(*context, std::move(opts));
    cn::spawn(*context, [&]() -> cn::task<void> {
        co_await client.connect();
        auto producer = *client.make_producer();
        auto consumer = *client.make_consumer({.group_id = "demo"});
        client.close();
        context->stop();
    }());
    context->run();
}
```

## Do's & Don'ts

| Do | Don't |
|----|-------|
| 使用 `client_facade` 作为入口创建生产者和消费者 | 直接手动构建 `broker_connection` 发送协议帧 |
| 消费时手动 `commit` 以确保 at-least-once 语义 | 在高吞吐场景对每条消息都同步 commit |
| 配置 `idempotent = true` 实现精确一次投递 | 假设 `send` 立即发送——内部有 linger 批处理 |
| 使用 `cancel_token` 控制长操作生命周期 | 在 `close()` 后继续使用 producer/consumer |
| 提供 `scram_crypto_provider` 实现 SCRAM 认证 | 直接实例化 `sasl_authenticator`——使用工厂函数 |

## 连接池与多核部署

> **注意**：Kafka 模块为纯客户端实现，不提供 `server_context` 多核模式。生产级部署建议如下。

### Producer 批量发送（内置）

`producer` 内置 linger + batch 机制，无需外部连接池。`producer_options` 中的 `batch_bytes` 和 `linger` 控制批量行为：

```cpp
kafka::producer_options opts;
opts.acks = kafka::acknowledgement::all;
opts.compression_type = kafka::compression::gzip;
opts.batch_bytes = 2 * 1024 * 1024;  // 2MB 批次
opts.linger = std::chrono::milliseconds(10);  // 等待 10ms 凑批
opts.idempotent = true;
opts.max_in_flight = 5;  // 最大并行请求数
```

### 多 Worker 消费者部署

每个 `client_facade` 实例绑定一个 `io_context`，可在多个线程上各自创建独立的消费者实例：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.kafka;

namespace cn = cnetmod;
namespace kafka = cnetmod::kafka;

auto consumer_worker(cn::io_context& ctx, const std::string& worker_id) -> cn::task<void> {
    kafka::client_options opts;
    opts.bootstrap_servers.push_back({.host = "kafka-broker", .port = 9092});
    opts.client_id = worker_id;

    kafka::client_facade client(ctx, std::move(opts));
    co_await client.connect();

    auto consumer = *client.make_consumer({
        .group_id = "order-processors",  // 同 group_id 自动分区消费
        .enable_auto_commit = false,
        .assignment_policy = kafka::consumer_assignment_policy::cooperative_sticky
    });

    co_await consumer.subscribe({"orders"});

    while (true) {
        auto batch = co_await consumer.poll();
        if (!batch) break;
        for (const auto& rec : *batch) {
            std::println("[{}] offset={} key={}", worker_id, rec.offset,
                rec.key ? std::string(rec.key->begin(), rec.key->end()) : "null");
            co_await consumer.commit(rec);
        }
    }

    co_await consumer.close();
    client.close();
}

auto main() -> int {
    cn::net_init net;

    constexpr unsigned NUM_WORKERS = 4;
    std::vector<std::unique_ptr<cn::io_context>> contexts;
    std::vector<std::jthread> threads;

    for (unsigned i = 0; i < NUM_WORKERS; ++i) {
        auto& ctx = contexts.emplace_back(cn::make_io_context());
        auto worker_id = std::format("worker-{}", i);
        cn::spawn(*ctx, consumer_worker(*ctx, worker_id));
        threads.emplace_back([&ctx] { ctx->run(); });
    }

    for (auto& t : threads) t.join();
    return 0;
}
```

### Do's & Don'ts（多实例部署）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 每个 worker 独立 `client_facade` + 独立 `io_context` | 多线程共享同一个 `client_facade` |
| 同 `group_id` 多 worker 自动分区消费 | 在不同 group 中重复消费同一 topic |
| 利用 `linger` + `batch_bytes` 自动批量发送 | 每条消息都立即 flush |

---

## 参考示例

- `examples/kafka/kafka_demo.cpp` — 完整的生产者+消费者应用入口
- `examples/kafka/producer_service.hpp` — 并发生产者，支持幂等和压缩
- `examples/kafka/consumer_service.hpp` — 消费组消费者，手动提交偏移量
- `examples/kafka/kafka_application.hpp` — 应用生命周期编排
- `examples/kafka/kafka_config.hpp` — 环境变量配置读取
<!-- END SOURCE: skill/protocols/kafka.md -->

<!-- BEGIN SOURCE: skill/protocols/modbus.md -->
# Source: `skill/protocols/modbus.md`

# Modbus

> 工业 Modbus TCP/UDP/RTU 协议全栈实现，支持客户端、服务端、连接池与数据转换。

**import**: `import cnetmod.protocol.modbus;`
**CMake**: `-DCNETMOD_ENABLE_MODBUS=ON`
**源码**: `src/protocol/modbus/`

## 场景导航

- 我要读写保持寄存器 / 线圈 → [看这里](#场景寄存器读写)
- 我要启动 Modbus TCP 服务端 → [看这里](#场景tcp-服务端)
- 我要通过串口 RTU 通信 → [看这里](#场景rtu-串口通信)
- 我要使用连接池管理多连接 → [看这里](#场景连接池)
- 我要在寄存器中存取浮点数/32 位整数 → [看这里](#场景数据转换)
- Modbus TCP vs UDP vs RTU 差异 → [看这里](#场景协议对比)

## API 参考

### Modbus 类型

**签名**: `export enum class function_code : std::uint8_t`

| 值 | 说明 |
|---|------|
| `read_coils` | 0x01 读线圈 |
| `read_discrete_inputs` | 0x02 读离散输入 |
| `read_holding_registers` | 0x03 读保持寄存器 |
| `read_input_registers` | 0x04 读输入寄存器 |
| `write_single_coil` | 0x05 写单个线圈 |
| `write_single_register` | 0x06 写单个寄存器 |
| `write_multiple_coils` | 0x0F 写多个线圈 |
| `write_multiple_registers` | 0x10 写多个寄存器 |

**签名**: `export enum class transport_type { tcp, udp, rtu, ascii };`

**签名**: `export enum class exception_code : std::uint8_t` — `illegal_function`, `illegal_data_address`, `illegal_data_value` 等。

辅助函数：
```cpp
auto function_code_name(function_code fc) -> std::string_view;
auto exception_code_name(exception_code ec) -> std::string_view;
auto calculate_crc16(std::span<const std::uint8_t> data) -> std::uint16_t;
```

### `request_builder` — 请求构建器

**签名**: `export class request_builder`

| 方法 | 签名 | 说明 |
|------|------|------|
| `set_transport` | `auto set_transport(transport_type) -> request_builder&` | 设置传输类型 |
| `set_unit_id` | `auto set_unit_id(std::uint8_t) -> request_builder&` | 设置从站地址 |
| `read_coils` | `auto read_coils(uint16_t start, uint16_t qty) -> modbus_request` | 读线圈 |
| `read_holding_registers` | `auto read_holding_registers(uint16_t start, uint16_t qty) -> modbus_request` | 读保持寄存器 |
| `read_input_registers` | `auto read_input_registers(uint16_t start, uint16_t qty) -> modbus_request` | 读输入寄存器 |
| `write_single_register` | `auto write_single_register(uint16_t addr, uint16_t val) -> modbus_request` | 写单个寄存器 |
| `write_multiple_registers` | `auto write_multiple_registers(uint16_t start, std::span<const uint16_t>) -> modbus_request` | 批量写寄存器 |
| `write_multiple_coils` | `auto write_multiple_coils(uint16_t start, std::span<const bool>) -> modbus_request` | 批量写线圈 |

**示例**:
```cpp
import std;
import cnetmod.protocol.modbus;

using namespace cnetmod::modbus;

request_builder builder;
builder.set_unit_id(1).set_transport(transport_type::tcp);

auto req = builder.read_holding_registers(0, 10);
auto resp = co_await client.execute(req);
if (resp) {
    response_parser parser(*resp);
    if (!parser.is_exception()) {
        auto regs = parser.parse_registers();
        for (std::size_t i = 0; i < regs->size(); ++i) {
            std::println("Register[{}] = {}", i, (*regs)[i]);
        }
    }
}
```

### `response_parser` — 响应解析器

**签名**: `export class response_parser`

| 方法 | 签名 | 说明 |
|------|------|------|
| `is_exception` | `auto is_exception() const -> bool` | 是否异常响应 |
| `get_exception` | `auto get_exception() const -> exception_code` | 获取异常码 |
| `parse_bits` | `auto parse_bits() const -> std::expected<std::vector<bool>, std::error_code>` | 解析线圈/离散位 |
| `parse_registers` | `auto parse_registers() const -> std::expected<std::vector<std::uint16_t>, std::error_code>` | 解析寄存器值 |

### `tcp_client` — Modbus TCP 客户端

**签名**: `export class tcp_client`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit tcp_client(io_context&)` | |
| `connect` | `auto connect(string_view host, uint16_t port) -> task<std::error_code>` | 连接服务端 |
| `execute` | `auto execute(const modbus_request&) -> task<std::expected<modbus_response, std::error_code>>` | 执行请求 |
| `execute_with_timeout` | `auto execute_with_timeout(const modbus_request&, duration) -> task<...>` | 带超时执行 |
| `reconnect` | `auto reconnect() -> task<std::error_code>` | 重连 |
| `close` | `void close()` | 关闭连接 |

### `udp_client` — Modbus UDP 客户端

**签名**: `export class udp_client`

与 `tcp_client` 类似，额外提供 `execute_with_retry(req, int retries)` 用于无连接重试。

### `rtu_client` — Modbus RTU 串口客户端

**签名**: `export class rtu_client`

```cpp
export struct rtu_config {
    std::string port_name;
    std::uint32_t baudrate = 9600;
    std::uint8_t data_bits = 8;
    stop_bits stop = stop_bits::one;
    parity par = parity::none;
    std::chrono::microseconds char_timeout = std::chrono::microseconds(1500);
    std::chrono::microseconds frame_delay = std::chrono::microseconds(3500);
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `open` | `auto open(const rtu_config&) -> task<std::error_code>` | 打开串口 |
| `execute` | `auto execute(const modbus_request&) -> task<std::expected<...>>` | 执行请求 |
| `execute_with_retry` | `auto execute_with_retry(const modbus_request&, int = 3) -> task<...>` | 带重试 |

### 服务端

**签名**: `export class tcp_server` / `export class udp_server`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `tcp_server(io_context& ctx, data_store& store)` | 绑定数据存储 |
| `listen` | `auto listen(string_view host, uint16_t port, ...) -> task<std::error_code>` | 监听端口 |
| `async_run` | `auto async_run() -> task<void>` | 启动接受连接 |
| `stop` | `void stop()` | 停止服务 |

RTU 服务端使用 `rtu_server(io_context&, data_store&)` + `start(const rtu_server_config&)`.

### `data_store` — 数据存储

**签名**: `export class data_store` (抽象接口)

```cpp
virtual auto read_holding_register(uint16_t) -> std::expected<uint16_t, exception_code> = 0;
virtual auto write_holding_register(uint16_t, uint16_t) -> std::expected<void, exception_code> = 0;
virtual auto read_coil(uint16_t) -> std::expected<bool, exception_code> = 0;
virtual auto write_coil(uint16_t, bool) -> std::expected<void, exception_code> = 0;
```

内置实现: `memory_data_store`（内存）和 `channel_data_store`（协程安全通道）。

### `connection_pool` — 连接池

**签名**: `export class connection_pool`

```cpp
export struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 502;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| `async_run` | `auto async_run() -> task<void>` | 启动连接池 |
| `async_get_connection` | `auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>` | 异步获取连接 |
| `cancel` | `auto cancel() -> task<void>` | 关闭连接池 |

`pooled_connection` 支持 RAII 自动归还，使用 `conn->execute(req)` 操作。

## Do's & Don'ts

- **Do**: 用 `request_builder` 构建请求，不要手动拼装字节
- **Do**: 始终用 `response_parser` 检查 `is_exception()` 再解析数据
- **Do**: RTU 通信时 `set_transport(transport_type::rtu)`，自动附加 CRC16
- **Don't**: 不要在高并发场景为每个请求创建新 `tcp_client`，使用 `connection_pool`
- **Don't**: UDP 无连接保证可靠，务必使用 `execute_with_retry` 或应用层重试

## 场景：协议对比

| 特性 | TCP | UDP | RTU |
|------|-----|-----|-----|
| 连接 | 长连接 | 无连接 | 串口点对点 |
| 帧头 | MBAP Header | MBAP Header | 地址 + CRC16 |
| 可靠性 | TCP 保证 | 需重试 | 需重试 |
| 典型场景 | 工业以太网 | 广播采集 | 串口设备 |

## 参考示例

- `examples/modbus/modbus_demo.cpp` — TCP 客户端/服务端完整流程
- `examples/modbus/modbus_udp_demo.cpp` — UDP 无连接通信
- `examples/modbus/modbus_rtu_demo.cpp` — RTU 串口通信
- `examples/modbus/modbus_converter_demo.cpp` — 数据转换工具（`RegisterConverter`, `BitOps`, `CRC16`, `Hex`）

## 连接池/连接管理（生产级用法）

### Pool API

`connection_pool` 是 Modbus TCP 客户端的内置连接池，自动管理多个 `tcp_client` 连接的生命周期、健康检查和按需分配。

```cpp
// pool_params — 连接池参数
export struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 502;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
    std::chrono::steady_clock::duration health_check_interval = std::chrono::minutes(5);
};

// connection_pool — 连接池核心
export class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};

// pooled_connection — RAII 连接守卫，析构时自动归还
export class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> tcp_client&;
    auto operator->() noexcept -> tcp_client*;
};
```

**示例 — 连接池管理多设备**:

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
namespace modbus = cn::modbus;

auto poll_devices(cn::io_context& ctx) -> cn::task<void> {
    // 连接池：初始 4 连接，最大 32 连接，定期健康检查
    modbus::pool_params params;
    params.host = "192.168.1.100";   // PLC 网关地址
    params.port = 502;
    params.initial_size = 4;
    params.max_size = 32;
    params.health_check_interval = std::chrono::minutes(2);
    params.retry_interval = std::chrono::seconds(10);

    modbus::connection_pool pool(ctx, params);
    cn::spawn(ctx, pool.async_run());

    // 并发轮询 10 个从站设备
    for (std::uint8_t unit = 1; unit <= 10; ++unit) {
        cn::spawn(ctx, [&pool, unit]() -> cn::task<void> {
            auto conn_r = co_await pool.async_get_connection();
            if (!conn_r) {
                std::println("获取连接失败: unit={}", unit);
                co_return;
            }

            modbus::request_builder builder;
            builder.set_unit_id(unit).set_transport(modbus::transport_type::tcp);
            auto req = builder.read_holding_registers(0, 20);

            auto resp = co_await conn_r->get().execute(req);
            if (resp) {
                modbus::response_parser parser(*resp);
                if (!parser.is_exception()) {
                    auto regs = parser.parse_registers();
                    std::println("Unit {}: {} registers read", unit, regs->size());
                }
            }
            // pooled_connection 析构自动归还连接
        });
    }

    // 监控池状态
    co_await cn::async_sleep(ctx, std::chrono::seconds(60));
    std::println("池状态: size={}, idle={}, waiters={}",
        pool.size(), pool.idle_count(), pool.waiter_count());

    co_await pool.cancel();
}
```

## 多核/集群部署

### 部署模式

Modbus `tcp_server` / `udp_server` 仅接受单个 `io_context&`，**没有内置 `server_context` 构造函数**。多核部署有两种方案：

1. **多实例方案** — 每个 worker 运行独立的 `tcp_server`（监听不同端口，前置负载均衡器）
2. **手动 round-robin 方案** — 在 `accept_io` 接受 TCP 连接后，分发到 worker 的 `io_context` 处理

### 方案 1：多实例 Modbus TCP 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
namespace modbus = cn::modbus;

auto main() -> int {
    cn::net_init net;
    cn::server_context sctx(4, 4);

    // 每个 worker 运行独立的 Modbus TCP 服务端 + 独立数据存储
    for (auto* io_ptr : sctx.worker_ios()) {
        cn::spawn(*io_ptr, [&sctx, io_ptr]() -> cn::task<void> {
            static std::atomic<std::uint16_t> port_counter{5020};
            auto port = port_counter.fetch_add(1);

            auto store = std::make_shared<modbus::memory_data_store>();
            auto server = std::make_shared<modbus::tcp_server>(*io_ptr, *store);

            auto ec = co_await server->listen("0.0.0.0", port);
            if (ec) {
                std::println("监听失败 port={}: {}", port, ec.message());
                co_return;
            }
            std::println("Modbus TCP 服务端启动 port={}", port);
            co_await server->async_run();
        });
    }

    sctx.run();
    return 0;
}
```

### 方案 2：Modbus 网关（多核 + 连接池）

生产级 Modbus TCP 网关示例：前端接受大量 SCADA 客户端连接，后端通过连接池访问 PLC 设备。

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.modbus;

namespace cn = cnetmod;
namespace modbus = cn::modbus;

constexpr unsigned WORKER_THREADS = 4;

// 每个 worker 持有独立的 Modbus 连接池
struct worker_gateway {
    cn::io_context& io;
    std::shared_ptr<modbus::memory_data_store> store;
    std::shared_ptr<modbus::tcp_server> server;
};

auto main() -> int {
    cn::net_init net;
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 为每个 worker 创建独立的 Modbus 服务端 + 数据存储
    std::vector<worker_gateway> gateways;
    for (auto* io_ptr : sctx.worker_ios()) {
        auto store = std::make_shared<modbus::memory_data_store>();
        auto server = std::make_shared<modbus::tcp_server>(*io_ptr, *store);
        gateways.push_back({*io_ptr, store, server});
    }

    // 在每个 worker 上启动 Modbus 服务端（不同端口，前置 LB）
    for (std::size_t i = 0; i < gateways.size(); ++i) {
        auto& gw = gateways[i];
        auto port = static_cast<std::uint16_t>(5020 + i);
        cn::spawn(gw.io, [&gw, port]() -> cn::task<void> {
            auto ec = co_await gw.server->listen("0.0.0.0", port);
            if (ec) co_return;
            std::println("Modbus 网关 worker 启动 port={}", port);
            co_await gw.server->async_run();
        });
    }

    // 同时在 accept_io 上运行连接池，用于网关主动轮询 PLC
    modbus::pool_params pool_params;
    pool_params.host = "192.168.1.100";
    pool_params.port = 502;
    pool_params.initial_size = 4;
    pool_params.max_size = 32;
    pool_params.health_check_interval = std::chrono::minutes(3);

    auto upstream_pool = std::make_shared<modbus::connection_pool>(
        sctx.accept_io(), pool_params);
    cn::spawn(sctx.accept_io(), upstream_pool->async_run());

    // 定期从 PLC 同步数据到各 worker 的 data_store
    cn::spawn(sctx.accept_io(), [upstream_pool, &gateways]() -> cn::task<void> {
        while (true) {
            co_await cn::async_sleep(sctx.accept_io(), std::chrono::seconds(5));

            auto conn_r = co_await upstream_pool->async_get_connection();
            if (!conn_r) continue;

            modbus::request_builder builder;
            builder.set_unit_id(1).set_transport(modbus::transport_type::tcp);
            auto req = builder.read_holding_registers(0, 100);

            auto resp = co_await conn_r->get().execute(req);
            if (!resp) continue;

            modbus::response_parser parser(*resp);
            if (parser.is_exception()) continue;
            auto regs = parser.parse_registers();
            if (!regs) continue;

            // 同步到所有 worker 的数据存储
            for (auto& gw : gateways) {
                auto& store_regs = gw.store->get_holding_registers();
                for (std::size_t i = 0; i < regs->size() && i < store_regs.size(); ++i) {
                    store_regs[i] = (*regs)[i];
                }
            }
        }
    });

    sctx.run();
    return 0;
}
```
<!-- END SOURCE: skill/protocols/modbus.md -->

<!-- BEGIN SOURCE: skill/protocols/mqtt.md -->
# Source: `skill/protocols/mqtt.md`

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
<!-- END SOURCE: skill/protocols/mqtt.md -->

<!-- BEGIN SOURCE: skill/protocols/openai-mail-dns.md -->
# Source: `skill/protocols/openai-mail-dns.md`

# OpenAI / Mail / DNS

> OpenAI API 异步客户端（Chat/Embedding/TTS/STT/DALL-E）、SMTP 邮件收发、异步 DNS 客户端与服务端。

**import**:
- `import cnetmod.protocol.openai;`
- `import cnetmod.protocol.mail;`
- `import cnetmod.protocol.dns;`

**CMake**:
- `-DCNETMOD_ENABLE_OPENAI=ON`
- `-DCNETMOD_ENABLE_MAIL=ON`
- `-DCNETMOD_ENABLE_DNS=ON`

**源码**:
- `src/protocol/openai/`
- `src/protocol/mail/`
- `src/protocol/dns/`

---

## Part 1: OpenAI

### 场景导航

- 我要调用 Chat Completions → [看这里](#场景chat-completions)
- 我要使用 Responses API → [看这里](#场景responses-api)
- 我要流式接收响应（SSE） → [看这里](#场景流式-chat-sse)
- 我要构建可组合链/结构化输出 → [看这里](#场景runnable-与结构化输出)
- 我要构建工具调用 Agent → [看这里](#场景工具调用-agent)
- 我要做对话记忆与 RAG → [看这里](#场景对话记忆与-rag)
- 我要增加重试、模型回退、取消和追踪 → [看这里](#场景韧性取消与运行追踪)
- 我要生成 Embedding 向量 → [看这里](#场景embeddings)
- 我要生成图片（DALL-E） → [看这里](#场景dall-e-图片生成)
- 我要语音合成/识别 → [看这里](#场景tts--stt)

### API 参考

#### `connect_options` — 连接配置

**签名**: `export struct connect_options`

```cpp
struct connect_options {
    std::string api_base = "https://api.openai.com/v1";
    std::string api_key;
    bool tls_verify = true;
    std::string tls_ca_file;
    int timeout_seconds = 120;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};
```

#### `message` — 对话协议消息

**签名**: `export struct message`

| 方法 | 签名 | 说明 |
|------|------|------|
| `user` | `static auto user(std::string_view text) -> message` | 构造终端用户输入（协议角色 `user`） |
| `system` | `static auto system(std::string_view text) -> message` | 构造系统级指令（协议角色 `system`） |
| `developer` | `static auto developer(std::string_view text) -> message` | 构造应用开发者指令（协议角色 `developer`） |
| `model_output` | `static auto model_output(std::string_view text) -> message` | 构造模型输出消息（序列化协议角色为 `assistant`） |
| `tool_call_request` | `static auto tool_call_request(std::vector<tool_call>) -> message` | 构造模型发起的工具调用请求，并携带调用标识与参数 |
| `tool_result` | `static auto tool_result(std::string_view id, std::string_view content) -> message` | 构造与工具调用标识关联的执行结果（协议角色 `tool`） |
| `user_multimodal` | `static auto user_multimodal(std::vector<content_part>) -> message` | 构造包含文本、图像等内容分片的用户输入 |

#### `chat_request` / `chat_response` — 请求与响应

**签名**: `export struct chat_request`

```cpp
struct chat_request {
    std::string model = "gpt-4o-mini";
    std::vector<message> messages;
    double temperature = 0.7;
    int max_tokens = 4096;
    bool stream = false;
    std::vector<tool> tools;
    std::string tool_choice; // "auto" | "none" | "required"
    std::string response_format; // "" | "json_object" | "json_schema"
    std::optional<int> seed;
};
```

**签名**: `export struct chat_response`

```cpp
struct chat_response {
    std::string id;
    std::string model;
    std::vector<choice> choices;
    usage token_usage;
    auto content() const -> std::string_view; // choices[0].msg.content
};
```

#### `client` — OpenAI 客户端

**签名**: `export class client`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit client(io_context&) noexcept` | |
| `connect` | `auto connect(connect_options) -> task<std::expected<void, std::string>>` | 连接 API |
| `chat` | `auto chat(chat_request) -> task<std::expected<chat_response, std::string>>` | Chat Completions |
| `responses` | `auto responses(response_request) -> task<std::expected<response_result, std::string>>` | Responses API |
| `chat_stream` | `auto chat_stream(chat_request, on_chunk_fn[, cancel_token&]) -> task<std::expected<std::string, std::string>>` | SSE 流式，可取消 |
| `chat_stream_async` | `auto chat_stream_async(chat_request, async_chunk_fn[, cancel_token&]) -> task<...>` | 异步回调流式，可取消 |
| `list_models` | `auto list_models() -> task<std::expected<std::vector<model_info>, std::string>>` | 列出模型 |
| `embeddings` | `auto embeddings(embedding_request) -> task<std::expected<embedding_response, std::string>>` | 向量嵌入 |
| `text_to_speech` | `auto text_to_speech(tts_request) -> task<std::expected<std::vector<std::byte>, std::string>>` | 语音合成 |
| `transcribe` | `auto transcribe(transcription_request) -> task<std::expected<transcription_response, std::string>>` | 语音转文字 |
| `translate` | `auto translate(translation_request) -> task<std::expected<transcription_response, std::string>>` | 语音翻译 |
| `create_image` | `auto create_image(image_generation_request) -> task<std::expected<image_response, std::string>>` | 生成图片 |
| `edit_image` | `auto edit_image(image_edit_request) -> task<std::expected<image_response, std::string>>` | 编辑图片 |
| `create_image_variation` | `auto create_image_variation(image_variation_request) -> task<...>` | 图片变体 |
| `moderate` | `auto moderate(moderation_request) -> task<std::expected<moderation_response, std::string>>` | 内容审核 |

#### `chat_model_template` — provider-neutral Application 大模型门面

Application 项目启用 OpenAI 自动装配后，优先使用
`application_runtime::chat_model(instance, options)`，不要在 route 中自行创建或连接
`openai::client`。Application 仅依赖 `cnetmod.ai` 的 provider-neutral 合约；
`openai_service` 作为 adapter 管理多个 client 和固定容量连接池，未来 Claude、Gemini
与本地模型实现相同的 `chat_model_service` 即可复用模板、会话和路由代码：

```cpp
auto model = runtime.chat_model("assistant",
    {.request = {.model = "gpt-4o-mini", .temperature = 0.2},
        .system_prompt = "Answer with verified facts."});
if (!model)
    co_return;

ai::run_config run{
    .metadata = {{"tenant", "acme"}},
    .cancellation = &cancellation,
    .trace_parent = parent,
};
auto response = co_await model->invoke("Summarize the incident", run);
```

| 方法 | 说明 |
|------|------|
| `invoke(chat_request, run_config)` | 执行完全显式的 Chat Completions 请求 |
| `invoke(string, run_config)` | 使用默认请求、system prompt 和当前 user 输入 |
| `stream(chat_request, handler, run_config)` | 显式请求的异步流式输出与背压 |
| `stream(string, handler, run_config)` | 使用模板默认值的异步流式输出 |
| `conversation(session_id, store, options)` | 创建显式、append-only 的会话门面 |

模板不拥有 managed service，也不隐藏取消或 trace context。每次调用从 provider 池独占
一个 model lease，流式调用直到终态才归还。OpenAI adapter 保留调用方 listeners，并只
追加一次 Application telemetry listener。同 session 的会话跨模板共享协程门，不同
session 不共享消息且可以并行。成功响应才原子追加 user/assistant 两条记录；store 始终是
唯一真相，不维护内存影子快照。

`application_runtime::reconfigure_chat_model()` 是 provider-neutral 热重载入口。
调用方提供完整的 `chat_model_reconfiguration::properties`；OpenAI adapter 接受
`base_url`、`api_key`、`tls_verify`、`timeout_seconds` 和 `pool_size`。Adapter 会先
建立并验证全部新连接，再通过连接池 generation swap 一次发布。配置非法或任一连接失败时
旧 generation 不变；成功发布后，在途请求继续持有旧客户端，新请求只获取新客户端。
`chat_model_pool::reset()` 是 provider 实现原语，不是 route 或领域代码的配置 API。

#### 多模态与 Function Calling 类型

```cpp
export struct content_part {
    std::string type;           // "text" | "image_url"
    std::string text;
    image_url_detail image_url;
    static auto make_text(std::string_view) -> content_part;
    static auto make_image_url(std::string_view url, std::string_view detail = "auto") -> content_part;
    static auto make_image_base64(std::string_view data, std::string_view media_type, std::string_view detail) -> content_part;
};

export struct tool_call { std::string id; std::string type; function_call function; };
export struct tool { std::string type; std::string function_name; std::string function_description; json function_parameters; };
```

#### 模型、链、Agent 与检索抽象

| 类型 | 设计职责 |
|------|----------|
| `chat_model` / `embedding_model` | Strategy：隔离供应商客户端，方便 fake、替换和组合 |
| `openai_chat_model` / `openai_embedding_model` | Adapter：将底层 `client` 接入模型抽象 |
| `chat_model_router` / `routed_chat_model` | Strategy：按每次调用上下文异步选择模型，并支持无匹配时回退 |
| `image_model` / `moderation_model` | Strategy：供应商无关的图像生成与内容审核模型契约 |
| `openai_image_model` / `openai_moderation_model` | Adapter：将底层图像和审核端点接入统一模型抽象 |
| `telemetry_listener` | Observer：输出 OpenMetrics 指标、成本估算和 W3C 关联 Span，可直接接入 OTLP/HTTP |
| `run_scope` | RAII：保证模型、工具、检索和 Agent 在成功、错误、取消与异常路径都闭合观测生命周期 |
| `resilient_chat_model` | Decorator：普通及流式调用的指数退避重试与有序模型回退；流已开始后禁止重放 |
| `governed_chat_model` | Decorator：对普通及流式调用应用 Bulkhead 并发隔离、Token Bucket 速率限制与 Circuit Breaker 熔断保护 |
| `runnable` | Composite/Pipeline：按顺序组合 prompt、model、parser 等异步步骤 |
| `prompt_template` / `chat_prompt_template` | 严格变量、默认值、条件段与列表 section；`{{`/`}}` 表示字面花括号 |
| `json_output_parser` | 解析 JSON 并校验 `type/required/properties/items/enum/additionalProperties` 子集 |
| `tool_registry` | Command Registry：注册异步工具，执行前校验 JSON Schema，并以结构化错误区分取消、未注册、参数无效和执行失败 |
| `tool_provider` / `functional_tool_provider` | Strategy：根据会话、调用参数和工具循环迭代动态提供工具 |
| `keyword_tool_search` / `semantic_tool_search` | Strategy：按关键词或向量相似度发现工具，避免完整工具目录占用上下文 |
| `tool_binding<Arguments, Result>` | Adapter：将强类型 C++ 异步命令绑定到 JSON Tool Calling 协议 |
| `agent_executor` | State：有界执行 model → tools → model 循环 |
| `conversation_memory` | Repository：协程安全、有界的会话消息存储 |
| `append_only_chat_memory_store` | Repository：面向消息表的原子追加与最近窗口读取，不重写历史快照 |
| `append_only_chat_record_store` | Repository：协议消息与应用元数据分离，追加后返回数据库补全的记录 |
| `chat_record_memory_adapter` | Adapter：将带 ID、模型、token、时间戳等元数据的记录仓库接入协议记忆 |
| `file_chat_memory_store` | Repository：按会话分文件、容量受限并以原子替换持久化完整消息 |
| `long_term_store` | Repository：跨会话 namespace/key JSON 记忆、TTL、过滤分页与可选语义检索 |
| `checkpoint_store` | Repository：版本化执行状态、pending writes、分支、回滚与乐观并发 |
| `checkpoint_agentic_scope_store` | Adapter：将通用 Checkpointer 接入 `agentic_runtime` |
| `retriever` / `embedding_store` | Repository/Strategy：供应商无关的过滤检索与向量存储契约 |
| `in_memory_vector_store` | Repository：异步嵌入、余弦检索、更新和删除 |
| `functional_retriever` | Adapter：将全文搜索、知识图谱、SQL、Web 搜索或应用检索函数接入统一检索契约 |
| `delegating_embedding_store` | Adapter：按能力组合外部向量仓库的写入、检索、删除、清空和计数处理器 |
| `metadata_filter` | Composite：嵌套字段条件、AND、OR 与 NOT 元数据表达式 |
| `retrieval_chain` | RAG Pipeline：retrieve → prompt context → model |
| `ai_service` | Facade：统一编排模型、会话记忆、工具、RAG 与输入/输出策略 |
| `structured_service<T>` | Typed Facade：JSON Schema 约束、本地校验并解码为 C++ 业务对象 |
| `guardrail_pipeline` | Chain of Responsibility：组合输入校验、内容审核、输出校验与重试 |
| `retrieval_augmentor` | Advanced RAG：查询转换、路由、多路检索、RRF、重排与上下文注入 |
| `citation_context_injector` | Strategy：以稳定来源标签、文档 ID、分数和白名单元数据注入可追溯上下文 |
| `model_query_router` | Strategy：使用严格结构化模型输出选择具名检索器，并提供 fail/none/all 回退策略 |
| `model_query_transformer` | Strategy：严格结构化地执行查询压缩、改写、多查询扩展和 HyDE |
| `functional_query_transformer` / `functional_query_router` | Adapter：接入应用自定义异步查询转换与路由策略 |
| `functional_content_aggregator` / `functional_content_reranker` / `functional_context_injector` | Adapter：替换聚合、重排和上下文注入阶段 |
| `scoring_model` / `scoring_reranker` | Strategy：供应商无关的相关性评分与确定性过滤、重排 |
| `chat_scoring_model` | Adapter：使用严格结构化聊天输出为候选文档逐项评分 |
| `ingestion_pipeline` | Pipeline：文档加载、可组合转换、分块与索引写入 |
| `metadata_enricher` / `document_filter` / `functional_document_transformer` | Strategy/Adapter：元数据增强、文档筛选与应用自定义异步转换 |
| `recursive_text_splitter` / `markdown_header_splitter` | Strategy：递归边界文本分块或保留标题层级元数据的 Markdown 分段 |
| `file_document_source` / `directory_document_source` / `url_document_source` | Source/Composite：异步加载指定文件、目录树或远程 URL |
| `document_parser` / `document_parser_registry` | Strategy/Registry：按扩展名或媒体类型选择可替换解析器，把来源读取与格式解析分离 |
| `functional_document_parser` | Adapter：接入异步 PDF、Office、Tika、Docling 等外部解析实现 |
| `markdown_document_parser` / `html_document_parser` | Strategy：内置 Markdown 元数据解析与 HTML 可见正文提取 |
| `evaluation_suite` | Composite：组合确定性匹配和嵌入语义相似度评测，输出逐项分数及汇总报告 |
| `agentic_runtime` / `workflow_planner` | Template Method + Strategy：并行 Agent、共享 Scope、人工审批与检查点恢复 |
| `file_agentic_scope_store` | Repository：按工作流隔离、容量受限并以原子替换持久化 Scope、Planner 和人工审批状态 |
| `parallel_planner` / `conditional_planner` / `loop_planner` | Strategy：开箱即用的并行、条件分支与有界循环编排 |
| `mcp_client` | Facade/Adapter：MCP 初始化、工具、资源、提示词与本地 Tool Registry 适配 |
| `mcp_tool_provider` | Adapter/Strategy：聚合多个 MCP 客户端，按客户端及工具定义过滤并动态暴露工具 |
| `skill_catalog` / `filesystem_skill_loader` | Repository/Provider：预加载 Agent Skills，并在激活后渐进披露资源与专属工具 |
| `mcp_stdio_transport` | Strategy：跨平台子进程 stdio JSON-RPC，并通过 executor bridge 避免阻塞 I/O 协程 |

协议契约按业务领域直接分区：

| 分区 | 职责 |
|------|------|
| `:foundation` | JSON 别名、usage、错误、连接与模型信息 |
| `:tool_contracts` | 函数工具声明与工具调用结果 |
| `:messages` | 文本、多模态与工具消息 |
| `:chat` | Chat Completions 请求、响应与流式 chunk |
| `:responses` | Responses API 请求与结果 |
| `:embeddings` | Embedding 请求与向量响应 |
| `:audio` | TTS、转录与翻译 |
| `:images` | 图片生成、编辑和变体 |
| `:moderation` | 内容审核 |
| `:guardrails` | 输入/输出策略链与审核适配器 |
| `:service` | 高层 AI Service 编排门面 |
| `:structured` | 强类型结构化输出契约与业务对象解码 |
| `:filters` | 可组合元数据过滤表达式 |
| `:rag` | Advanced RAG 扩展点与默认实现 |
| `:ingestion` | 文档摄取流水线 |
| `:loaders` | 基于异步文件 I/O 的具体文档来源 |
| `:evaluation` | 可组合的响应质量评测与汇总报告 |
| `:bindings` | 强类型工具参数解码、执行与结果编码适配 |
| `:tool_search` | 关键词与向量语义工具检索策略 |
| `:skills` | Agent Skills 目录加载、激活、资源读取与技能专属工具供应 |
| `:memory_store` | 基于文件系统的持久化 Chat Memory Store |
| `:long_term_store` | 跨会话、具名空间、可检索的长期 JSON Memory Store |
| `:checkpoint` | 版本化 Checkpointer、pending writes、分支和回滚契约 |
| `:agentic` | 多 Agent 工作流运行时与持久化 Scope |
| `:planners` | 可恢复的并行、条件与循环 Planner |
| `:mcp` | MCP client、Streamable HTTP/stdio transport 与 Tool Adapter |

所有高层调用均接收 `run_config`。可通过 `run_id`、`tags`、`metadata` 传播运行上下文，通过 `callback` 接收模型、重试、工具、检索和 Agent 生命周期事件，通过 `cancel_token` 协作式取消。上下文感知工具、动态工具提供器和检索器会收到同一次调用配置的非拥有视图，不得在对应异步操作完成后保留该指针或引用。

`run_config.listeners` 可同时安装多个 `run_listener`，以 Observer 方式接收嵌套调用事件；`callback` 作为轻量兼容入口继续保留。每个 `run_event` 带时间戳和结构化 `attributes`，便于映射 OpenTelemetry GenAI 语义字段或指标标签。监听器和兼容回调相互隔离：单个观察者抛出的异常会记录警告但不会中断后续观察者或业务调用；`functional_run_listener` 可将应用函数直接适配为观察者。

`conversation_memory` 同时支持消息数量窗口与 token 窗口。使用 `chat_memory_store` 可以按 session ID 持久化完整快照；使用 `append_only_chat_memory_store` 时，追加直接进入消息表，读取只请求最近窗口，裁剪不会回写数据库。需要保留数据库生成的消息 ID、模型、token 和时间戳时，实现 `append_only_chat_record_store`：`persisted_chat_message` 将协议 `message` 与任意 JSON metadata 分离，追加返回数据库补全后的记录；`load_page(session_id, offset, limit)` 按插入顺序分页，`count(session_id)` 单独返回总数，零 `limit` 对已存在会话返回空页；`chat_record_memory_adapter` 只向模型暴露协议消息，metadata 永远不会进入 OpenAI 请求。记录仓库统一返回 `std::error_code`，可用 `chat_record_store_errc` 区分会话不存在、参数错误、冲突、存储不可用、数据损坏、资源耗尽和原子写失败。record store 的分页、计数、最近读取和删除在会话不存在时返回 `session_not_found`；memory adapter 将读取映射为空历史、将删除映射为幂等成功。无外部会话目录的存储可在首次 append 时创建会话；受外键约束的实现应返回 `session_not_found`。空批次是无副作用成功，不创建会话。`append_batch` 是明确的原子存储边界，实现必须使用数据库原生事务或等效的原子操作，框架不会泄漏一套无法覆盖 SQL 与非 SQL 存储的伪事务对象。`trim_messages` 是公开的无持久化纯算法，下游也可以直接复用窗口与工具交换裁剪策略。`pinned_prefix_messages` 保护固定前缀并将其排除在消息数和 token 预算之外，`preserved_tail_messages` 默认保护最后一条消息。裁剪返回 `trim_result`，报告删除消息数、删除 token、剩余 token 及预算是否真正满足；`remaining_tokens` 只统计受预算约束的后缀，不包含 pinned 前缀，并直接与非零 `max_tokens` 比较。只剩受保护消息时不会为了硬凑预算删除本轮输入。`max_messages` 与 `max_tokens` 的零值均表示不限制。存储失败会沿协程调用链返回；淘汰模型工具调用时会同时清理关联的工具结果，避免产生孤立协议消息。

`prompt_template` 保留 `{name}` 缺失即报错的严格行为，并增加 `{name|default}` 默认值、`{?name}...{/name}` 条件段及 `{#items}...{/items}` 列表 section。列表的每一行使用 `prompt_section`，拥有局部变量和可递归的子 section；变量及子 section 都按“当前行优先、根上下文兜底”解析。富上下文通过 `prompt_context` 和 `format_context()` 显式传入，避免与旧的花括号 `prompt_variables` 调用产生重载歧义。`output_parser` 仍是按需组合的结构化输出边界，普通文本业务不需要为了使用 Prompt 或 Memory 强制接入 Parser。

`file_chat_memory_store` 提供开箱即用的持久化实现。每个 session 使用独立 JSON 文件，session ID 先稳定哈希为安全文件名并保存在文件信封中二次校验；写入使用同目录临时文件和原子替换。文件访问通过 executor bridge 执行，支持文本、多模态、模型工具调用和工具执行结果的完整往返恢复，并限制单会话文件大小。

`long_term_store` 面向跨会话记忆，不与聊天记录混用。`store_namespace` 提供层级隔离，`put` 保存任意 JSON 并返回单键递增版本，`expected_version` 以 compare-and-swap 防止覆盖并发更新；`search` 支持 namespace 前缀、`metadata_filter`、limit/offset 分页、TTL 刷新和可选 embedding 相似度。`in_memory_long_term_store` 是具备完整语义的参考实现；生产数据库通过相同接口在事务中实现版本、过期和索引。

`checkpoint_store` 保存不可变状态版本，并为每一版本维护幂等的 pending-write journal。`commit`、`put_pending_writes` 和 `rollback` 分别提供 head version 或 write revision 的乐观并发检查；`fork` 从指定历史版本创建隔离分支，`rollback` 通过追加新版本恢复旧状态，不删除审计历史。`checkpoint_agentic_scope_store` 将该契约适配到现有 `agentic_runtime`，因此 Agent 的暂停、恢复和每步保存自动得到版本历史与并发冲突保护。

`ai_service` 是推荐的应用层入口。它在一次调用内按顺序执行输入 Guardrail、会话读取、Advanced RAG、模型或工具 Agent、输出 Guardrail 以及会话提交；不合规输出可在限定次数内带修正指令重新生成。工具 Agent 返回完整 `transcript`，AI Service 会把模型工具请求、带调用标识的工具结果和最终模型输出作为一个连续交换提交到记忆，不会只保留最终文本而破坏下一轮协议上下文。

`retrieval_augmentor` 将 `query_transformer`、`query_router`、`content_aggregator`、`content_reranker` 与 `context_injector` 作为独立策略组合。默认提供静态路由、Reciprocal Rank Fusion、透传重排器、developer context 注入器及引用溯源注入器。`citation_context_injector` 为每个候选生成稳定的 `[source N]` 标签，可选择附带文档 ID、相关性分数和显式白名单中的元数据，避免把私有元数据无意发送给模型。

`functional_retriever` 可把全文搜索引擎、知识图谱、SQL、Web 搜索或应用函数直接接入 RAG。`delegating_embedding_store` 将外部向量数据库的能力拆成独立异步处理器，检索为必需能力，写入、按 ID 删除、按元数据删除、清空和计数可以按后端实际能力选择性提供；未实现的操作返回明确的“不支持”错误，而不是静默丢弃。这样 PostgreSQL、Redis、Milvus、Pinecone 等集成可复用完整的过滤、摄取、路由、重排和 AI Service 流水线。

`model_query_router` 为每个检索器绑定稳定名称、用途描述和实例指针，使用温度 0 及严格 JSON Schema 让模型选择相关检索器。返回结果会再次本地校验并映射到已注册实例；模型错误、非法 JSON 或未知名称按配置选择直接失败、不路由或路由到全部检索器，避免模型输出直接控制未注册资源。

`model_query_transformer` 使用温度 0 和严格 JSON Schema 实现四种检索前变换：将带上下文问题压缩成独立查询、精确改写、生成去重的多查询扩展，以及生成仅用于向量检索的假设文档（HyDE）。它限制最多 32 个结果，拒绝空白、超量和无效结构，保留原查询的过滤条件、阈值、数量限制及元数据，并可显式保留原始查询。所有 Advanced RAG 阶段都有对应的 `functional_*` Adapter，应用可以替换任一策略而不继承框架实现。

为 `retrieval_augmentor` 提供 `io_context` 时，多查询与多检索器形成的检索任务通过 `task_group` 并发执行，结果仍按确定性任务顺序交给聚合器；每个子任务拥有独立取消令牌，并通过 `retrieval_request::config` 向异步存储或搜索引擎传播。任一任务失败会取消同组任务并在全部子任务退出后返回，且优先保留原始检索错误而不是后续取消错误，避免孤立协程、静默后台工作和诊断信息丢失；不提供执行上下文时保留顺序执行模式。

`scoring_model` 将交叉编码器、专用 rerank API 或应用自定义算法统一为批量相关性评分契约，`functional_scoring_model` 可直接适配异步业务实现。`scoring_reranker` 校验评分数量和有限值，按阈值过滤后稳定降序排列；`chat_scoring_model` 可在没有专用 rerank 服务时使用聊天模型，限制每篇候选内容长度，通过严格 JSON Schema 和本地索引完整性检查保证每篇文档恰好得到一个 `[0, 1]` 分数。

`agentic_runtime` 使用 `workflow_planner` 决定下一组 Agent，同组 Agent 通过 `task_group` 并行执行。每个成功步骤都会把共享 `agentic_scope` 和 Planner 状态写入 `agentic_scope_store`；`human_input_request` 可携带请求标识、提示、写回键与 JSON Schema，`resume` 验证人工回复后从检查点继续，重复 `execute` 不会绕过待处理审批。除轻量的内存仓库外，`file_agentic_scope_store` 在 executor 上执行文件访问，以工作流 ID 的稳定哈希隔离文件，通过信封二次校验原始 ID，限制检查点容量，并用同目录临时文件原子替换目标文件；进程重启后仍可恢复 Planner、共享 Scope、已完成步骤和待处理人工审批。

`structured_service<T>` 接收显式 `structured_output_contract<T>`，向模型发送严格 JSON Schema，并在边界再次校验 JSON 后调用业务解码器。该设计不依赖反射宏，解码失败通过 `std::expected` 返回，同时保留 `ai_service_result` 中的原始消息、token 用量、检索文档和工具步骤。

`governed_chat_model` 复用 cnetmod 的协程信号量、通用 Token Bucket 和断路器，实现并发隔离、请求速率限制与快速失败；它可与负责重试/模型故障转移的 `resilient_chat_model` 按装饰器顺序组合。两者均保留真正的异步流式调用，不会退化为完整响应后再伪造单个 chunk；重试仅允许发生在尚未向消费者交付任何 chunk 时，防止部分输出被重复或与备用供应商输出拼接。

`routed_chat_model` 在每次普通或流式调用前通过 `chat_model_router` 异步选择模型。`functional_chat_model_router` 可根据 `run_config` 中的租户、任务等级、成本或区域元数据实现应用策略；路由失败或未选择模型时可使用显式 fallback。路由层仍实现统一 `chat_model` 契约，因此可继续与重试、治理、Agent 和 AI Service 组合。

`telemetry_listener` 将 `run_config` 的嵌套生命周期事件转成低基数 OpenMetrics：操作总数、活动数、延迟直方图、输入/输出 token、重试、治理拒绝、Span 丢弃和按配置价格估算的成本。相同 `run_id` 内的 Agent、模型、工具与检索 Span 共享 W3C Trace ID；可使用通用 `span_exporter`，也可直接连接有界非阻塞的 `otlp_http_exporter`。提示词、输出和工具参数默认不进入 Span，必须显式启用且受单属性容量限制；运行 ID、租户和标签不会进入指标标签，避免高基数污染。`run_scope` 以 RAII 保证正常返回、业务错误、取消和异常退出都产生配对结束事件，并用唯一操作标识正确关联并行调用。

`image_model` 与 `moderation_model` 将图像生成和内容审核提升为可替换的模型策略，统一接收 `run_config` 并在发起网络请求前检查取消状态。内置 OpenAI Adapter 复用现有异步客户端并发出一致的模型生命周期事件，应用也可以实现相同契约接入其他供应商。`moderation_input_guardrail` 优先依赖这一供应商无关契约，同时保留直接接收 `client` 的兼容构造方式。

`chat_model::stream` 是供应商无关的异步流式接口，消费者返回 `task<bool>` 形成自然背压并可提前终止。OpenAI 适配器会聚合文本、工具调用分片、finish reason 与 token 用量；`agent_executor::stream` 和 `ai_service::stream` 将流式能力贯通工具循环、RAG、记忆和 Guardrail。输出策略触发重新生成时，`chat_chunk::generation_attempt` 用于区分每次候选输出。

`file_document_source`（兼容名 `text_file_source`）使用 `async_file_read_all` 加载文件，并保留来源路径、文件名和扩展名元数据；不会在协程中执行同步文件读取。`directory_document_source` 将目录发现工作桥接到 `thread_pool`，支持递归开关、大小写无关的扩展名白名单、文件数量和单文件容量上限、不可读文件策略；它忽略符号链接，以标准化路径排序后再异步读取，并附加 `source_root` 元数据，因此相同目录的摄取顺序可重复。`url_document_source` 通过可替换 `document_fetcher` 获取远程内容；内置 `http_document_fetcher` 复用 cnetmod 异步 HTTP 客户端，传播取消、验证 2xx 状态并限制响应体容量，同时保存 URL 与 Content-Type 元数据。

`document_parser_registry` 根据标准化扩展名或 HTTP `Content-Type` 选择 `document_parser`，媒体类型优先且会忽略大小写和参数，也可配置兜底解析器；文件与目录 Source 只负责取得字节和来源元数据。内置 `plain_text_document_parser` 处理 UTF-8 BOM、非法 UTF-8、嵌入 NUL 和空文档；`markdown_document_parser` 提取有界的 YAML 风格 front matter 与一级标题，并可在索引内容中移除 front matter；`html_document_parser` 提取标题、解码常用及数字实体、保留块级换行，并排除 `head`、脚本、样式、模板和 `noscript` 内容。`functional_document_parser` 可将 PDF、Office、Tika、Docling 等异步业务解析器适配到同一契约，无需修改摄取流水线。

摄取流水线可依次组合任意多个 `document_transformer`。内置 `metadata_enricher` 以可配置覆盖策略补充来源标签，`document_filter` 按应用谓词筛选文档，`functional_document_transformer` 将业务异步转换适配到统一接口；所有转换都传播 `run_config` 取消状态。`recursive_text_splitter` 适合一般文本并保留字符偏移，`markdown_header_splitter` 按 ATX 标题分段并附加标题、层级、源文档和稳定 section ID，便于检索结果回溯原章节。

`evaluation_suite` 对一组 `evaluation_case` 运行多个 `response_evaluator`，保留每个用例、每个评测器的分数、通过状态与解释，并计算总体通过/失败数量和平均分。内置 `exact_match_evaluator` 支持大小写及空白归一化，`semantic_similarity_evaluator` 复用供应商无关的 `embedding_model` 计算余弦相似度和通过阈值；`model_judge_evaluator` 使用确定性温度和严格 JSON Schema 约束模型裁判的分数与解释，并由应用阈值决定是否通过。

`bind_tool` 使用 `tool_binding<Arguments, Result>` 将协议边界的 JSON Schema 校验、参数解码、强类型异步命令和结果编码分离。业务处理函数直接接收 C++ 参数对象，不必在命令实现中手工读取 JSON 字段。

`tool_provider` 在 Agent 每次调用开始时解析可见工具；动态供应器还能在每轮模型调用前根据完整会话重新解析。`tool_provider_request` 携带会话消息、session ID、调用参数、迭代次数和调用配置。每个 `executable_tool` 可用 `tool_return_behavior` 选择把结果返回模型、立即作为最终结果返回，或仅在它是本轮最后一个调用时立即返回；需要贯穿取消、租户信息或追踪上下文时使用 `contextual_tool_handler`，普通一参数 `tool_handler` 保持兼容。

`tool_registry::invoke_detailed` 返回 `tool_error`，其 `tool_error_kind` 明确区分取消、工具不存在、参数解析或 Schema 校验失败以及业务处理失败。`agent_options::handle_tool_error` 可按应用策略把可恢复错误转换为模型可见结果，或终止本次调用；未配置时继续兼容 `continue_on_tool_error`。向 Agent 提供专用 `io_context` 并调用 `with_parallel_tool_execution` 后，同一轮的多个工具调用使用结构化 `task_group` 并发执行，等待全部子任务结束后仍按模型给出的调用顺序写回消息。上下文感知工具可在长操作内部检查取消令牌；并行工具处理函数及运行监听器必须自行满足并发安全要求。

启用 `agent_executor::with_tool_search` 后，默认工具不会一次性暴露给模型，只保留 `tool_visibility::always_visible` 工具和 `find_tools` 检索命令。模型完成检索后，命中的工具从下一轮开始可见。`keyword_tool_search` 提供无外部依赖的确定性匹配；`semantic_tool_search` 通过供应商无关的 `embedding_model` 按向量相似度排序。

`skill_catalog` 实现渐进式 Agent Skills：初始只暴露激活、停用和资源读取命令；`activate_skill` 的工具结果保存技能激活状态和指令，下一轮才加入技能专属工具及嵌套 Tool Provider。`filesystem_skill_loader` 在 executor 上预加载 `SKILL.md` 与相对资源，限制单文件和总容量、忽略符号链接及隐藏文件，模型推理期间不直接访问文件系统。

`mcp_client` 支持 MCP 初始化协商、ping、`tools/list`、`tools/call`、资源/资源模板、资源订阅与退订、提示词、参数补全和服务端日志级别 API，并可把远程工具注册到 `tool_registry`。`mcp_tool_provider` 可聚合多个客户端，使用任意多个 client-aware filter 限制工具，允许运行时增删客户端，并可选择忽略单个服务失败或整体失败；同名工具必须通过过滤器消歧。客户端入站分派器支持 `sampling/createMessage`、`roots/list`、`elicitation/create` 和通知回调，因此资源更新、日志等服务端通知可由应用统一处理。`mcp_streamable_http_transport` 可处理包含多个 JSON-RPC 事件的 JSON/SSE 响应、回复服务端请求并传播 session；`mcp_stdio_transport` 使用 `child_process` 和 executor bridge 与本地 MCP server 双向交换换行分隔 JSON-RPC，并确定性回收子进程。

`response_request` 对常用字段提供强类型成员，包括 `previous_response_id`、`tool_outputs`、`max_output_tokens`、`max_tool_calls`、`reasoning`、`service_tier`、`prompt_cache_key` 和 `safety_identifier`。托管工具/MCP 工具及新增输入项可分别通过 `additional_tools`、`additional_input_items` 扩展；尚未建模的新字段可放入 `extra_body`（最后合并，同名字段会覆盖强类型序列化结果）。

### 场景：Chat Completions

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.openai;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::openai::client client(ctx);
    co_await client.connect({.api_key = "sk-..."});

    cn::openai::chat_request req{
        .model = "gpt-4o-mini",
        .messages = {
            cn::openai::message::system("You are a helpful assistant."),
            cn::openai::message::user("What is C++23?"),
        },
        .temperature = 0.7,
        .max_tokens = 512,
    };

    auto resp = co_await client.chat(req);
    if (resp) {
        cn::logger::info{"Reply: {}", resp->content()};
        cn::logger::info{"Tokens: prompt={}, completion={}",
            resp->token_usage.prompt_tokens, resp->token_usage.completion_tokens};
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

### 场景：Responses API

```cpp
cn::openai::response_request req{
    .model = "gpt-4.1-mini",
    .input = {cn::openai::message::user("Return a concise summary")},
    .instructions = "Be factual.",
    .max_output_tokens = 512,
    .prompt_cache_key = "summary-v1",
    .response_schema_name = "summary",
    .response_schema = {
        {"type", "object"},
        {"properties", {{"summary", {{"type", "string"}}}}},
        {"required", {"summary"}},
        {"additionalProperties", false},
    },
};
auto response = co_await client.responses(std::move(req));
if (response)
    cn::logger::info{"Response: {}", response->output_text};
```

### 场景：流式 Chat（SSE）

```cpp
cn::openai::chat_request req{
    .model = "gpt-4o-mini",
    .messages = {cn::openai::message::user("Explain async programming")},
    .stream = true,
};

// 同步回调版本
auto full = co_await client.chat_stream(req, [](const cn::openai::chat_chunk& chunk) {
    cn::logger::info{"{}", chunk.delta_content};
});

// 异步回调版本（可在回调中 co_await）
auto full = co_await client.chat_stream_async(req,
    [](const cn::openai::chat_chunk& chunk) -> cn::task<bool> {
        cn::logger::info{"{}", chunk.delta_content};
        co_return true; // return false to abort
    });

// 调用方取消会中止挂起的网络读取并淘汰当前连接。
cn::cancel_token cancellation;
auto cancellable = co_await client.chat_stream_async(req, callback, cancellation);
```

流式回调按完整 SSE event 实时触发，不等待整个 HTTP body。客户端兼容
`data:` 与 `data: `，并以 `[DONE]`、任意非空 `finish_reason`、HTTP
分帧结束或连接关闭作为完成边界。请求 `stream_options.include_usage` 时，
客户端会在 `finish_reason` 后继续接收独立 usage 尾帧，并使用一秒有界等待
兼容省略 usage 与 `[DONE]` 的网关。消费端返回 `false` 时立即关闭当前连接，
避免未消费的增量污染下一次请求。
每次流式网络读取受 `connect_options::timeout_seconds` 限制；调用方取消、
读取超时、写入失败和解析失败都会关闭连接，后续请求通过自动重连获得干净会话。

### 场景：Runnable 与结构化输出

```cpp
cn::openai::chat_prompt_template prompt{{
    {.role = "system", .prompt = cn::openai::prompt_template{"Return JSON only."}},
    {.role = "user", .prompt = cn::openai::prompt_template{"Question: {question}"}},
}};
auto parser = std::make_shared<cn::openai::json_output_parser>(
    cn::openai::json{{"type", "object"},
        {"properties", {{"answer", {{"type", "string"}}}}},
        {"required", {"answer"}}});

cn::openai::openai_chat_model model{client};
cn::openai::runnable chain{cn::openai::prompt_runnable(std::move(prompt))};
chain = chain.pipe(cn::openai::model_runnable(model))
            .pipe(cn::openai::parser_runnable(std::move(parser)));
auto result = co_await chain.invoke(
    cn::openai::prompt_variables{{"question", "What is C++23?"}});
```

普通文本输出不需要安装 `output_parser`。条件与列表 Prompt 使用显式
`prompt_context`：

```cpp
cn::openai::prompt_template scoped{
    "{?title}{title}\n{/title}"
    "{#scopes}- {name}: {value|unset}\n{/scopes}"};
cn::openai::prompt_context values{
    .variables = {{"title", "Permissions"}},
    .sections = {{"scopes", {
        cn::openai::prompt_section{{{"name", "read"}, {"value", "allowed"}}},
        cn::openai::prompt_section{{{"name", "write"}}},
    }}},
};
auto rendered = scoped.format_context(values);
```

### 场景：工具调用 Agent

```cpp
cn::openai::tool_registry tools;
auto registered = tools.add({
    .definition = {
        .function_name = "lookup_weather",
        .function_description = "Look up weather by city",
        .function_parameters = {
            {"type", "object"},
            {"properties", {{"city", {{"type", "string"}}}}},
            {"required", {"city"}},
            {"additionalProperties", false},
        },
    },
    .handler = [](const cn::openai::json& arguments)
        -> cn::task<std::expected<cn::openai::json, std::string>> {
        co_return cn::openai::json{{"city", arguments["city"]}, {"temperature", 24}};
    },
});
if (!registered)
    co_return;

cn::openai::conversation_memory memory{{.max_messages = 32}};
cn::openai::openai_chat_model model{client};
cn::openai::agent_executor agent{model, tools, &memory,
    {.max_iterations = 6, .system_prompt = "Use tools when required."}};
agent.with_parallel_tool_execution(ctx);
auto answer = co_await agent.invoke("Shanghai weather?");
```

需要按错误类别定义恢复策略时，在 `agent_options` 中安装异步错误处理器：

```cpp
cn::openai::agent_options options;
options.handle_tool_error = [](const cn::openai::tool_call&,
                                const cn::openai::tool_error& error,
                                const cn::openai::run_config&)
    -> cn::task<cn::openai::tool_error_resolution> {
    if (error.kind == cn::openai::tool_error_kind::invalid_arguments)
        co_return {cn::openai::tool_error_action::return_to_model,
            R"({"recoverable":true,"reason":"invalid arguments"})"};
    co_return {cn::openai::tool_error_action::fail_invocation, error.message};
};
cn::openai::agent_executor governed_agent{model, tools, nullptr,
    std::move(options)};
```

工具集合依赖用户权限或会话状态时，使用动态供应器：

```cpp
cn::openai::functional_tool_provider provider{
    [](const cn::openai::tool_provider_request& request)
        -> cn::task<std::expected<cn::openai::tool_provider_result, std::string>> {
        std::vector<cn::openai::executable_tool> allowed;
        // 根据 request.session_id、request.invocation_parameters 和
        // request.conversation 选择当前一轮允许暴露的工具。
        co_return cn::openai::tool_provider_result{.tools = std::move(allowed)};
    },
    true}; // true 表示每轮模型调用前重新解析

cn::openai::agent_executor agent{model, provider};
auto answer = co_await agent.invoke("Execute the permitted operation", {},
    {.metadata = {{"session_id", "tenant-42"}, {"role", "operator"}}});
```

### 场景：对话记忆与 RAG

数据库消息表实现 `append_only_chat_memory_store` 后，可以直接作为
`conversation_memory` 后端。框架追加时不会先读取或重写历史；批量追加必须在
同一数据库事务中完成。业务分页使用 `count(session_id)` 获取总数，再通过
`load_page(session_id, offset, limit)` 读取稳定插入顺序的页面；模型上下文使用
`load_recent(session_id, limit)` 读取最近窗口：

```cpp
database_chat_store store{/* application repository dependencies */};
cn::openai::conversation_memory memory{"session-42", store,
    {.max_messages = 32, .max_tokens = 8'000}};
co_await memory.append(cn::openai::message::user("Hello"));
auto context_messages = co_await memory.snapshot();

// 只复用框架窗口策略时，无需实现 Store。
auto trimmed = cn::openai::trim_messages(messages,
    {.max_messages = 32,
     .max_tokens = 8'000,
     .pinned_prefix_messages = 1,
     .preserved_tail_messages = 1});
if (!trimmed.limit_satisfied)
    cn::logger::warn{"Protected prompt messages exceed the configured budget"};
```

跨会话用户记忆使用独立的 Long-term Store：

```cpp
cn::openai::in_memory_long_term_store memories{&embeddings};
auto saved = co_await memories.put({"users", user_id}, "preferences",
    cn::openai::json{{"theme", "dark"}, {"language", "zh-CN"}},
    {.ttl = std::chrono::hours{24 * 30}, .expected_version = 0});

auto relevant = co_await memories.search({
    .namespace_prefix = {"users", user_id},
    .query = "preferred response language",
    .limit = 5,
});
```

需要暂停恢复、分支和审计历史的 Agent 使用通用 Checkpointer Adapter：

```cpp
cn::openai::in_memory_checkpoint_store checkpoints;
cn::openai::checkpoint_agentic_scope_store workflow_store{checkpoints};
cn::openai::agentic_runtime runtime{ctx, workflow_store};

auto result = co_await runtime.execute("workflow-42", planner, initial_state);
auto history = co_await checkpoints.list("workflow-42", "main", 20);
auto branch = co_await checkpoints.fork(
    "workflow-42", "main", 2, "experiment");
auto restored = co_await checkpoints.rollback(
    "workflow-42", "main", 1, history->front().version);
```

```cpp
cn::thread_pool cpu_pool{2};
cn::openai::openai_embedding_model embeddings{client};
cn::openai::in_memory_vector_store store{ctx, cpu_pool, embeddings};
co_await store.add_documents({
    {.id = "guide", .page_content = "cnetmod uses C++23 modules."},
});

cn::openai::chat_prompt_template rag_prompt{{
    {.role = "system", .prompt = cn::openai::prompt_template{"Use this context:\n{context}"}},
    {.role = "user", .prompt = cn::openai::prompt_template{"{input}"}},
}};
cn::openai::openai_chat_model model{client};
cn::openai::retrieval_chain rag{store, model, std::move(rag_prompt),
    {.limit = 4, .minimum_score = 0.2F}};
auto result = co_await rag.invoke("How are modules organized?");
```

### 场景：韧性、取消与运行追踪

```cpp
cn::openai::openai_chat_model primary{client};
cn::openai::resilient_chat_model resilient{ctx, primary, {},
    {.max_attempts_per_model = 3,
     .initial_backoff = std::chrono::milliseconds{100},
     .max_backoff = std::chrono::seconds{2}}};

cn::cancel_token cancellation;
cn::openai::run_config config{
    .run_id = "request-42",
    .tags = {"production"},
    .callback = [](const cn::openai::run_event& event) {
        cn::logger::debug{"OpenAI run={} event={} name={}",
            event.run_id, static_cast<int>(event.type), event.name};
    },
    .cancellation = &cancellation,
};
```

### 场景：Embeddings

```cpp
cn::openai::embedding_request req{
    .model = "text-embedding-3-small",
    .input = {"Hello world", "C++ modules"},
    .dimensions = 512,
};
auto resp = co_await client.embeddings(req);
if (resp) {
    for (auto& d : resp->data) {
        cn::logger::info{"Embedding[{}] size={}", d.index, d.embedding.size()};
    }
}
```

### 场景：DALL-E 图片生成

```cpp
cn::openai::image_generation_request req{
    .model = "dall-e-3",
    .prompt = "A futuristic cityscape at sunset",
    .quality = "hd",
    .size = "1792x1024",
};
auto resp = co_await client.create_image(req);
if (resp && !resp->data.empty()) {
    cn::logger::info{"Image URL: {}", resp->data[0].url};
}
```

### 场景：TTS / STT

```cpp
// TTS: 文字转语音
cn::openai::tts_request tts{
    .model = "tts-1",
    .input = "Hello, this is a test.",
    .voice = "alloy",
    .response_format = "mp3",
};
auto audio = co_await client.text_to_speech(tts);

// STT: 语音转文字
cn::openai::transcription_request stt{
    .file = audio_bytes,
    .filename = "audio.mp3",
    .language = "en",
};
auto transcript = co_await client.transcribe(stt);
if (transcript) cn::logger::info{"Text: {}", transcript->text};
```

---

## Part 2: Mail (SMTP)

### 场景导航

- 我要发送邮件 → [看这里](#场景smtp-发送邮件)
- 我要搭建 SMTP 服务端 → [看这里](#场景smtp-服务端)

### API 参考

#### `message` — 邮件消息

**签名**: `export struct message`（`cnetmod::mail` 命名空间）

```cpp
struct message {
    using header = std::pair<std::string, std::string>;
    std::vector<header> headers;
    std::string body;
    void set_header(std::string name, std::string value);
    auto header_value(std::string_view name) const -> std::optional<std::string_view>;
};
```

#### `envelope` — 邮件信封

**签名**: `export struct envelope`

```cpp
struct envelope {
    std::string sender;
    std::vector<std::string> recipients;
    void add_recipient(std::string recipient);
};
```

#### `client` — SMTP 客户端

**签名**: `export class client`（`cnetmod::mail::client`）

```cpp
struct client_options {
    bool tls = false;       // SMTPS (port 465)
    bool starttls = false;  // STARTTLS 升级
    std::string hostname;
    std::uint16_t port = 25;
    bool verify = true;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit client(io_context&, client_options = {}) noexcept` | |
| `connect` | `auto connect(string_view host, uint16_t port = 0) -> task<std::expected<void, std::string>>` | 连接并 EHLO |
| `authenticate` | `auto authenticate(string_view user, string_view pass, auth_mechanism = plain) -> task<...>` | 认证 |
| `send` | `auto send(const envelope&, const message&) -> task<std::expected<void, std::string>>` | 发送邮件 |
| `quit` | `auto quit() -> task<std::expected<void, std::string>>` | 退出 |
| `close` | `void close() noexcept` | 关闭连接 |

支持的认证机制：`plain`, `login`, `cram_md5`, `xoauth2`, `oauthbearer`, `external`

#### `server` — SMTP 服务端

**签名**: `export class server`（`cnetmod::mail::server`）

```cpp
struct server_options {
    std::string hostname = "localhost";
    std::size_t max_message_size = 25U * 1024U * 1024U;
    std::size_t max_recipients = 100;
    bool require_auth = false;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `server(io_context&, server_options = {})` | |
| `listen` | `auto listen(string_view host, uint16_t port) -> std::expected<void, std::error_code>` | 监听端口 |
| `set_message_handler` | `void set_message_handler(recipient_handler)` | 设置消息处理器 |
| `set_authenticator` | `void set_authenticator(authenticator)` | 设置认证回调 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop()` | 停止服务 |

### 场景：SMTP 发送邮件

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.mail;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::mail::client client(ctx, {
        .tls = true,
        .hostname = "smtp.example.com",
        .port = 465,
    });
    co_await client.connect("smtp.example.com", 465);
    co_await client.authenticate("user@example.com", "password");

    cn::mail::envelope env;
    env.sender = "user@example.com";
    env.add_recipient("recipient@example.com");

    cn::mail::message msg;
    msg.set_header("From", "user@example.com");
    msg.set_header("To", "recipient@example.com");
    msg.set_header("Subject", "Hello from cnetmod");
    msg.body = "This is a test email sent via cnetmod SMTP client.";

    auto result = co_await client.send(env, msg);
    if (result) cn::logger::info{"Email sent successfully!"};

    co_await client.quit();
    ctx.stop();
}
```

### 场景：SMTP 服务端

```cpp
auto run_server(cn::io_context& ctx) -> cn::task<void> {
    cn::mail::server server(ctx, {.hostname = "mail.example.com"});
    server.set_message_handler(
        [](const cn::mail::envelope& env, const cn::mail::message& msg)
            -> cn::task<std::expected<void, std::error_code>> {
            cn::logger::info{"Received mail from {} to {}", env.sender, env.recipients[0]};
            co_return std::expected<void, std::error_code>{};
        });
    server.listen("0.0.0.0", 2525);
    co_await server.run();
}
```

---

## Part 3: DNS

### 场景导航

- 我要异步解析域名 → [看这里](#场景dns-客户端查询)
- 我要搭建 DNS 服务端 → [看这里](#场景dns-服务端)
- 我要使用 DoH / DoT → [看这里](#场景doh--dot)

### API 参考

#### DNS 类型

**签名**: `export enum class record_type : std::uint16_t` — `A`(1), `NS`(2), `CNAME`(5), `SOA`(6), `PTR`(12), `MX`(15), `TXT`(16), `AAAA`(28), `SRV`(33), `HTTPS`(65)

**签名**: `export enum class response_code : std::uint8_t` — `no_error`(0), `format_error`(1), `server_failure`(2), `name_error`(3), `refused`(5)

```cpp
export struct question { std::string name; record_type type; record_class cls; };
export struct resource_record { std::string name; record_type type; record_class cls; std::uint32_t ttl; std::vector<std::byte> data; };
export struct message {
    std::uint16_t id;
    bool query; bool recursion_desired;
    response_code rcode;
    std::vector<question> questions;
    std::vector<resource_record> answers;
    std::vector<resource_record> authorities;
    std::vector<resource_record> additionals;
};
```

#### DNS Codec

```cpp
auto parse_message(std::span<const std::byte>) -> std::expected<message, std::error_code>;
auto serialize_message(const message&) -> std::expected<std::vector<std::byte>, std::error_code>;
auto make_query(std::string_view name, record_type, uint16_t id = 0) -> message;
auto a_record(std::string_view name, const ipv4_address&, uint32_t ttl = 60) -> resource_record;
auto aaaa_record(std::string_view name, const ipv6_address&, uint32_t ttl = 60) -> resource_record;
auto txt_record(std::string_view name, std::string_view text, uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>;
auto cname_record(std::string_view name, std::string_view canonical, uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>;
```

#### `udp_client` / `tcp_client` — DNS 客户端

**签名**: `export class udp_client` / `export class tcp_client`（`cnetmod::dns` 命名空间）

| 方法 | 签名 | 说明 |
|------|------|------|
| `udp_client::query` | `auto query(const endpoint& server, const message&) -> task<std::expected<message, std::error_code>>` | UDP 查询 |
| `tcp_client::query` | `auto query(string_view host, uint16_t port, const message&) -> task<...>` | TCP 查询 |

#### `doh_client` / `dot_client` — 加密 DNS

**签名**: `export class doh_client`（DNS over HTTPS）

```cpp
explicit doh_client(io_context&, std::string endpoint_url = "https://dns.google/dns-query");
auto query(const message&) -> task<std::expected<message, std::error_code>>;
```

**签名**: `export class dot_client`（DNS over TLS，需 `CNETMOD_HAS_SSL`）

```cpp
explicit dot_client(io_context&);
auto query(string_view host, uint16_t port, const message&) -> task<...>;
```

#### `udp_server` / `tcp_server` — DNS 服务端

**签名**: `export class udp_server` / `export class tcp_server`（`cnetmod::dns` 命名空间）

| 方法 | 签名 | 说明 |
|------|------|------|
| `listen` | `auto listen(string_view host, uint16_t port, socket_options) -> std::expected<void, std::error_code>` | 监听 |
| `set_handler` | `void set_handler(query_handler)` | 设置查询处理器 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop() noexcept` | 停止 |

`dot_server` 需额外传入 `dot_server_options{.cert_file, .key_file, .verify_peer}`。

### 场景：DNS 客户端查询

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.dns;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::dns::udp_client client(ctx);
    auto server = cn::endpoint{cn::ip_address{cn::ipv4_address{8,8,8,8}}, 53};

    auto query = cn::dns::make_query("example.com", cn::dns::record_type::A, 1);
    auto resp = co_await client.query(server, query);
    if (resp) {
        for (auto& rr : resp->answers) {
            cn::logger::info{"Answer: {} TTL={}", rr.name, rr.ttl};
        }
    }
    ctx.stop();
}
```

### 场景：DoH / DoT

```cpp
// DNS over HTTPS
cn::dns::doh_client doh(ctx, "https://dns.google/dns-query");
auto query = cn::dns::make_query("example.com", cn::dns::record_type::A);
auto resp = co_await doh.query(query);

// DNS over TLS (需 -DCNETMOD_ENABLE_SSL=ON)
cn::dns::dot_client dot(ctx);
auto resp2 = co_await dot.query("dns.google", 853, query);
```

### 场景：DNS 服务端

```cpp
auto run_dns_server(cn::io_context& ctx) -> cn::task<void> {
    cn::dns::udp_server server(ctx);
    server.set_handler([](const cn::dns::message& query, const cn::endpoint& peer)
        -> cn::task<cn::dns::message> {
        cn::dns::message resp;
        resp.id = query.id;
        resp.query = false;
        resp.recursion_desired = true;
        resp.recursion_available = true;
        for (auto& q : query.questions) {
            if (q.type == cn::dns::record_type::A && q.name == "example.com") {
                resp.answers.push_back(
                    cn::dns::a_record("example.com", cn::ipv4_address{93,184,216,34}));
            }
        }
        co_return resp;
    });
    server.listen("0.0.0.0", 5353);
    co_await server.run();
}
```

## Do's & Don'ts

- **Do**: OpenAI 客户端支持自动重连，连接断开后下次调用会自动 reconnect
- **Do**: 优先使用 Responses API 的 `previous_response_id` 延续多轮状态；函数工具结果用 `response_request::tool_outputs`
- **Do**: 流式响应完成或调用方主动停止后会关闭当前连接，下一次请求自动重连，避免残余 chunk 污染后续响应
- **Do**: `extra_body` 仅用于尚未建模的 OpenAI 新字段；稳定字段优先使用强类型成员
- **Do**: SMTP 发送邮件时根据服务端要求选择 `tls`（端口 465）或 `starttls`（端口 587）
- **Do**: DNS 查询使用 `make_query` 构建标准查询，避免手动构造 message
- **Don't**: 不要在 OpenAI `chat_stream` 回调中执行耗时操作，会阻塞 SSE 解析
- **Don't**: DNS `udp_client` 单次查询限制 512 字节，大响应需用 `tcp_client`
<!-- END SOURCE: skill/protocols/openai-mail-dns.md -->

<!-- BEGIN SOURCE: skill/protocols/raft.md -->
# Source: `skill/protocols/raft.md`

# Raft

> Raft 共识算法实现，支持 Leader 选举、日志复制、快照、动态成员变更和 TCP 传输。

**import**: `import cnetmod.protocol.raft;`
**CMake**: `-DCNETMOD_ENABLE_RAFT=ON`
**源码**: `src/protocol/raft/`

## 场景导航

- 我要创建 Raft 集群节点 → [看这里](#场景创建-raft-集群)
- 我要实现自定义状态机 → [看这里](#场景自定义状态机)
- 我要持久化存储 → [看这里](#场景持久化存储)
- 我要配置 TCP 传输层 → [看这里](#场景tcp-传输层)
- 我要理解选举与日志复制 → [看这里](#场景选举与日志复制)
- 我要使用快照压缩日志 → [看这里](#场景快照机制)

## API 参考

### Raft 类型

**签名**: `export enum class node_role { follower, pre_candidate, candidate, leader };`

**签名**: `export enum class entry_type { no_op, command, configuration };`

```cpp
export using term_t = std::uint64_t;
export using log_index = std::uint64_t;
export using node_id = std::string;
export using group_id = std::string;
```

**签名**: `export struct raft_config`

```cpp
struct raft_config {
    node_id id;
    std::vector<node_id> peers;
    raft_options options;

    auto initial_configuration() const -> configuration_state;
    auto cluster_size() const noexcept -> std::size_t;
    auto majority() const noexcept -> std::size_t;
};
```

**签名**: `export struct raft_options`

```cpp
struct raft_options {
    std::chrono::milliseconds election_timeout{150};
    std::chrono::milliseconds heartbeat_interval{50};
    std::chrono::milliseconds leader_lease_timeout{100};
    bool pre_vote = true;
    bool check_quorum = true;
    bool lease_read = false;
    std::size_t max_entries_per_append = 128;
    std::size_t snapshot_chunk_size = 1024 * 1024;
};
```

**签名**: `export struct raft_error { raft_errc code; std::string message; };`

`raft_errc` 枚举值：`ok`, `stale_term`, `log_inconsistent`, `not_leader`, `not_voter`, `configuration_error`, `snapshot_required`, `storage_error`, `state_machine_error`, `backpressure`, `stopped`

### `raft_node` — Raft 节点核心

**签名**: `export class raft_node`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `raft_node(raft_config, std::shared_ptr<raft_storage>, state_machine* = nullptr)` | |
| `id` | `auto id() const noexcept -> std::string_view` | 节点 ID |
| `role` | `auto role() const noexcept -> node_role` | 当前角色 |
| `current_term` | `auto current_term() const noexcept -> term_t` | 当前任期 |
| `leader_id` | `auto leader_id() const noexcept -> std::string_view` | Leader ID |
| `metrics` | `auto metrics() const -> raft_metrics` | 获取指标 |
| `append_command` | `auto append_command(std::string command) -> std::expected<log_entry, raft_error>` | 追加命令（仅 Leader） |
| `begin_pre_vote` | `auto begin_pre_vote() -> request_vote_request` | 发起 Pre-Vote |
| `begin_election` | `auto begin_election() -> request_vote_request` | 发起选举 |
| `handle_request_vote` | `auto handle_request_vote(const request_vote_request&) -> request_vote_response` | 处理投票请求 |
| `handle_vote_response` | `auto handle_vote_response(const node_id&, const request_vote_response&) -> bool` | 处理投票响应 |
| `handle_append_entries` | `auto handle_append_entries(const append_entries_request&) -> append_entries_response` | 处理日志追加 |
| `handle_append_entries_response` | `auto handle_append_entries_response(const node_id&, const append_entries_response&) -> bool` | 处理追加响应 |
| `handle_install_snapshot` | `auto handle_install_snapshot(const install_snapshot_request&) -> install_snapshot_response` | 处理快照安装 |
| `create_snapshot` | `auto create_snapshot(std::string uri) -> std::expected<snapshot_metadata, raft_error>` | 创建快照 |
| `maybe_create_snapshot` | `auto maybe_create_snapshot(const raft_snapshot_policy&) -> std::expected<std::optional<snapshot_metadata>, raft_error>` | 按策略创建快照 |
| `transfer_leader` | `auto transfer_leader(const node_id& target) -> std::expected<std::optional<timeout_now_request>, raft_error>` | 转移 Leader |
| `enter_joint_configuration` | `auto enter_joint_configuration(std::vector<node_id>) -> std::expected<log_entry, raft_error>` | 联合配置变更 |
| `leave_joint_configuration` | `auto leave_joint_configuration() -> std::expected<log_entry, raft_error>` | 离开联合配置 |
| `set_learners` | `auto set_learners(std::vector<node_id>) -> std::expected<log_entry, raft_error>` | 设置 Learner |
| `promote_learner` | `auto promote_learner(const node_id&) -> std::expected<log_entry, raft_error>` | 提升 Learner |
| `remove_node` | `auto remove_node(const node_id&) -> std::expected<log_entry, raft_error>` | 移除节点 |
| `stop` | `void stop(raft_error error)` | 停止节点 |

### `raft_storage` — 存储接口

**签名**: `export class raft_storage`（抽象接口）

```cpp
virtual auto load_hard_state() -> hard_state = 0;
virtual void save_hard_state(const hard_state&) = 0;
virtual auto load_snapshot_metadata() -> snapshot_metadata = 0;
virtual void save_snapshot_metadata(const snapshot_metadata&) = 0;
virtual auto first_log_index() const -> log_index = 0;
virtual auto last_log_index() const -> log_index = 0;
virtual auto term_at(log_index) const -> term_t = 0;
virtual auto entry_at(log_index) const -> std::optional<log_entry> = 0;
virtual auto entries(log_index first, std::size_t max_entries) const -> std::vector<log_entry> = 0;
virtual void append(const std::vector<log_entry>&) = 0;
virtual void truncate_prefix(log_index first_kept) = 0;
virtual void truncate_suffix(log_index first_removed) = 0;
virtual void reset_to_snapshot(const snapshot_metadata&) = 0;
```

### `memory_store` — 内存存储

**签名**: `export class memory_store final : public raft_storage`

完整实现 `raft_storage` 接口，数据存储在内存中。适用于测试和非持久化场景。

### `leveldb_store` — LevelDB 持久化存储

**签名**: `export class leveldb_store final : public raft_storage`（需 `CNETMOD_HAS_LEVELDB`）

```cpp
explicit leveldb_store(std::string path);
void set_sync(bool enabled) noexcept;
```

### `state_machine` — 有限状态机接口

**签名**: `export class state_machine`

| 方法 | 签名 | 说明 |
|------|------|------|
| `on_apply` | `virtual void on_apply(const log_entry&) = 0` | 应用日志条目（必须实现） |
| `on_snapshot_save` | `virtual void on_snapshot_save(const snapshot_metadata&)` | 快照保存回调 |
| `on_snapshot_load` | `virtual void on_snapshot_load(const snapshot_metadata&)` | 快照加载回调 |
| `save_snapshot` | `virtual auto save_snapshot(const snapshot_writer&) -> std::expected<void, raft_error>` | 保存快照数据 |
| `load_snapshot` | `virtual auto load_snapshot(const snapshot_reader&) -> std::expected<void, raft_error>` | 加载快照数据 |
| `on_leader_start` | `virtual void on_leader_start(term_t)` | Leader 上任回调 |
| `on_leader_stop` | `virtual void on_leader_stop(term_t)` | Leader 卸任回调 |

### `log_manager` — 日志管理

**签名**: `export class log_manager`

```cpp
explicit log_manager(std::shared_ptr<raft_storage> storage);
auto append_as_leader(term_t, std::string command) -> log_entry;
auto append_noop(term_t) -> log_entry;
auto append_configuration(term_t, configuration_state) -> log_entry;
auto append_from_leader(log_index prev, term_t prev_term, const std::vector<log_entry>&) -> append_result;
auto restore_snapshot(const snapshot_metadata&) -> append_result;
void compact_prefix(log_index first_kept);
```

### `progress_tracker` — 复制进度跟踪

**签名**: `export class progress_tracker`

```cpp
progress_tracker(std::vector<node_id> peers, log_index next_index, std::size_t inflight_capacity);
auto get(const node_id& peer) -> peer_progress*;
void mark_sent(const node_id& peer, log_index last_index);
void mark_replicated(const node_id& peer, log_index index);
void mark_rejected(const node_id& peer, log_index rejected_next, log_index conflict = 0);
auto committed_index(log_index leader_match, std::size_t majority) const -> log_index;
```

### `configuration` — 集群配置管理

**签名**: `export class configuration`

| 方法 | 说明 |
|------|------|
| `contains(node_id)` | 是否为投票成员 |
| `is_member(node_id)` | 是否为任意成员 |
| `quorum_size()` | 法定人数 |
| `has_quorum(set<node_id>)` | 是否达到法定人数 |
| `with_joint(new_voters)` | 进入联合配置变更 |
| `with_learners(learners)` | 添加 Learner |
| `promote_learner(node_id)` | 提升 Learner 为 Voter |
| `remove_member(node_id)` | 移除成员 |
| `leave_joint()` | 完成联合配置 |

### `raft_tcp_transport` — TCP 传输层

**签名**: `export class raft_tcp_transport final : public raft_transport`

```cpp
struct raft_tcp_transport_options {
    std::uint32_t max_send_attempts = 3;
    std::chrono::milliseconds retry_backoff{20};
    std::filesystem::path snapshot_directory = "raft-snapshots";
    std::size_t snapshot_chunk_size = 1024 * 1024;
    raft_tcp_security_options security;
    raft_snapshot_retention_options snapshot_retention;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `raft_tcp_transport(io_context&, node_id local_id, options)` | |
| `set_node` | `void set_node(raft_node&) noexcept` | 绑定 Raft 节点 |
| `add_peer` | `void add_peer(raft_tcp_peer)` | 添加对端 |
| `serve` | `auto serve(endpoint) -> task<std::expected<void, std::error_code>>` | 监听入站连接 |
| `broadcast_pre_vote` | `void broadcast_pre_vote(raft_node&)` | 广播 Pre-Vote |
| `broadcast_request_vote` | `void broadcast_request_vote(raft_node&)` | 广播投票请求 |
| `replicate_to_all` | `void replicate_to_all(raft_node&)` | 向所有 Follower 复制日志 |
| `transfer_leader` | `auto transfer_leader(raft_node&, const node_id&) -> std::expected<void, raft_error>` | 转移 Leader |
| `cleanup_snapshot_files` | `auto cleanup_snapshot_files() -> task<std::expected<std::size_t, std::error_code>>` | 清理过期快照 |
| `stop` / `async_stop` | | 停止传输层 |

### `raft_node_runtime` — 运行时

**签名**: `export class raft_node_runtime`

```cpp
struct raft_runtime_options {
    bool start_tcp_server = true;
    bool auto_election = true;
    bool auto_heartbeat = true;
    bool auto_snapshot = false;
    raft_snapshot_policy snapshot_policy;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `raft_node_runtime(io_context&, raft_node&, raft_tcp_transport&, endpoint, raft_options, runtime_options)` | |
| `start` | `void start()` | 启动运行时（TCP + 选举 + 心跳） |
| `stop` | `void stop() noexcept` | 停止 |
| `async_stop` | `auto async_stop() -> task<void>` | 异步停止 |
| `tick_now` | `void tick_now()` | 立即触发一次 tick |
| `transfer_leader` | `auto transfer_leader(const node_id&) -> std::expected<void, raft_error>` | 转移 Leader |
| `maybe_snapshot_now` | `auto maybe_snapshot_now() -> std::expected<std::optional<snapshot_metadata>, raft_error>` | 手动触发快照 |
| `async_read_index` | `auto async_read_index(read_index_request) -> task<std::expected<read_index_response, raft_error>>` | 线性一致性读 |

## Do's & Don'ts

- **Do**: 实现 `state_machine::on_apply` 来应用日志命令，这是唯一必须实现的虚函数
- **Do**: 生产环境使用 `leveldb_store` 持久化，测试用 `memory_store`
- **Do**: 配置 `raft_snapshot_policy` 定期快照以压缩日志
- **Do**: 使用 `raft_node_runtime` 简化选举/心跳/快照的自动化管理
- **Don't**: 不要在非 Leader 节点调用 `append_command`，会返回 `raft_errc::not_leader`
- **Don't**: 不要忽略 `raft_tcp_transport::serve`，节点必须监听才能接收 RPC

## 场景：创建 Raft 集群

```cpp
import std;
import cnetmod.core.address;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.protocol.raft;

namespace raft = cnetmod::raft;

auto run_node(cnetmod::io_context& ctx, std::string id,
    std::vector<std::string> peers, std::uint16_t port) -> cnetmod::task<void>
{
    raft::raft_config cfg{.id = id, .peers = peers};
    auto store = std::make_shared<raft::memory_store>();
    raft::raft_node node(cfg, store, /*state_machine=*/nullptr);

    raft::raft_tcp_transport transport(ctx, id);
    transport.set_node(node);
    transport.add_peer({.id = peers[0], .address = cnetmod::endpoint{
        cnetmod::ip_address{cnetmod::ipv4_address{127,0,0,1}},
        static_cast<std::uint16_t>(port + 1)}});

    raft::raft_node_runtime runtime(ctx, node, transport,
        cnetmod::endpoint{cnetmod::ip_address{cnetmod::ipv4_address::any()}, port},
        cfg.options);
    runtime.start();

    // 等待直到停止
    co_await cnetmod::async_sleep(ctx, std::chrono::seconds(30));
    runtime.stop();
}
```

## 场景：自定义状态机

```cpp
class kv_state_machine final : public raft::state_machine {
public:
    void on_apply(const raft::log_entry& entry) override {
        if (entry.type != raft::entry_type::command) return;
        // 解析命令并应用到 KV 存储
        std::istringstream ss(entry.command);
        std::string op, key, value;
        ss >> op >> key >> value;
        if (op == "SET") data_[key] = value;
        else if (op == "DEL") data_.erase(key);
    }

    auto save_snapshot(const raft::snapshot_writer& writer)
        -> std::expected<void, raft::raft_error> override {
        // 序列化 data_ 到文件
        return {};
    }

    auto load_snapshot(const raft::snapshot_reader& reader)
        -> std::expected<void, raft::raft_error> override {
        // 从文件反序列化 data_
        return {};
    }

    void on_leader_start(raft::term_t term) override {
        std::println("Became leader at term {}", term);
    }

private:
    std::map<std::string, std::string> data_;
};
```

## 场景：持久化存储

```cpp
// LevelDB 持久化（需 -DCNETMOD_ENABLE_LEVELDB=ON）
if constexpr (raft::leveldb_store_available) {
    auto store = std::make_shared<raft::leveldb_store>("/var/raft/node1");
    store->set_sync(true); // 每次写都 fsync
    raft::raft_node node(cfg, store, &fsm);
}

// 内存存储（测试用）
auto store = std::make_shared<raft::memory_store>();
raft::raft_node node(cfg, store, &fsm);
```

## 场景：快照机制

```cpp
// 配置自动快照策略
raft::raft_snapshot_policy policy{
    .log_entries_threshold = 10000,  // 每 10000 条日志触发
    .min_interval = std::chrono::milliseconds{60000},
    .uri_prefix = "raft-snapshot"
};

// 运行时自动快照
raft::raft_runtime_options runtime_opts{
    .auto_snapshot = true,
    .snapshot_policy = policy
};
raft::raft_node_runtime runtime(ctx, node, transport, ep, cfg.options, runtime_opts);

// 手动触发快照
auto result = runtime.maybe_snapshot_now();
if (result && *result) {
    std::println("Snapshot created at index {}", (*result)->last_included_index);
}
```

## 参考示例

- `examples/raft/redis_cluster.cpp` — Redis 风格 KV 复制集群
- `examples/raft/oss_shared_storage.cpp` — OSS 风格对象存储（含分片）
- `examples/raft/raft_demo_cluster.hpp` — 三节点集群测试工具

## 连接池/连接管理（生产级用法）

### 说明

Raft 是分布式共识协议，**不适用传统连接池概念**。`raft_tcp_transport` 内部为每个 peer 维护独立的 TCP 长连接（`peer_connection`），自动管理重连、消息队列和发送重试：

```cpp
// raft_tcp_transport — 内部连接管理
export class raft_tcp_transport final : public raft_transport {
    raft_tcp_transport(io_context& ctx, node_id local_id,
        raft_tcp_transport_options options = {});

    void add_peer(raft_tcp_peer peer);          // 添加对端（自动建连）
    void remove_peer(const node_id& peer);      // 移除对端
    auto peers() const -> std::vector<node_id>;

    // 传输层指标（每 peer 独立统计）
    auto peer_metrics() const -> std::vector<raft_peer_transport_metrics>;
    auto peer_metrics(const node_id& peer) const
        -> std::optional<raft_peer_transport_metrics>;
};

// 传输选项 — 控制连接行为
struct raft_tcp_transport_options {
    std::uint32_t max_send_attempts = 3;        // 最大发送重试
    std::chrono::milliseconds retry_backoff{20}; // 重试退避
    std::size_t max_outbound_queue = 1024;       // 每 peer 最大出站队列
    raft_tcp_security_options security;          // TLS/认证
    raft_snapshot_retention_options snapshot_retention;
};

// 每 peer 传输指标
struct raft_peer_transport_metrics {
    node_id peer;
    bool connected = false;
    std::uint64_t queued_sends = 0;
    std::uint64_t send_successes = 0;
    std::uint64_t send_failures = 0;
    std::uint64_t reconnects = 0;
    std::uint64_t max_queue_depth = 0;
    std::chrono::steady_clock::duration last_queue_wait_latency{};
    std::chrono::steady_clock::duration last_send_latency{};
    std::chrono::steady_clock::time_point last_send_at{};
    std::chrono::steady_clock::time_point last_receive_at{};
    std::error_code last_error;
};
```

### 传输层连接监控

```cpp
import std;
import cnetmod.protocol.raft;

namespace raft = cnetmod::raft;

// 监控所有 peer 连接状态
void print_transport_metrics(const raft::raft_tcp_transport& transport) {
    auto metrics = transport.peer_metrics();
    for (auto& m : metrics) {
        std::println("Peer: {} | connected={} | successes={} | failures={} | "
                     "reconnects={} | queue={}",
            m.peer, m.connected, m.send_successes, m.send_failures,
            m.reconnects, m.queued_sends);
    }
}
```

## 多核/集群部署

### 部署模式

Raft 是分布式共识协议，"多核" 的含义是**多节点集群**而非多线程 worker。每个 Raft 节点运行在独立的 `io_context` 上，通过 `raft_tcp_transport` 互联。`raft_node_runtime` 自动管理选举、心跳和快照。

**模块不使用 `server_context`**，每个节点独立部署在单线程或多线程 `io_context` 上。

### 关键 API

```cpp
// raft_node_runtime — 自动化运行时
export class raft_node_runtime {
    raft_node_runtime(io_context& ctx, raft_node& node,
        raft_tcp_transport& transport, endpoint listen_endpoint,
        raft_options options,
        raft_runtime_options runtime_options = {});

    void start();    // 启动 TCP 服务 + 选举循环 + 心跳循环
    void stop() noexcept;
    auto async_stop() -> task<void>;
    void tick_now();
    auto transfer_leader(const node_id& target) -> std::expected<void, raft_error>;
    auto maybe_snapshot_now() -> std::expected<std::optional<snapshot_metadata>, raft_error>;
    auto async_read_index(read_index_request request)
        -> task<std::expected<read_index_response, raft_error>>;
};

// raft_runtime_options — 运行时控制
struct raft_runtime_options {
    bool start_tcp_server = true;   // 自动启动 TCP 监听
    bool auto_election = true;      // 自动发起选举
    bool auto_heartbeat = true;     // 自动发送心跳
    bool auto_snapshot = false;     // 自动快照（生产建议开启）
    raft_snapshot_policy snapshot_policy;
};
```

### 生产级三节点集群

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.raft;

namespace cn = cnetmod;
namespace raft = cn::raft;

// KV 状态机（生产级）
class kv_state_machine final : public raft::state_machine {
public:
    void on_apply(const raft::log_entry& entry) override {
        if (entry.type != raft::entry_type::command) return;
        auto pos = entry.command.find(' ');
        if (pos == std::string::npos) return;
        auto op = entry.command.substr(0, pos);
        auto rest = entry.command.substr(pos + 1);

        if (op == "SET") {
            auto eq = rest.find('=');
            if (eq != std::string::npos)
                data_[rest.substr(0, eq)] = rest.substr(eq + 1);
        } else if (op == "DEL") {
            data_.erase(rest);
        }
    }

    auto save_snapshot(const raft::snapshot_writer& writer)
        -> std::expected<void, raft::raft_error> override {
        std::ofstream out(writer.uri, std::ios::binary);
        for (auto& [k, v] : data_) {
            std::uint32_t ks = static_cast<std::uint32_t>(k.size());
            std::uint32_t vs = static_cast<std::uint32_t>(v.size());
            out.write(reinterpret_cast<const char*>(&ks), 4);
            out.write(k.data(), ks);
            out.write(reinterpret_cast<const char*>(&vs), 4);
            out.write(v.data(), vs);
        }
        return {};
    }

    auto load_snapshot(const raft::snapshot_reader& reader)
        -> std::expected<void, raft::raft_error> override {
        data_.clear();
        std::ifstream in(reader.uri, std::ios::binary);
        while (in) {
            std::uint32_t ks, vs;
            if (!in.read(reinterpret_cast<char*>(&ks), 4)) break;
            std::string k(ks, '\0');
            in.read(k.data(), ks);
            in.read(reinterpret_cast<char*>(&vs), 4);
            std::string v(vs, '\0');
            in.read(v.data(), vs);
            data_[k] = v;
        }
        return {};
    }

    void on_leader_start(raft::term_t term) override {
        std::println("成为 Leader, term={}", term);
    }
    void on_leader_stop(raft::term_t term) override {
        std::println("失去 Leader, term={}", term);
    }

    auto get(const std::string& key) const -> std::optional<std::string> {
        auto it = data_.find(key);
        return it != data_.end() ? std::optional{it->second} : std::nullopt;
    }

private:
    std::map<std::string, std::string> data_;
};

// 集群节点配置
struct cluster_node_config {
    std::string id;
    std::uint16_t port;
    std::vector<std::string> peer_ids;
    std::vector<std::pair<std::string, std::uint16_t>> peer_addrs;
};

auto run_cluster_node(cn::io_context& ctx, cluster_node_config cfg)
    -> cn::task<void>
{
    // 1. 配置 Raft 选项
    raft::raft_config raft_cfg{
        .id = cfg.id,
        .peers = cfg.peer_ids,
        .options = {
            .election_timeout = std::chrono::milliseconds(300),
            .heartbeat_interval = std::chrono::milliseconds(100),
            .leader_lease_timeout = std::chrono::milliseconds(200),
            .pre_vote = true,
            .check_quorum = true,
            .max_entries_per_append = 256,
        },
    };

    // 2. 持久化存储（生产环境用 LevelDB）
    auto store = std::make_shared<raft::leveldb_store>(
        std::format("/var/raft/{}", cfg.id));
    store->set_sync(true);

    // 3. 状态机
    kv_state_machine fsm;

    // 4. 创建节点
    raft::raft_node node(raft_cfg, store, &fsm);

    // 5. TCP 传输层
    raft::raft_tcp_transport_options transport_opts;
    transport_opts.max_send_attempts = 5;
    transport_opts.retry_backoff = std::chrono::milliseconds(50);
    transport_opts.snapshot_directory = std::format("/var/raft/{}/snapshots", cfg.id);
    transport_opts.snapshot_chunk_size = 2 * 1024 * 1024;  // 2MB 分块
    transport_opts.max_outbound_queue = 2048;

    // TLS 安全配置（生产环境建议开启）
    transport_opts.security.shared_secret = "raft-cluster-secret-key";
    transport_opts.security.require_auth_token = true;

    raft::raft_tcp_transport transport(ctx, cfg.id, transport_opts);
    transport.set_node(node);

    // 添加所有 peer
    for (std::size_t i = 0; i < cfg.peer_ids.size(); ++i) {
        auto& [peer_host, peer_port] = cfg.peer_addrs[i];
        transport.add_peer({
            .id = cfg.peer_ids[i],
            .address = cn::endpoint{
                cn::ip_address{cn::ipv4_address::from_string(peer_host)},
                peer_port},
        });
    }

    // 6. 运行时（自动选举 + 心跳 + 快照）
    raft::raft_snapshot_policy snap_policy{
        .log_entries_threshold = 10000,
        .min_interval = std::chrono::milliseconds(60000),
        .uri_prefix = std::format("raft-{}-snap", cfg.id),
    };

    raft::raft_runtime_options runtime_opts{
        .start_tcp_server = true,
        .auto_election = true,
        .auto_heartbeat = true,
        .auto_snapshot = true,
        .snapshot_policy = snap_policy,
    };

    auto listen_ep = cn::endpoint{
        cn::ip_address{cn::ipv4_address::any()}, cfg.port};

    raft::raft_node_runtime runtime(ctx, node, transport, listen_ep,
        raft_cfg.options, runtime_opts);

    runtime.start();
    std::println("节点 {} 启动, 监听端口 {}", cfg.id, cfg.port);

    // 7. 定期监控节点状态
    while (runtime.running()) {
        co_await cn::async_sleep(ctx, std::chrono::seconds(10));
        auto m = node.metrics();
        std::println("[{}] role={} term={} commit={} applied={} voters={} "
                     "learners={} pending_reads={}",
            cfg.id, raft::role_name(m.role), m.current_term,
            m.commit_index, m.last_applied, m.voters, m.learners,
            m.pending_reads);

        // 清理过期快照文件
        auto cleaned = co_await transport.cleanup_snapshot_files();
        if (cleaned && *cleaned > 0) {
            std::println("清理 {} 个过期快照", *cleaned);
        }
    }

    co_await runtime.async_stop();
}

// 启动三节点集群（同一进程内，生产环境应分进程/机器部署）
auto main() -> int {
    cn::net_init net;

    std::vector<cluster_node_config> nodes = {
        {"node1", 9001, {"node2", "node3"},
         {{"127.0.0.1", 9002}, {"127.0.0.1", 9003}}},
        {"node2", 9002, {"node1", "node3"},
         {{"127.0.0.1", 9001}, {"127.0.0.1", 9003}}},
        {"node3", 9003, {"node1", "node2"},
         {{"127.0.0.1", 9001}, {"127.0.0.1", 9002}}},
    };

    // 每个节点独立 io_context（生产环境应分进程）
    std::vector<std::thread> threads;
    for (auto& cfg : nodes) {
        threads.emplace_back([&cfg]() {
            cn::io_context ctx;
            cn::spawn(ctx, run_cluster_node(ctx, cfg));
            ctx.run();
        });
    }

    for (auto& t : threads) t.join();
    return 0;
}
```

### 线性一致性读（ReadIndex）

```cpp
// 通过 ReadIndex 实现线性一致性读（无需写入日志）
auto read_with_consistency(raft::raft_node_runtime& runtime,
    cn::io_context& ctx) -> cn::task<void>
{
    raft::read_index_request req{
        .id = 1,
        .context = "read-user-data",
    };

    auto result = co_await runtime.async_read_index(req);
    if (result) {
        std::println("ReadIndex: term={} index={} ready={}",
            result->term, result->index, result->ready);
        // ready=true 时可安全读取本地状态机
    }
}
```

### Leader 转移

```cpp
// 优雅下线前转移 Leader 到其他节点
auto graceful_shutdown(raft::raft_node_runtime& runtime) -> void {
    auto r = runtime.transfer_leader("node2");
    if (r) {
        std::println("Leader 转移已发起到 node2");
    } else {
        std::println("Leader 转移失败: {}", r.error().message);
    }
}
```
<!-- END SOURCE: skill/protocols/raft.md -->

<!-- BEGIN SOURCE: skill/protocols/socks5.md -->
# Source: `skill/protocols/socks5.md`

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
<!-- END SOURCE: skill/protocols/socks5.md -->

<!-- BEGIN SOURCE: skill/protocols/websocket.md -->
# Source: `skill/protocols/websocket.md`

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

- `examples/websocket/ws_demo.cpp` — 底层 connection + cnetmod task/spawn 并发
- `examples/websocket/hight_ws.cpp` — 高层 server 路由注册 + client
- `examples/websocket/multicore_ws.cpp` — server_context 多核分发
<!-- END SOURCE: skill/protocols/websocket.md -->

<!-- BEGIN SOURCE: skill/security/security-jwt.md -->
# Source: `skill/security/security-jwt.md`

# JWT 签发与验证

> 协程原生 JWT 模块，基于 jwt-cpp，CPU 密集操作卸载到 cnetmod 线程池。
> 模块: `import cnetmod.security.jwt;`

## 核心原则

- `sign_jwt()` / `verify_jwt()` 均为 `task<T>` 协程接口
- CPU 密集的签名/验证操作自动卸载到 `thread_pool`，不阻塞 IO 线程
- 使用 `std::expected<T, std::string>` 返回结果
- 当前仅支持 HS256（HMAC-SHA256 对称签名）

## 1. jwt_algorithm — 签名算法

```cpp
enum class jwt_algorithm
{
    hs256,  // HMAC-SHA256（对称）
    // rs256 预留，未来支持 RSA-SHA256
};
```

## 2. jwt_claims — JWT 声明

```cpp
struct jwt_claims
{
    std::string subject;
    std::string issuer;
    std::vector<std::string> scopes;
    std::chrono::system_clock::time_point issued_at;
    std::chrono::system_clock::time_point expires_at;
    /// 所有非标准自定义声明
    std::map<std::string, std::string> custom;
};
```

| 字段 | 标准 JWT Claim | 说明 |
|------|----------------|------|
| `subject` | `sub` | 主题（通常是用户 ID） |
| `issuer` | `iss` | 签发者 |
| `scopes` | 自定义 | 权限范围列表 |
| `issued_at` | `iat` | 签发时间 |
| `expires_at` | `exp` | 过期时间 |
| `custom` | 自定义 | 额外键值对 |

## 3. jwt_sign_options — 签发参数

```cpp
struct jwt_sign_options
{
    std::string issuer;
    std::string subject;
    std::vector<std::string> scopes;
    std::chrono::system_clock::duration lifetime = std::chrono::hours(1);
    jwt_algorithm algorithm = jwt_algorithm::hs256;
    /// 注入到 payload 的额外自定义声明
    std::map<std::string, std::string> custom_claims;
};
```

## 4. sign_jwt — 签发 JWT

```cpp
auto sign_jwt(thread_pool& pool, io_context& io,
              const jwt_sign_options& opts, std::string_view secret)
    -> task<std::expected<std::string, std::string>>;
```

| 参数 | 说明 |
|------|------|
| `pool` | cnetmod 线程池，用于卸载 CPU 密集操作 |
| `io` | io_context，完成后返回 IO 线程 |
| `opts` | 签发参数（issuer、subject、lifetime 等） |
| `secret` | HS256 密钥（或未来 RS256 的 PEM 私钥） |
| **返回** | JWT 字符串（`header.payload.signature`），失败返回错误信息 |

### 示例

```cpp
import std;
import cnetmod.security.jwt;
import cnetmod.executor.pool;
import cnetmod.io;

auto token_result = co_await cnetmod::security::sign_jwt(pool, io, {
    .issuer = "myapp",
    .subject = "user123",
    .scopes = {"read", "write"},
    .lifetime = std::chrono::hours(24)
}, "super-secret-key");

if (token_result)
    std::println("JWT: {}", *token_result);
else
    std::println("签发失败: {}", token_result.error());
```

## 5. verify_jwt — 验证 JWT

```cpp
auto verify_jwt(thread_pool& pool, io_context& io,
                std::string_view token, std::string_view secret)
    -> task<std::expected<jwt_claims, std::string>>;
```

| 参数 | 说明 |
|------|------|
| `pool` | cnetmod 线程池 |
| `io` | io_context |
| `token` | 编码的 JWT 字符串（`header.payload.signature`） |
| `secret` | HS256 验证密钥 |
| **返回** | 解析后的 `jwt_claims`，失败返回错误信息 |

### 示例

```cpp
auto claims_result = co_await cnetmod::security::verify_jwt(
    pool, io, token_value, "super-secret-key");

if (claims_result)
{
    auto& claims = *claims_result;
    std::println("用户: {}, 权限: {}", claims.subject, claims.scopes.size());

    if (!cnetmod::security::is_jwt_expired(claims))
        std::println("Token 有效");
    else
        std::println("Token 已过期");
}
else
{
    std::println("验证失败: {}", claims_result.error());
}
```

## 6. is_jwt_expired — 过期检查

```cpp
[[nodiscard]] inline auto is_jwt_expired(const jwt_claims& claims) -> bool
{
    return std::chrono::system_clock::now() > claims.expires_at;
}
```

轻量级检查，无密码学开销。

## 7. 线程池卸载模式

JWT 签名/验证涉及 HMAC-SHA256 计算，属于 CPU 密集操作。cnetmod 通过 `thread_pool` + `blocking_invoke` 将其从 IO 线程卸载:

```cpp
// 1. 创建线程池和 IO 上下文
auto ctx = cnetmod::make_io_context();
cnetmod::thread_pool pool;

// 2. 在协程中调用（自动卸载到线程池）
auto work = [&]() -> cnetmod::task<void>
{
    // sign_jwt 内部自动: IO线程 → 线程池执行加密 → 回到IO线程
    auto token = co_await cnetmod::security::sign_jwt(pool, *ctx, {
        .issuer = "myapp",
        .subject = "user123",
        .lifetime = std::chrono::hours(1)
    }, "secret");

    // verify_jwt 同理
    if (token)
    {
        auto claims = co_await cnetmod::security::verify_jwt(
            pool, *ctx, *token, "secret");
        // ...
    }
};

cnetmod::spawn(*ctx, work());
ctx->run();
```

## 8. 完整示例：HTTP 中间件中的 JWT

```cpp
import std;
import cnetmod.security.jwt;
import cnetmod.protocol.http;
import cnetmod.executor.pool;

auto jwt_middleware(cnetmod::thread_pool& pool, std::string_view secret)
{
    return [&](cnetmod::http::request& req, cnetmod::http::response& res,
               auto next) -> cnetmod::task<void>
    {
        auto auth = req.header("Authorization");
        if (!auth || !auth->starts_with("Bearer "))
        {
            res.status(401).body("Missing token");
            co_return;
        }

        auto token = auth->substr(7);
        auto result = co_await cnetmod::security::verify_jwt(
            pool, req.io_context(), token, secret);

        if (!result)
        {
            res.status(401).body("Invalid token");
            co_return;
        }

        if (cnetmod::security::is_jwt_expired(*result))
        {
            res.status(401).body("Token expired");
            co_return;
        }

        // 将用户信息注入请求上下文
        req.set("user_id", result->subject);
        co_await next();
    };
}
```

## CMake 依赖

JWT 模块位于 `cnetmod_core` 静态库中，无需额外 CMake 开关。
依赖 `3rdparty/jwt-cpp`（已内置）。
<!-- END SOURCE: skill/security/security-jwt.md -->
