# 项目架构

> cnetmod v2.0.0 — 基于 C++23 Modules 的跨平台异步网络库。

## 项目概述

| 属性 | 值 |
|------|------|
| 名称 | cnetmod |
| 版本 | 2.0.0（`CNETMOD_VERSION_STRING "2.0.0"`） |
| 语言标准 | C++23（`CMAKE_CXX_STANDARD 23`） |
| 构建系统 | CMake 3.28+，`CMAKE_CXX_SCAN_FOR_MODULES ON` |
| 库类型 | 分层静态组件；`cnetmod::core` / `cnetmod::all` 是兼容聚合入口 |
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
│   ├── utils/              # 通用工具与安全能力（security/JWT）
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
│   ├── json/               # Glaze-only JSON module facade
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

## 二进制组件边界

构建不再把所有实现塞进一个巨大归档。按职责生成并导出以下 CMake target：

- `cnetmod::runtime`：core、coro、io、executor、JSON、安全与基础观测能力。
- `cnetmod::orm`：通用 ORM；仅在 `CNETMOD_ENABLE_ORM=ON` 时存在。
- `cnetmod::<protocol>`：每个已启用协议各自一个静态组件，例如
  `cnetmod::http`、`cnetmod::redis`、`cnetmod::mysql`。
- `cnetmod::application`：Application 组合与自动配置层。
- `cnetmod::http_redis_cache`：Redis 驱动的 HTTP 缓存中间件适配器；仅在
  HTTP 与 Redis 同时启用时存在。
- `cnetmod::core` / `cnetmod::all`：兼容旧工程的一次性聚合入口，自身只含
  极小 facade，不再复制组件实现。

应用应链接实际使用的最小组件。特别是 Redis 协议并不是 HTTP 的隐式依赖，
HTTP 的内存缓存也不需要 Redis。只有明确使用 Redis 缓存后端时才链接：

```cmake
target_link_libraries(my_service PRIVATE cnetmod::http_redis_cache)
```

```cpp
import cnetmod.integration.http.redis_cache;
```

`CNETMOD_USE_SYSTEM_DEPS=OFF` 表示严格使用仓库内置依赖；可选压缩库在仓库
没有对应源码时保持关闭，不得从 Conda、系统目录或包管理器静默捡取。

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
-DCNETMOD_ENABLE_ALL_PROTOCOLS=ON|OFF   # 各协议开关的默认值，默认 OFF
-DCNETMOD_ENABLE_ORM=ON|OFF             # SQL ORM 和 XML mapper 支持
```

仓库默认配置只构建核心运行时和 ORM，不隐式启用全部协议，也不构建测试、
benchmark、示例、C API 或 Python Binding。应用必须按实际需求显式开启协议；框架自身的
完整验证构建则显式传入 `CNETMOD_ENABLE_ALL_PROTOCOLS=ON` 和对应的构建开关。
这条边界避免普通 SDK 构建无意间生成数百个 IFC/OBJ，并避免 MSVC 编译仅供框架
维护者使用的重模板测试。

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
    -DCNETMOD_ENABLE_HTTP=ON \
    -DCNETMOD_ENABLE_OPENAI=ON

# 构建
cmake --build build

# 运行测试
ctest --test-dir build

# 安装
cmake --install build --prefix install
```

需要维护者完整验证时，必须显式配置：

```bash
cmake -B build-full -G Ninja \
    -DCNETMOD_ENABLE_ALL_PROTOCOLS=ON \
    -DCNETMOD_BUILD_TESTS=ON \
    -DCNETMOD_BUILD_EXAMPLES=ON \
    -DCNETMOD_BUILD_BENCH=ON
cmake --build build-full --target cnetmod_build_all
```

MSVC 构建使用 `rebuild_install.bat` 脚本。

## 第三方依赖

| 依赖 | 目录 | 用途 |
|------|------|------|
| Glaze | `3rdparty/glaze` | `cnetmod.json` private parsing backend |
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
