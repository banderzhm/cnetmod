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
10. 耗时 CPU 操作和兼容其他协程库必须通过 executor（`thread_pool`/`spawn_on`）和 bridge（`blocking_invoke`/`await_sender`/`from_awaitable`）卸载，**禁止在协程中同步阻塞** — 参见 [executor-bridge.md](coro/executor-bridge.md)
11. C++23 模块的**导入可见性必须显式声明** — 传递 `import` **不会自动继承**可见性，在模块 A 中使用模块 B 的符号前必须直接 `import cnetmod.xxx` — 否则 Clang 报错 `declaration of 'X' must be imported from module 'Y' before it is required` — 参见 [module-conventions.md](infra/module-conventions.md)

## 我要做 X → 看哪个文件

### 基础设施

| 我想… | 看这个文件 |
|-------|-----------|
| 了解项目架构、目录结构、模块清单 | [architecture.md](infra/architecture.md) |
| 了解模块/文件命名约定、export 规则 | [module-conventions.md](infra/module-conventions.md) |
| 了解代码风格、clang-format、命名规范 | [code-style.md](infra/code-style.md) |
| 新增一个模块或协议 | [new-module-guide.md](infra/new-module-guide.md) |

### 核心网络

| 我想… | 看这个文件 |
|-------|-----------|
| 缓冲区、字节序、二进制读写 | [buffer.md](core/buffer.md) |
| TCP/UDP socket 连接、监听、收发 | [tcp-socket.md](core/tcp-socket.md) |
| SSL/TLS/DTLS 加密通信 | [ssl-tls.md](core/ssl-tls.md) |
| 异步 IO 操作（read/write/accept/connect） | [network-io.md](core/network-io.md) |
| 异步文件读写、send_file | [file-io.md](core/file-io.md) |
| 日志初始化、级别、文件输出 | [logging.md](core/logging.md) |
| 错误码、工具函数 | [utils-error.md](core/utils-error.md) |

### 协程与并发

| 我想… | 看这个文件 |
|-------|-----------|
| task/spawn/channel/mutex/semaphore/wait_group | [coroutine.md](coro/coroutine.md) |
| 定时器、超时、重试、断路器 | [timer-retry.md](coro/timer-retry.md) |
| 执行器、线程池、stdexec 桥接 | [executor-bridge.md](coro/executor-bridge.md) |

### HTTP

| 我想… | 看这个文件 |
|-------|-----------|
| HTTP 服务器（路由、SSE、Swagger） | [http-server.md](http/http-server.md) |
| HTTP 客户端（请求、响应、流式） | [http-client.md](http/http-client.md) |
| HTTP 中间件（认证、限流、CORS 等 17 个） | [http-middleware.md](http/http-middleware.md) |

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
