# cnetmod

基于 C++23 模块和原生协程的跨平台异步网络库。

[![Linux Build](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/linux-clang.yml)
[![macOS Build](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/macos-clang.yml)
[![Windows Build](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml/badge.svg)](https://github.com/banderzhm/cnetmod/actions/workflows/windows-msvc.yml)

[English](README.md) | 简体中文

## 平台支持

| 平台 | I/O 引擎 | 编译器 | 状态 |
|----------|-----------|----------|--------|
| Windows | IOCP | 最新 Visual Studio 2026 (MSVC) | ✅ |
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
- **HTTP/1.1 & HTTP/2**: 完整服务器，包含路由器、中间件管道、分块传输、多部分上传；HTTP/2 通过 TLS + ALPN 协商，支持多路复用流
- **WebSocket**: 服务端从 HTTP 升级、帧编解码、ping/pong、per-message deflate
- **SOCKS5**: 代理协议客户端和服务端 — CONNECT、BIND、UDP ASSOCIATE 命令；认证方法（无认证、用户名/密码）；支持 IPv4、IPv6 和域名
- **MQTT v3.1.1 / v5.0**: 完整 broker + 异步客户端 — QoS 0/1/2、保留消息、遗嘱、会话恢复、共享订阅、主题别名、自动重连；同步客户端封装
- **MySQL**: 异步客户端，支持预处理语句、连接池、管道、事务管理、ORM（CRUD / 迁移 / 查询构建器 / MyBatis-Plus 风格 XML 映射器 / BaseMapper / 分页 / 软删除 / 乐观锁 / 多租户 / 缓存）
- **Redis**: 异步客户端，支持 RESP 协议、连接池
- **Raft**: 复制状态机工具集，支持 leader 选举、日志复制、ReadIndex、leader lease / check-quorum、joint consensus 成员变更、snapshot install / compaction、LevelDB 持久化、TCP transport、TLS / mTLS 认证、传输指标、chaos / 重启恢复测试，以及分布式存储示例
- **Modbus**: 完整协议实现 — TCP/UDP/RTU（串口）客户端和服务端、所有标准功能码、连接池、CRC-16 校验、帧时序控制、数据存储（基于互斥锁和无锁通道）
- **CoAP**: 面向受限设备的 CoAP 协议栈 — RFC 7252 消息/选项编解码、confirmable 请求重传、Observe 订阅/通知、token/message-id 匹配、资源路由、Block1/Block2 辅助函数、CoAPS/DTLS context 支持
- **OpenAI**: 异步 API 客户端（聊天补全等）

### Raft 性能
Intel Core i9-14900K 上的 Release benchmark 结果：

| 场景 | 吞吐 |
|----------|------------|
| 单节点 append + commit | ~5.17M - 5.50M ops/s |
| 5 节点多数派复制 | ~0.80M - 0.83M ops/s |

可通过 `testing/bench/bench_raft.cpp` 在本地复测。实际结果会受编译器、allocator、存储后端、CPU 频率策略、网络 / loopback 环境影响。

完整说明见：[Raft](docs/zh/protocols/raft.md)。宿主项目存在相同第三方库时的接入方式见：[第三方依赖集成](docs/zh/advanced/thirdparty-dependency-integration.md)。

### HTTP / gRPC 性能
Windows Release benchmark，硬件为 Intel Core i9-14900K，Visual Studio 2026，IOCP，本机 loopback，多核模式（`mc:16/16`）：

| Benchmark | 命令 | 吞吐 |
|----------|---------|------------|
| HTTP/1.1 cleartext | `bench_http.exe 1000 16` | ~117.69K req/s |
| HTTP/2 h2c | `bench_http.exe 1000 16` | ~100.66K req/s |
| HTTPS/1.1 | `bench_http.exe 1000 16` | ~41.54K req/s |
| HTTPS/2 | `bench_http.exe 1000 16` | ~41.24K req/s |
| WebSocket echo | `bench_ws.exe 1000 16` | ~290.00K msg/s |
| WebSocket Secure echo | `bench_ws.exe 1000 16` | ~73.52K msg/s |
| gRPC unary over HTTP/2 h2c | `bench_grpc.exe 5000 16` | ~112.92K req/s |

gRPC 正确性测试包含 Python `grpcio` 跨进程双向互操作测试。以上为本机 loopback 结果，实际性能会受 CPU 电源策略、TLS 库、worker 数和系统并发负载影响。

### MQTT 性能
Windows Release benchmark，硬件为 Intel Core i9-14900K，Visual Studio 2026，IOCP，本机 loopback，4 个 broker worker，8 个 publisher，QoS 0，`write_batch=16`：

| Benchmark | 命令 | 结果 |
|----------|---------|--------|
| MQTT QoS0 broker/client burst | `bench_mqtt.exe 20000 8 clientburst multi` | 平均约 128.18K msg/s，最高约 133.78K msg/s |

连续 5 轮均为 `160000 ok, 0 failed`。Broker 指标每轮均达到 `routed=160000`、`delivered=160000`。

### 中间件（HTTP）
CORS、JWT 认证、速率限制、gzip 压缩、请求体大小限制、请求 ID、访问日志、指标、超时、优雅关闭、IP 防火墙、缓存、健康检查、文件上传、panic 恢复

### 同步原语
`mutex`、`shared_mutex`、`semaphore`、`condition_variable`（均支持协程）、`channel<T>`、`wait_group`、`cancel_token`

### 实用工具
- **定时器**: `async_sleep()` / `async_sleep_until()` 为便捷封装，`with_timeout()` 用于带 `cancel_token` 的 `task<std::expected<...>>` 操作
- **缓冲区**: 字节序感知的读写器、缓冲池
- **日志**: 基于 `std::format` 的日志器（无外部依赖）
- **崩溃转储**: 平台原生 minidump（Windows）/ 信号处理器（Unix）
- **异步文件 I/O**: 基于 IOCP（Windows）

## 快速开始

### 构建要求

**CMake 4.0+** 是 C++23 模块支持所必需的。

**Windows**: 最新 Visual Studio 2026，并启用 C++23 模块。

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

支持三种常用构建方式：

- **子模块/本地构建**：适合开发本仓库，使用 `3rdparty` 里的 Git submodule。
- **vcpkg manifest 构建**：适合让 vcpkg 统一安装依赖，Windows + VS 2026 推荐使用仓库自带的 `x64-windows-vs2026` overlay triplet。
- **Conan 构建/打包**：适合通过 Conan 分发和复用，`conan create` 可验证 recipe 的导出、隔离构建和打包流程。

```bash
# 克隆仓库
git clone https://github.com/banderzhm/cnetmod.git
cd cnetmod

# 初始化子模块（第三方依赖所必需）
git submodule update --init --recursive

# 构建
cmake -B build -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build

# 显式构建全部 cnetmod 目标
cmake --build build --target cnetmod_build_all

# Visual Studio 生成器构建 C++ modules 时建议使用单节点 MSBuild
cmake --build build --target cnetmod_build_all --config Debug
```

### 使用 vcpkg 构建

仓库已包含 `vcpkg.json`，安装了 vcpkg 的用户可以直接使用 manifest
mode 安装受支持的第三方依赖：

```bash
cmake -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build-vcpkg --target cnetmod_build_all
```

Windows 上如果同时安装了多个 Visual Studio，可以用仓库自带的 overlay
triplet 强制使用 Visual Studio 2026：

```bat
set VCPKG_ROOT=<path-to-vcpkg>
set VCPKG_VISUAL_STUDIO_PATH=<path-to-Visual-Studio-2026>

:: 可选：C 盘空间不足时，把 vcpkg 缓存放到其它盘
set X_VCPKG_REGISTRIES_CACHE=%USERPROFILE%\.cache\vcpkg\registries
set VCPKG_DOWNLOADS=%USERPROFILE%\.cache\vcpkg\downloads

%VCPKG_ROOT%\vcpkg.exe install --triplet x64-windows-vs2026 ^
  --overlay-triplets=cmake\vcpkg-triplets

cmake -S . -B build-vcpkg-vs2026 -G"Visual Studio 18 2026" ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-vs2026 ^
  -DVCPKG_OVERLAY_TRIPLETS=cmake/vcpkg-triplets

cmake --build build-vcpkg-vs2026 --config Release --target cnetmod_build_all
```

cnetmod 会先复用当前 toolchain 或宿主项目暴露的依赖，再回退到已有的
`3rdparty` 副本。`pugixml` 在本仓库里保持为正常 Git submodule；包管理器
构建应优先使用 package target，只在没有系统包时回退到该 submodule。

### 使用 Conan 构建

仓库也包含 Conan 2 recipe：

```bash
conan install . --output-folder=build-conan --build=missing \
  -s build_type=Release -s compiler.cppstd=23
cmake --preset conan-default
cmake --build --preset conan-release --target cnetmod_core
```

Visual Studio 2026 需要 Conan 2.30+ 和 CMake 4.2+，这样才能识别 MSVC
195 和 `Visual Studio 18 2026` 生成器：

```bat
:: 可选：C 盘空间不足时，把 Conan cache 和临时目录放到其它盘
set CONAN_HOME=%USERPROFILE%\.conan2-vs2026
set TEMP=%USERPROFILE%\.cache\build-tmp
set TMP=%USERPROFILE%\.cache\build-tmp

conan --version
conan install . --output-folder=build-conan-vs2026 --build=missing ^
  -s build_type=Release ^
  -s compiler=msvc -s compiler.version=195 ^
  -s compiler.runtime=dynamic -s compiler.runtime_type=Release ^
  -s compiler.cppstd=23 ^
  -c tools.cmake.cmaketoolchain:generator="Visual Studio 18 2026"

cmake --preset conan-default
cmake --build --preset conan-release --target cnetmod_core

:: 可选：验证 recipe 的导出、隔离构建和打包流程
conan create . --build=missing -pr:h vs2026 -pr:b vs2026
```

默认 Conan recipe 会从 ConanCenter 安装 `jwt-cpp`、`nlohmann_json`、
`pugixml`、`libnghttp2`、`leveldb`、`openssl` 和 `zlib`。`mimalloc`
默认启用；如需关闭可传 `-o cnetmod/*:with_mimalloc=False`。`stdexec`
默认使用 `3rdparty/stdexec`；如果你的 Conan remote 提供上游 `p2300` 包，可以开启
`-o cnetmod/*:with_stdexec_package=True`。

构建系统会自动检测 MSVC 和 libc++ 的标准库模块路径。Windows 安装最新 Visual Studio 2026 后直接使用默认自动检测路径即可。Linux/macOS 如果检测失败，可手动指定：
```bash
# Linux/macOS with clang
cmake -B build \
  -DLIBCXX_MODULE_DIRS=/usr/lib/llvm-21/share/libc++/v1 \
  -DLIBCXX_INCLUDE_DIRS=/usr/lib/llvm-21/include/c++/v1
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

**SOCKS5 代理**：
```cpp
import cnetmod.protocol.socks5;

// SOCKS5 服务端
socks5::server_config config;
config.allow_no_auth = true;
config.allow_username_password = true;
config.add_user("user", "pass");

socks5::server proxy(ctx, config);
co_await proxy.listen(1080);
spawn(ctx, proxy.run());

// SOCKS5 客户端
socks5::client client(ctx);
co_await client.connect("127.0.0.1", 1080);
co_await client.authenticate();  // 无认证或用户名/密码
co_await client.connect_to("example.com", 80);

// 现在使用隧道连接
co_await client.send("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
auto response = co_await client.recv(4096);
```

**Modbus 协议**：
```cpp
import cnetmod.protocol.modbus;

// TCP 客户端
modbus::tcp_client client(ctx);
co_await client.connect("192.168.1.100", 502);

auto req = modbus::request_builder()
    .unit_id(1)
    .read_holding_registers(0, 10)
    .build();

auto resp = co_await client.execute(req);

// RTU 服务端（串口）
modbus::memory_data_store store;
modbus::rtu_server_config config;
config.port_name = "COM3";  // Windows 或 "/dev/ttyUSB0" (Linux)
config.baudrate = 9600;
config.unit_id = 1;

modbus::rtu_server server(ctx, store);
co_await server.start(config);
```

**CoAP over UDP**：
```cpp
import cnetmod.protocol.coap;

using namespace cnetmod;

coap::udp_server server(ctx);
auto listen = server.listen("0.0.0.0", coap::default_port);
if (!listen) {
    throw std::system_error(listen.error());
}

server.route(coap::method::get, "/sensors/temp",
    [](const coap::inbound_request& req, const endpoint&) -> task<coap::message> {
        auto resp = coap::make_response(req.request, coap::response_code::content);
        resp.add_uint_option(coap::option_number::content_format,
            static_cast<std::uint16_t>(coap::content_format::text_plain));
        std::string body = "22.5";
        resp.payload.assign(reinterpret_cast<const std::byte*>(body.data()),
            reinterpret_cast<const std::byte*>(body.data() + body.size()));
        co_return resp;
    });
spawn(ctx, server.run());

coap::udp_client client(ctx);
auto remote = co_await client.resolve_endpoint("127.0.0.1");
if (remote) {
    auto response = co_await client.get(*remote, "/sensors/temp");
}
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

**MySQL ORM**（模型映射 + CRUD + 迁移 + 高级特性）：
```cpp
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

// 定义带高级特性的模型
struct User {
    std::int64_t                id         = 0;
    std::string                 name;
    std::optional<std::string>  email;
    std::int32_t                version    = 0;  // 乐观锁
    std::int32_t                deleted    = 0;  // 软删除
    std::time_t                 created_at = 0;  // 插入时自动填充
    std::time_t                 updated_at = 0;  // 插入/更新时自动填充
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id,         "id",         bigint,    PK | AUTO_INC),
    CNETMOD_FIELD(name,       "name",       varchar),
    CNETMOD_FIELD(email,      "email",      varchar,   NULLABLE),
    CNETMOD_FIELD(version,    "version",    int_,      VERSION),
    CNETMOD_FIELD(deleted,    "deleted",    tinyint,   LOGIC_DELETE),
    CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT),
    CNETMOD_FIELD(updated_at, "updated_at", timestamp, FILL_INSERT_UPDATE)
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

// BaseMapper — 通用 CRUD 接口
task<void> base_mapper_demo(mysql::client& cli) {
    base_mapper<User> mapper(cli);
    
    // QueryWrapper — 流式查询构建器
    query_wrapper<User> wrapper;
    wrapper.eq("status", 1)
           .like("name", "%Alice%")
           .gt("age", 18)
           .order_by_desc("created_at")
           .limit(10);
    
    auto users = co_await mapper.select_list(wrapper);
    
    // 分页
    auto page = co_await mapper.select_page(1, 10, wrapper);
    std::println("第 {}/{} 页，总数: {}", 
        page.current_page, page.total_pages, page.total);
}

// MyBatis-Plus 风格 XML 映射器 — 动态 SQL
task<void> xml_mapper_demo(mysql::client& cli) {
    mapper_registry registry;
    registry.load_file("mappers/user_mapper.xml");
    
    mapper_session session(cli, registry);
    
    // 执行带动态条件的查询
    auto result = co_await session.query<User>("UserMapper.findByCondition",
        param_context::from_map({
            {"name", param_value::from_string("Alice")},
            {"status", param_value::from_int(1)}
        }));
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

查看 `examples/` 获取完整示例，包括 `http_demo`、`http2_demo`、`ws_demo`、`mqtt_demo`、`mysql_crud`、`mysql_orm`、`redis_client`、`redis_cluster`、`oss_shared_storage`、`modbus_demo`、`multicore_http`、`ssl_echo_server` 等。

## 架构

**模块结构**: 纯 C++23 模块接口（`.cppm`），无头文件。平台特定实现在 `.cpp` 文件中通过 CMake 选择。

```
cnetmod.core          — socket、buffer、address、error、log、dns、ssl、serial_port
cnetmod.coro          — task、spawn、channel、mutex、semaphore、timer、cancel
cnetmod.io            — io_context + 平台后端（iocp、io_uring、epoll、kqueue）
cnetmod.executor      — async_op、server_context、scheduler、stdexec 桥接
cnetmod.protocol.tcp  — TCP acceptor/connector
cnetmod.protocol.udp  — UDP 异步 I/O
cnetmod.protocol.http — HTTP/1.1 + HTTP/2 服务器、路由器、中间件管道、ALPN 协商
cnetmod.protocol.websocket — WebSocket 服务器
cnetmod.protocol.socks5 — SOCKS5 代理客户端 + 服务端
cnetmod.protocol.mqtt — MQTT broker + 客户端（v3.1.1 / v5.0）
cnetmod.protocol.mysql — MySQL 异步客户端 + ORM
cnetmod.protocol.redis — Redis 异步客户端
cnetmod.protocol.raft — Raft 复制状态机、存储、传输、运行时、成员变更、快照
cnetmod.protocol.modbus — Modbus TCP/UDP/RTU 客户端 + 服务端
cnetmod.protocol.coap — CoAP UDP 客户端 + 服务端、数据报编解码、资源路由
cnetmod.protocol.openai — OpenAI API 客户端
cnetmod.protocol.http.middleware.*  — HTTP 中间件组件
cnetmod.utils         — 协议转换工具（字节序、CRC、十六进制、寄存器转换）
```

**调度器/执行器**: `io_context` 提供 `post(coroutine_handle<>)` 用于线程安全的任务提交。平台特定的 `wake()` 实现：
- Windows: `PostQueuedCompletionStatus` + 哨兵键
- Linux: 非阻塞管道 + io_uring 读取
- macOS/epoll: eventfd/pipe 排空触发

**协程原语**: `task<T>` 使用对称传输实现尾调用优化。`spawn()` 通过 `detached_task` 将急切协程桥接到调度器。

**异步操作**: 基于 RAII 的 async_op 基类，带平台特定的 overlap/submission 跟踪。完成回调通过 `post()` 恢复等待的协程。

## 已知问题

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

cnetmod 是一个展示 C++23 模块和协程强大能力的现代网络库。它提供了 HTTP/1.1 & HTTP/2、MQTT、MySQL、WebSocket、Modbus、CoAP 等协议的生产级实现，全部基于零开销的 async/await 构建。

该库证明了 C++23 模块已经可以用于实际项目，并在 Linux、macOS 和 Windows 上提供完整的跨平台支持。

## 许可证

MIT 许可证。详见 LICENSE 文件。
