# cnetmod

基于 C++23 模块和原生协程的跨平台异步网络库。

[![Linux Build](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml)
[![macOS Build](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml)
[![Windows Build](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml)

[English](README.md) | 简体中文

## 平台支持

| 平台 | I/O 引擎 | 编译器 | 状态 |
|----------|-----------|----------|--------|
| Windows | IOCP | MSVC 2022 17.12+ | ✅ |
| Linux | io_uring + epoll | clang-21 + libc++ | ✅ |
| macOS | kqueue | clang-21 + libc++ | ✅ |

## 功能特性

### 核心运行时
- **协程引擎**: `task<T>`、`spawn()` 即发即弃、对称传输实现尾调用优化
- **I/O 上下文**: 平台原生事件循环（IOCP / io_uring / epoll / kqueue）+ 线程安全的 `post()`
- **多核支持**: `server_context` 包含专用接受线程 + N 个工作 `io_context` 线程 + stdexec 线程池
- **stdexec 集成**: `schedule()` sender、`async_scope`、`blocking_invoke()` 用于卸载阻塞调用

### 网络功能
- **TCP**: 异步 accept / connect / read / write，带 RAII socket 封装
- **UDP**: 异步 sendto / recvfrom
- **TLS/SSL**: 基于 OpenSSL 的 `ssl_context` / `ssl_stream`，支持异步握手、SNI、客户端证书
- **异步 DNS**: `async_resolve()` — 通过 stdexec 线程池 + `getaddrinfo` 实现非阻塞 DNS
- **串口**: 跨平台异步串口 I/O

### 协议支持
- **HTTP/1.1**: 完整服务器，包含路由器、中间件管道、分块传输、多部分上传
- **WebSocket**: 服务端从 HTTP 升级、帧编解码、ping/pong、per-message deflate
- **MQTT v3.1.1 / v5.0**: 完整 broker + 异步客户端 — QoS 0/1/2、保留消息、遗嘱、会话恢复、共享订阅、主题别名、自动重连；同步客户端封装
- **MySQL**: 异步客户端，支持预处理语句、连接池、管道、ORM（CRUD / 迁移 / 查询构建器）
- **Redis**: 异步客户端，支持 RESP 协议
- **OpenAI**: 异步 API 客户端（聊天补全等）

### 中间件（HTTP）
CORS、JWT 认证、速率限制、gzip 压缩、请求体大小限制、请求 ID、访问日志、指标、超时、优雅关闭、IP 防火墙、缓存、健康检查、文件上传、panic 恢复

### 同步原语
`mutex`、`shared_mutex`、`semaphore`、`condition_variable`（均支持协程）、`channel<T>`、`wait_group`、`cancel_token`

### 实用工具
- **定时器**: `async_sleep()`、`async_sleep_until()`、`with_timeout()` 用于可取消操作
- **缓冲区**: 字节序感知的读写器、缓冲池
- **日志**: 基于 `std::format` 的日志器（无外部依赖）
- **崩溃转储**: 平台原生 minidump（Windows）/ 信号处理器（Unix）
- **异步文件 I/O**: 基于 IOCP（Windows）

## 快速开始

### 构建要求

**CMake 4.0+** 是 C++23 模块支持所必需的。

**Windows**: Visual Studio 2022 17.12+ 并启用 C++23 模块。

**Linux**: clang-21 + libc++ + liburing-dev。
```bash
wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh
sudo ./llvm.sh 21 all
sudo apt install libc++-21-dev libc++abi-21-dev liburing-dev
```

**macOS**: Homebrew LLVM 21+（系统 clang 不支持 C++23 模块）。
```bash
brew install llvm ninja cmake
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"  # Apple Silicon
export PATH="/usr/local/opt/llvm/bin:$PATH"      # Intel Mac
```

### 克隆和构建

```bash
# 克隆仓库
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod

# 初始化子模块（第三方依赖所必需）
git submodule update --init --recursive

# 构建
cmake -B build -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

构建系统会自动检测 MSVC 和 libc++ 的标准库模块路径。如果检测失败，手动指定：
```bash
# Linux/macOS with clang
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1

# Windows MSVC
cmake -B build \
  -DLIBCXX_MODULE_DIRS="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/modules"
```

### 示例

**Echo 服务器**（TCP + 协程）：
```cpp
import cnetmod;
using namespace cnetmod;

task<void> handle_client(tcp_socket client) {
    char buf[1024];
    while (true) {
        int n = co_await client.async_recv(buf, sizeof(buf));
        if (n <= 0) break;
        co_await client.async_send(buf, n);
    }
}

task<void> server(io_context& ctx) {
    tcp_acceptor acceptor(ctx, 8080);
    while (true) {
        auto [client, addr] = co_await acceptor.async_accept();
        spawn(ctx, handle_client(std::move(client)));
    }
}

int main() {
    net_init guard;  // Windows 上的 WSAStartup RAII
    io_context ctx;
    spawn(ctx, server(ctx));
    ctx.run();
}
```

**MQTT Broker + 客户端**：
```cpp
import cnetmod.protocol.mqtt;

// 启动 broker
mqtt::broker brk(ctx);
brk.set_options({.port = 1883});
brk.listen();
spawn(ctx, brk.run());

// 异步客户端
mqtt::client cli(ctx);
co_await cli.connect({.host = "127.0.0.1", .port = 1883, .version = mqtt::protocol_version::v5});
co_await cli.subscribe("sensor/#", mqtt::qos::at_least_once);
cli.on_message([](const mqtt::publish_message& msg) {
    std::println("topic={} payload={}", msg.topic, msg.payload);
});
co_await cli.publish("sensor/temp", "22.5", mqtt::qos::exactly_once);
co_await cli.disconnect();
```

**定时器**：
```cpp
task<void> delayed_task(io_context& ctx) {
    co_await async_sleep(ctx, 1s);
    std::cout << "1 秒已过\n";
}
```

**Channel**（生产者-消费者）：
```cpp
channel<int> ch(10);
spawn(ctx, producer(ch));  // 发送 0..9
spawn(ctx, consumer(ch));  // 接收并打印

task<void> producer(channel<int>& ch) {
    for (int i = 0; i < 10; ++i) co_await ch.send(i);
    ch.close();
}
```

**MySQL ORM**（模型映射 + CRUD + 迁移）：
```cpp
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

// 定义模型
struct User {
    std::int64_t                id    = 0;
    std::string                 name;
    std::optional<std::string>  email;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id,    "id",    bigint,  PK | AUTO_INC),
    CNETMOD_FIELD(name,  "name",  varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE)
)

task<void> demo(mysql::client& cli) {
    orm::db_session db(cli);

    // DDL — create / drop / sync_schema（自动迁移）
    co_await db.create_table<User>();
    co_await orm::sync_schema<User>(cli);  // 检测差异并应用 ALTER TABLE

    // INSERT（自增 ID 自动填充）
    User u; u.name = "Alice"; u.email = "a@b.com";
    co_await db.insert(u);           // u.id 现在已设置
    co_await db.insert_many(batch);  // 批量插入

    // SELECT — find_all / find_by_id / 查询构建器
    auto all = co_await db.find_all<User>();
    auto one = co_await db.find_by_id<User>(param_value::from_int(1));
    auto top = co_await db.find(
        orm::select<User>()
            .where("`name` = {}", {param_value::from_string("Alice")})
            .order_by("`id` DESC")
            .limit(10).offset(0)
    );

    // UPDATE — 按主键
    u.name = "Bob";
    co_await db.update(u);

    // DELETE — 按模型 / 按 ID / 条件删除
    co_await db.remove(u);
    co_await db.remove_by_id<User>(param_value::from_int(1));
    co_await db.remove(orm::delete_of<User>()
        .where("`name` = {}", {param_value::from_string("test")}));
}
```

ID 策略 — 内置 UUID v4 和 Snowflake：
```cpp
struct Tag {
    orm::uuid    id;
    std::string  name;
};
CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id,   "id",   char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar)
)

struct Event {
    std::int64_t  id = 0;
    std::string   title;
};
CNETMOD_MODEL(Event, "events",
    CNETMOD_FIELD(id,    "id",    bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar)
)

// Snowflake 需要生成器
orm::snowflake_generator sf(/*machine_id=*/1);
orm::db_session db(cli, sf);
co_await db.insert(event);  // event.id 自动生成
```

查看 `examples/` 获取完整示例，包括 `http_demo`、`ws_demo`、`mqtt_demo`、`mysql_crud`、`mysql_orm`、`redis_client`、`multicore_http`、`ssl_echo_server` 等。

## 架构

**模块结构**: 纯 C++23 模块接口（`.cppm`），无头文件。平台特定实现在 `.cpp` 文件中通过 CMake 选择。

```
cnetmod.core          — socket、buffer、address、error、log、dns、ssl、serial_port
cnetmod.coro          — task、spawn、channel、mutex、semaphore、timer、cancel
cnetmod.io            — io_context + 平台后端（iocp、io_uring、epoll、kqueue）
cnetmod.executor      — async_op、server_context、scheduler、stdexec 桥接
cnetmod.protocol.tcp  — TCP acceptor/connector
cnetmod.protocol.udp  — UDP 异步 I/O
cnetmod.protocol.http — HTTP/1.1 服务器、路由器、中间件管道
cnetmod.protocol.websocket — WebSocket 服务器
cnetmod.protocol.mqtt — MQTT broker + 客户端（v3.1.1 / v5.0）
cnetmod.protocol.mysql — MySQL 异步客户端 + ORM
cnetmod.protocol.redis — Redis 异步客户端
cnetmod.protocol.openai — OpenAI API 客户端
cnetmod.middleware.*  — HTTP 中间件组件
```

**调度器/执行器**: `io_context` 提供 `post(coroutine_handle<>)` 用于线程安全的任务提交。平台特定的 `wake()` 实现：
- Windows: `PostQueuedCompletionStatus` + 哨兵键
- Linux: 非阻塞管道 + io_uring 读取
- macOS/epoll: eventfd/pipe 排空触发

**协程原语**: `task<T>` 使用对称传输实现尾调用优化。`spawn()` 通过 `detached_task` 将急切协程桥接到调度器。

**异步操作**: 基于 RAII 的 async_op 基类，带平台特定的 overlap/submission 跟踪。完成回调通过 `post()` 恢复等待的协程。

## 已知问题

### MSVC 错误 C1605: 对象文件大小超过 4 GB 限制

在 Debug 模式下使用 MSVC 构建时，编译器可能报错：

> **fatal error C1605**: 编译器限制: 对象文件大小不能超过 4 GB

这是因为 C++23 模块 + 大量模板实例化（特别是 `std` 模块 + `std::format` + 协议编解码器）在 Debug 构建中会产生极大的 `.obj` / `.ifc` 文件。MSVC 的 COFF 对象格式有 4 GB 硬限制。

**CMakeLists.txt 中已应用的解决方法：**
- `/bigobj` — 将节数限制从 65,536 提升到 40 亿
- `/Ob1` — 减少内联展开（减少每个 TU 中的代码重复）
- `/GL-` — 禁用全程序优化（防止合并对象膨胀）
- `/Z7` — 使用 C7 兼容调试格式而非程序数据库（`/Zi`）。将调试符号嵌入 `.obj` 文件而非单个巨大的 `.pdb`，绕过 4 GB 限制。

**如果仍然遇到 C1605：**
1. **拆分大型模块** — 将单体 `.cppm` 文件拆分为更小的分区。协议模块（`mqtt`、`http`、`mysql`）已使用分区接口（`:types`、`:codec`、`:parser` 等）。
2. **使用 Release/RelWithDebInfo** — 优化构建产生的对象文件要小得多。
3. **减少模板实例化** — 避免在单个 TU 中使用具有多种不同参数类型的 `std::format`。
4. **在 WSL 上使用 clang 构建** — clang + libc++ 使用 ELF 对象，没有 4 GB 限制。这是完整项目的推荐开发工作流。

### clang 模块依赖可见性

在模块之间添加新的 `import` 依赖时，clang 可能报错：

> declaration of 'X' must be imported from module 'Y' before it is required

确保显式导入导出所需符号的模块。与头文件不同，模块具有严格的可见性 — 传递导入不会自动可见。

## 设计理念

**为什么使用模块？** 零成本的无头文件构建模型。减少编译时间，更清晰的 API 表面。与 C++23 标准库方向一致。

**为什么使用协程？** 零开销的 async/await，无回调地狱。无栈协程编译为状态机，性能最优。

**为什么使用 io_uring/IOCP/kqueue？** 平台原生异步 I/O 提供最佳性能。io_uring 避免系统调用开销。IOCP 是 Windows 服务器的久经考验的选择。kqueue 是 macOS 的唯一选项。

**为什么使用 stdexec？** sender/receiver 的事实标准。支持与其他异步库组合。通过 `async_scope` 实现结构化并发。用于阻塞操作卸载（`blocking_invoke`）。

## 项目状态

这是一个探索现代 C++23 特性的学习项目。API 不稳定，不适合生产环境。使用风险自负。

## 许可证

MIT 许可证。详见 LICENSE 文件。
