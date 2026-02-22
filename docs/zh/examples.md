# 示例程序

本页面概述了 cnetmod 包含的所有示例程序。每个示例演示特定功能和最佳实践。

## 基础示例

### Echo 服务器 (`echo_server.cpp`)

**演示内容**：使用协程的基础 TCP 服务器

```cpp
// 接受连接并回显接收到的数据
task<void> handle_client(tcp_socket client);
task<void> server(io_context& ctx);
```

**关键概念**：
- TCP 套接字操作
- `task<T>` 协程
- `spawn()` 用于并发连接
- RAII 套接字管理

**运行**：
```bash
./build/examples/echo_server
# 连接方式：telnet localhost 8080
```

### 定时器演示 (`timer_demo.cpp`)

**演示内容**：异步定时器和超时

```cpp
// 休眠指定时长
co_await async_sleep(ctx, 2s);

// 为操作设置超时
auto result = co_await with_timeout(ctx, operation(), 5s);
```

**关键概念**：
- `async_sleep()` 和 `async_sleep_until()`
- `with_timeout()` 用于可取消操作
- 基于定时器的调度

**运行**：
```bash
./build/examples/timer_demo
```

## 同步示例

### 互斥锁演示 (`mutex_demo.cpp`)

**演示内容**：协程感知的同步

```cpp
// 独占锁
co_await mtx.lock();
// ... 临界区 ...
mtx.unlock();

// RAII 守卫
auto guard = co_await mtx.scoped_lock();
```

**关键概念**：
- `mutex` 用于独占访问
- `shared_mutex` 用于读写锁
- RAII 锁守卫
- 死锁避免

**运行**：
```bash
./build/examples/mutex_demo
```

### 通道演示 (`channel_demo.cpp`)

**演示内容**：Go 风格的通道通信

```cpp
channel<int> ch(10);  // 缓冲通道

// 生产者
co_await ch.send(42);

// 消费者
auto value = co_await ch.recv();
```

**关键概念**：
- `channel<T>` 用于生产者-消费者模式
- 缓冲 vs 无缓冲通道
- 通道关闭和迭代
- 多生产者、多消费者

**运行**：
```bash
./build/examples/channel_demo
```

## HTTP 示例

### HTTP 演示 (`http_demo.cpp`)

**演示内容**：功能完整的 HTTP 服务器

```cpp
http::server srv(ctx);

// 路由
srv.get("/", handler);
srv.post("/api/users", create_user);
srv.get("/api/users/:id", get_user);

// 中间件
srv.use(cors_middleware());
srv.use(jwt_auth_middleware());
```

**关键概念**：
- 带路径参数的 HTTP 路由
- 中间件管道
- JSON 请求/响应
- 查询参数和头部

**运行**：
```bash
./build/examples/http_demo
# 访问：http://localhost:8080
```

### 高性能 HTTP (`hight_http.cpp`)

**演示内容**：优化的 HTTP 服务器

**关键概念**：
- 零拷贝响应处理
- 连接池
- Keep-alive 优化
- 最小化分配

**运行**：
```bash
./build/examples/hight_http
```

### 多核 HTTP (`multicore_http.cpp`)

**演示内容**：多线程 HTTP 服务器

```cpp
server_context srv_ctx(4);  // 4 个工作线程

http::server srv(srv_ctx.get_io_context());
srv.listen(8080);

srv_ctx.run();  // 运行接受器 + 工作者
```

**关键概念**：
- `server_context` 用于多核
- 接受线程 + 工作线程
- 跨工作者的负载均衡
- 线程安全操作

**运行**：
```bash
./build/examples/multicore_http
# 基准测试：wrk -t4 -c100 -d10s http://localhost:8080/
```

## WebSocket 示例

### WebSocket 演示 (`ws_demo.cpp`)

**演示内容**：WebSocket 服务器

```cpp
ws::server srv(ctx);

srv.on_connect([](ws::connection& conn) {
    std::println("Client connected");
});

srv.on_message([](ws::connection& conn, string_view msg) {
    conn.send(msg);  // 回显
});
```

**关键概念**：
- 从 HTTP 升级到 WebSocket
- 帧编码/解码
- Ping/pong 心跳
- 二进制和文本消息

**运行**：
```bash
./build/examples/ws_demo
# 使用浏览器连接：ws://localhost:8080
```

### 多核 WebSocket (`multicore_ws.cpp`)

**演示内容**：可扩展的 WebSocket 服务器

**关键概念**：
- 多线程 WebSocket 处理
- 广播到所有连接
- 连接管理
- 内存高效的消息分发

**运行**：
```bash
./build/examples/multicore_ws
```

## MQTT 示例

### MQTT 演示 (`mqtt_demo.cpp`)

**演示内容**：MQTT 代理和客户端

```cpp
// 代理
mqtt::broker brk(ctx);
brk.listen(1883);

// 客户端
mqtt::client cli(ctx);
co_await cli.connect({.host = "127.0.0.1", .port = 1883});
co_await cli.subscribe("sensor/#", mqtt::qos::at_least_once);
co_await cli.publish("sensor/temp", "22.5", mqtt::qos::exactly_once);
```

**关键概念**：
- MQTT v3.1.1 和 v5.0 协议
- QoS 级别（0、1、2）
- 保留消息和遗嘱
- 主题通配符
- 会话持久化

**运行**：
```bash
# 终端 1：启动代理
./build/examples/mqtt_demo broker

# 终端 2：运行客户端
./build/examples/mqtt_demo client
```

## 数据库示例

### MySQL CRUD (`mysql_crud.cpp`)

**演示内容**：MySQL 异步客户端

```cpp
mysql::client cli(ctx);
co_await cli.connect({
    .host = "127.0.0.1",
    .user = "root",
    .password = "password",
    .database = "test"
});

// 执行查询
auto result = co_await cli.query("SELECT * FROM users");

// 预处理语句
auto stmt = co_await cli.prepare("INSERT INTO users (name) VALUES (?)");
co_await stmt.execute({param_value::from_string("Alice")});
```

**关键概念**：
- 异步查询执行
- 预处理语句
- 连接池
- 事务管理
- 结果集迭代

**运行**：
```bash
# 需要 MySQL 服务器运行
./build/examples/mysql_crud
```

### MySQL ORM (`mysql_orm.cpp`)

**演示内容**：对象关系映射

```cpp
struct User {
    int64_t id = 0;
    string name;
    optional<string> email;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE)
)

orm::db_session db(cli);

// CRUD 操作
co_await db.insert(user);
auto users = co_await db.find_all<User>();
co_await db.update(user);
co_await db.remove(user);

// 查询构建器
auto results = co_await db.find(
    orm::select<User>()
        .where("`name` = {}", {param_value::from_string("Alice")})
        .order_by("`id` DESC")
        .limit(10)
);
```

**关键概念**：
- 使用宏定义模型
- 自动迁移（`sync_schema`）
- CRUD 操作
- 查询构建器
- UUID 和雪花 ID 生成

**运行**：
```bash
./build/examples/mysql_orm
```

### Redis 客户端 (`redis_client.cpp`)

**演示内容**：Redis 异步操作

```cpp
redis::client cli(ctx);
co_await cli.connect("127.0.0.1", 6379);

// 字符串操作
co_await cli.set("key", "value");
auto value = co_await cli.get("key");

// 列表操作
co_await cli.lpush("mylist", "item1");
auto items = co_await cli.lrange("mylist", 0, -1);

// 哈希操作
co_await cli.hset("user:1", "name", "Alice");
auto name = co_await cli.hget("user:1", "name");
```

**关键概念**：
- RESP 协议
- 批量操作的管道
- 发布/订阅消息
- 连接池

**运行**：
```bash
# 需要 Redis 服务器运行
./build/examples/redis_client
```

## 高级示例

### SSL Echo 服务器 (`ssl_echo_server.cpp`)

**演示内容**：TLS/SSL 加密

```cpp
ssl_context ssl_ctx(ssl_context::tlsv13_server);
ssl_ctx.use_certificate_file("server.crt");
ssl_ctx.use_private_key_file("server.key");

ssl_stream stream(std::move(socket), ssl_ctx);
co_await stream.async_handshake(ssl_stream::server);

// 现在使用加密流
co_await stream.async_send(data);
```

**关键概念**：
- SSL/TLS 设置
- 证书管理
- 异步握手
- 加密 I/O

**运行**：
```bash
# 首先生成自签名证书
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365 -nodes

./build/examples/ssl_echo_server
# 连接方式：openssl s_client -connect localhost:8443
```

### 异步文件 I/O (`async_file.cpp`)

**演示内容**：非阻塞文件操作

```cpp
file f(ctx);
co_await f.open("data.txt", file::read_write | file::create);

// 读取
char buffer[1024];
auto n = co_await f.async_read(buffer, sizeof(buffer), 0);

// 写入
co_await f.async_write("Hello", 5, 0);
```

**关键概念**：
- IOCP 支持的文件 I/O（Windows）
- 线程池卸载（Linux/macOS）
- 分散-聚集 I/O
- 文件映射

**运行**：
```bash
./build/examples/async_file
```

### 串口 (`serial_port.cpp`)

**演示内容**：串行通信

```cpp
serial_port port(ctx, "COM3");  // 或 "/dev/ttyUSB0"
port.set_baud_rate(115200);
port.set_parity(serial_port::parity::none);

co_await port.async_write("AT\r\n");
auto response = co_await port.async_read();
```

**关键概念**：
- 跨平台串行 I/O
- 波特率和奇偶校验配置
- 异步读/写
- 超时处理

**运行**：
```bash
./build/examples/serial_port
```

### stdexec 桥接 (`stdexec_bridge.cpp`)

**演示内容**：与 stdexec 集成

```cpp
// 在 stdexec 线程池上调度工作
auto result = co_await blocking_invoke(ctx, [] {
    return expensive_computation();
});

// 使用 sender/receiver
auto sender = schedule(ctx) | then([] { return 42; });
auto value = co_await as_awaitable(sender);
```

**关键概念**：
- `blocking_invoke()` 用于 CPU 密集型工作
- Sender/receiver 组合
- `async_scope` 用于结构化并发
- 线程池集成

**运行**：
```bash
./build/examples/stdexec_bridge
```

### 阻塞桥接 (`blocking_bridge_demo.cpp`)

**演示内容**：从同步上下文调用异步代码

```cpp
// 同步函数调用异步代码
int sync_function() {
    io_context ctx;
    return blocking_invoke(ctx, async_operation());
}
```

**关键概念**：
- 同步和异步世界之间的桥接
- 临时事件循环
- 异常传播

**运行**：
```bash
./build/examples/blocking_bridge_demo
```

## 性能示例

### TechEmpower 基准测试 (`tfb_benchmark.cpp`)

**演示内容**：框架基准测试合规性

**端点**：
- `/json` - JSON 序列化
- `/db` - 单个数据库查询
- `/queries?queries=N` - 多个查询
- `/updates?queries=N` - 数据库更新
- `/plaintext` - 纯文本响应

**关键概念**：
- 最大性能优化
- 连接池
- 预处理语句
- 零拷贝响应

**运行**：
```bash
./build/examples/tfb_benchmark
# 基准测试：wrk -t4 -c256 -d30s http://localhost:8080/json
```

## 构建和运行示例

### 构建所有示例

```bash
cmake -B build -DCNETMOD_BUILD_EXAMPLES=ON
cmake --build build
```

### 构建特定示例

```bash
cmake --build build --target echo_server
```

### 带参数运行

```bash
# 在自定义端口上运行 HTTP 服务器
./build/examples/http_demo --port 9000

# 带详细日志的 MQTT 代理
./build/examples/mqtt_demo broker --verbose
```

## 示例源代码

所有示例源代码位于 [`examples/`](../examples/) 目录：

```
examples/
├── echo_server.cpp
├── timer_demo.cpp
├── mutex_demo.cpp
├── channel_demo.cpp
├── http_demo.cpp
├── hight_http.cpp
├── multicore_http.cpp
├── ws_demo.cpp
├── multicore_ws.cpp
├── mqtt_demo.cpp
├── mysql_crud.cpp
├── mysql_orm.cpp
├── redis_client.cpp
├── ssl_echo_server.cpp
├── async_file.cpp
├── serial_port.cpp
├── stdexec_bridge.cpp
├── blocking_bridge_demo.cpp
└── tfb_benchmark.cpp
```

## 下一步

- **[快速入门指南](getting-started.md)** - 学习基础知识
- **[核心概念](core/coroutines.md)** - 理解协程
- **[协议指南](protocols/http.md)** - 深入了解协议
- **[API 参考](api/core.md)** - 详细的 API 文档
