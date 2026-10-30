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

> 异步 MongoDB C++ 客户端，包含 SRV/TXT 发现、Snappy/zlib/zstd、SCRAM-SHA-256、AWS、OIDC 机器身份、X.509、TLS、连接池、事务和游标流。

**import**: `import cnetmod.protocol.mongodb;`
**CMake**: `-DCNETMOD_ENABLE_MONGODB=ON`
**源码**: `src/protocol/mongodb/`

## 场景导航

| 场景 | 推荐入口 |
|------|----------|
| Application 用户入口 | `mongodb_service` URI 配置、`command` / `acquire`，见 [Application](../infra/application.md) |
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
对于 exhaust cursor 等连续响应场景，先用 find/aggregate 获得 cursor id，再将 getMore 传给 `command_stream()`：

```cpp
auto streamed = co_await conn.command_stream("analytics",
    bson_document{{"getMore", cursor_id}, {"collection", "events"}},
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

## Wire 编码与边界校验

- `decode_command_message()` 拒绝未知的低 16 位必需标志，忽略高 16 位未知可选标志的语义，并在 `decoded_message::flags` 保留原始值。
- `encode_command_message()` 默认不生成校验和；显式传入 `op_message_checksum_present` 时追加覆盖消息头和正文的 CRC-32C，并将校验和的 4 字节计入消息长度和 `max_message_bytes`。
- `op_message_checksum_present | op_message_exhaust_allowed` 可组合使用。此编码入口仍拒绝无应答的 `moreToCome` 请求；`connection::command()` 的一问一答行为不变。
- `encode_compressed_message()` 检查原始消息声明长度与实际字节数一致，并拒绝嵌套 `OP_COMPRESSED`。
- `decode_compressed_message()` 在分配解压缓冲前检查重建消息是否超出 int32 长度及配置上限；zlib 解码还要求完整消费输入，拒绝尾随垃圾。
- 本节接口是低层 wire API；普通连接命令不会自动开启请求校验和。

```cpp
bson_document ping{{"ping", bson_value{std::int32_t{1}}}};
auto wire = encode_command_message(7, ping, 1024,
    op_message_checksum_present);
if (!wire)
    co_return std::unexpected(wire.error());
```

协议定义参见 [MongoDB Wire Protocol](https://www.mongodb.com/docs/manual/reference/mongodb-wire-protocol/)。

## 包职责与文件命名

MongoDB 保留单一公开入口 `import cnetmod.protocol.mongodb;`，内部使用职责明确的分区和文件。禁止新增 `types.cpp` / `types.cppm` 或以 `_types` 结尾的笼统文件，也不要用 `common`、`utils`、`helpers`、`base` 命名聚合实现。

| 包目录 | 责任 | 可依赖的 MongoDB 包 |
|---|---|---|
| `endpoint/` | 服务器地址与端口契约 | error |
| `discovery/` | SRV/TXT、IDNA 和系统 DNS 适配 | endpoint、error |
| `configuration/` | URI 与选项、发现结果的组合 | topology、discovery、endpoint、transport、auth、wire、error |
| `bson/` | BSON 值与编解码 | error |
| `wire/` | OP_MSG、CRC-32C、文档序列、压缩 | bson、error |
| `auth/` | SCRAM/AWS/OIDC 凭据、签名和 SASL 契约 | bson、error |
| `transport/` | socket/TLS、握手、命令超时与取消 | wire、bson、auth、error |
| `pool/` | 单节点租约、排队、维护与关闭 | transport、error |
| `topology/` | 节点描述、发现、选择与多节点池 | discovery、endpoint、pool、transport、auth 契约、bson、error |
| `session/` | 会话、事务、重试策略 | topology、pool、transport、bson、error |
| `cursor/` | find/aggregate 游标与 change stream | session、pool、transport、bson、error |

底层包不得反向导入上层包；所有协议包不得导入 Application、HTTP 或其他数据库协议。
`python3 testing/tests/test_mongodb_architecture.py` 检查分区归属、依赖方向和文件命名；测试配置中同样注册此门禁。

## 批量文档与普通游标

`document_sequence` 表示 OP_MSG Kind 1 文档序列。每个 BSON 文档单独受 `maxBsonObjectSize` 限制，整个报文受 `maxMessageSizeBytes` 限制；不能把整批文档错误地限制为一个 BSON 对象大小。解码支持正文前后的序列，将其按标识符路径合并成正文数组，拒绝重复正文、路径冲突、重复标识符和截断的序列。

```cpp
auto inserted = co_await conn.command("analytics", bson_document{{"insert", "events"}},
    std::vector<document_sequence>{{"documents", {
        bson_document{{"n", std::int32_t{1}}},
        bson_document{{"n", std::int32_t{2}}}}}});

cursor rows(pool, "analytics", "events", {.batch_size = 100});
auto opened = co_await rows.find(bson_document{});
if (!opened)
    co_return std::unexpected(opened.error());
while (true)
{
    auto item = co_await rows.next();
    if (!item)
        co_return std::unexpected(item.error());
    if (!*item)
        break;
    // 处理 **item。
}
auto closed = co_await rows.close();
```

`cursor::aggregate(std::vector<bson_document>)` 打开聚合游标。
`try_next()` 最多发一次 getMore；空的活跃批次返回空 optional，调用者用 `exhausted()` 区分真正耗尽。
`next()` 会继续跨越空批次。游标独占租约直到服务器 id 为零或显式 `close()`；同一对象的并发协议操作返回错误。
必须等待 `close()` 才能保证及时向服务器发送 killCursors；析构不会投递脱离监管的清理协程。

## 压缩、认证与拓扑正确性

- `connection_options::enable_zstd_compression` 默认 false；启用且构建包含 zstd 时优先协商 zstd，其次选择已启用的 zlib。只向服务器声明实际编译进来的 codec。
- MongoDB 组件直接链接自身所需的 zlib、可选 zstd 和 ICU；不能依靠 HTTP、Kafka 或 PostgreSQL 间接提供这些依赖。
- SCRAM-SHA-256 使用 ICU 完成 RFC 4013 SASLprep，拒绝非法 UTF-8、禁止字符、重复 SCRAM 属性、未知必需扩展和非法 Base64。
- 最大复制延迟根据 `lastWrite.lastWriteDate`、监控更新时间差与心跳间隔计算；心跳新鲜不代表复制已经追平。
- 标签集按先后次序匹配；secondaryPreferred 在从节点被标签/延迟过滤后回退主节点，primaryPreferred 有主节点时不套用从节点筛选。
- 监控连接不执行用户认证，RTT 测量排除建连与认证；监控任务等待心跳时可被 stop token 唤醒。
- 重试策略区分普通读写、事务提交与变更流；事务瞬态错误不能盲目重放单条命令，变更流历史丢失不能当作可恢复错误无限重试。
- 变更流只在事件实际消费后推进事件 token，批次消费完后使用 postBatchResumeToken；缺少事件 token 会关闭流并报错。

## MongoDB 本地验证

仅启用 `CNETMOD_ENABLE_MONGODB=ON` 并关闭其余协议和 ORM，可以单独编译 MongoDB 测试。
除纯协议、拓扑、认证、生命周期测试外，`test_mongodb_integration` 连接可丢弃的本地副本集：

```bash
CNETMOD_MONGODB_TEST_PORT=27017 ctest --test-dir <build> -R '^test_mongodb_' --output-on-failure
# TLS 测试服务另行配置本地 CA；不关闭证书验证。
CNETMOD_MONGODB_TEST_PORT=28719 CNETMOD_MONGODB_TEST_CA=/absolute/path/server-cert.pem \
  ctest --test-dir <build> -R '^test_mongodb_' --output-on-failure
```

未配置端口时，真实服务测试以 77 明确跳过；不把跳过记成真实服务验证成功。
测试使用唯一的 `cnetmod_native_*` 数据库和临时用户，并在结束后清理。副本集须允许本机测试连接创建临时用户。
SRV/TXT、Snappy、AWS、OIDC 机器身份与 X.509 均有专门实现和测试；真实 AWS IAM/Atlas/Azure/GCP 账号授权需要独立的部署凭据，模拟 TLS 对话测试不能替代真实账号验收。OIDC 人工交互登录是规范可选功能，本模块公开的是机器身份回调；完整负载均衡部署认证矩阵仍需单独验收。

本地测试快捷入口：`bash tools/test-mongodb-local.sh [CMake构建目录]`，默认 `build/mongodb-only` 和端口 27017。该入口显式启用真实服务测试，不把未配置端口导致的跳过混入本地联调结果。
macOS 可用官方 Homebrew 包 `mongodb/brew/mongodb-community@8.0` 管理服务；在 `mongod.conf` 配置 `replication.replSetName: cnetmod-test`，初始化单节点副本集后可测试事务与变更流。仅绑定回环地址的开发配置不应直接用于远程部署。

空事务的 commit/abort 不发送网络命令；变更流的恢复重试预算只计算恢复尝试，不因正常空批次而减少。

## SRV/TXT 与连接字符串

`parse_connection_string(uri)` 解析 `mongodb://` 和 `mongodb+srv://`；`resolve_connection_string(io, workers, uri, lookup = {})` 异步完成 SRV/TXT 解析，再返回 `connection_configuration`。其中 `topology` 可直接构造 `topology_connection_pool`，`selection` 是 URI 的读取偏好。

- `endpoint/` 拥有服务器地址契约；`discovery/` 拥有 SRV 校验和系统 DNS 适配；`configuration/` 拥有 URI 和选项映射。拓扑包不反向依赖 URI 配置包。
- 系统解析器使用操作系统 SRV/TXT API，并通过 `blocking_invoke` 在工作池执行。Windows 链接 dnsapi，Unix 链接 resolv；核心 MongoDB 不依赖 HTTP/DNS 高层协议模块。
- SRV URI 禁止端口、多主机和 IP 字面量，默认开启 TLS；只允许 URI 的显式 `tls=false` 覆盖，TXT 不能关闭 TLS。
- 每个 DNS 目标都先规范化为小写 IDNA A-label，检查父域和端口，再考虑 `srvMaxHosts` 采样。不能通过采样跳过恶意目标校验。
- TXT 的多段字符字符串按顺序拼接；多个 TXT RR 报错。只接受 authSource、replicaSet、loadBalanced 默认值，显式 URI 选项覆盖 TXT。
- `srvMaxHosts` 与 replicaSet/loadBalanced 冲突会报错。SRV 轮询只用于 Unknown/Sharded 拓扑，遵守至少 60 秒的 TTL；查询失败保留现有拓扑。
- 节点移除会关闭该节点连接池；等待中的命令/借用持有独立 shared ownership，不能因 DNS 更新被提前销毁。
- `topology.refresh_srv` 闭包捕获解析器所用 io/工作池；调用者必须让二者活到客户端和所有解析任务结束。

## Snappy 与压缩职责

`wire/mongodb_wire_protocol.cpp` 负责 OP_MSG 组帧；`wire/mongodb_wire_compression.cpp` 单独负责 OP_COMPRESSED。
Snappy 使用 libsnappy 的原始 block 格式，不使用 framed stream；解压前校验声明大小，拒绝畸形长度和尾随数据。
默认构建请求启用 `CNETMOD_ENABLE_MONGODB_SNAPPY`。找到 libsnappy 时声明 Snappy 能力；系统未安装 libsnappy 时继续构建，并输出 Arch、Ubuntu 和 CentOS 的安装命令。显式关闭该开关时不会探测或声明 Snappy 能力。
运行时使用 `connection_options::enable_snappy_compression` 开启；URI 的 `compressors=...` 顺序保留到握手。
程序配置可用 `compression_preference` 的 codec ID 列表指定优先级；未指定时保留 zstd、Snappy、zlib 的默认顺序。

## 云认证职责与组合入口

核心包 `cnetmod.protocol.mongodb` 实现 SASL、AWS SigV4、OIDC cache、推测认证和重认证；不导入 HTTP 或 Application。
云凭据获取与装配属于 Application，源码位于
`src/application/integration/mongodb/credentials/`，随 `cnetmod::application` 构建和导出。
不新增顶层 `src/integration/` 层，也不单独导出 MongoDB cloud 组件。
用户入口是 `import cnetmod.application;` 和 `mongodb_service` 的 URI 自动配置，见
[Application MongoDB 配置](../infra/application.md)。

`mongodb_metadata_http.cpp` 复用框架 HTTP 客户端，验证 TLS、禁用重定向和 cookie，限制响应为 1 MiB。
AWS Web Identity、ECS、EC2 IMDSv2、OIDC workload、token 文件读取和凭据文档解析各自分文件；禁止 `types` / `common` 等聚合文件名。
高级自定义凭据装配可使用 `cnetmod.application.mongodb_credentials` 中的
`cnetmod::application::mongodb_credentials` 工厂，并通过
`mongodb_service_options::configure_connection` 设置 provider。常规用户不必手动装配 provider
或管理协议连接池。

### AWS IAM

`authentication_mechanism::aws` / URI `authMechanism=MONGODB-AWS` 使用 `$external` 并要求 TLS。
AWS 凭据不得放在 URI 或 username/password 选项中。优先使用 `aws_provider`；默认组合 provider 按环境变量、Web Identity、ECS、EC2 IMDSv2 获取凭据。

- 环境凭据按每次授权动态读取；已经取得的临时元数据凭据按 expiration 缓存并提前刷新。
- Web Identity 读取 AWS_WEB_IDENTITY_TOKEN_FILE 和 AWS_ROLE_ARN，支持 AWS_ROLE_SESSION_NAME、AWS_REGION/AWS_DEFAULT_REGION 及中国区域 STS 域名。
- ECS 只接受可信 HTTP metadata 主机或显式 HTTPS endpoint；可读取 Authorization token 文件。EC2 使用 IMDSv2 token，不降级为 IMDSv1。
- `aws_credential_cache` 支持异步 provider；同步 AWS SDK 调用应通过 `blocking_invoke` 桥接，不能阻塞 io_context。
- AWS SASL 严格校验 64 字节服务端 nonce 的客户端前缀、STS 主机和 conversationId，再计算包含 session token 的 SigV4 签名。

### OIDC 机器身份

`authentication_mechanism::oidc` / URI `authMechanism=MONGODB-OIDC` 使用 `$external`、要求 TLS、禁止 password。
支持自定义 `oidc_token_cache` callback，或通过 `configure_cloud_credentials` 选择 Azure/GCP/Kubernetes workload provider。
Azure/GCP 必须提供 TOKEN_RESOURCE；Kubernetes 不允许该参数。callback 与内置 environment 不得同时配置。

- Azure 使用 Instance Metadata Service，传递编码后的 resource 和可选 client_id；GCP 使用 metadata identity endpoint 和编码后的 audience。
- Kubernetes 优先读取 AZURE_FEDERATED_TOKEN_FILE，其次 AWS_WEB_IDENTITY_TOKEN_FILE，最后 ServiceAccount 默认 token 文件。
- token 文件在工作池读取，必须是大小受限的普通文件；provider 不解析或验证不透明 access token 的 JWT 内容。
- 每个客户端/池共享一个 cache，所有调用必须位于同一 io_context。一次只执行一个 provider callback；等待者各自遵守自己的超时，不阻塞缓存快照和条件失效。
- callback 参数包含 version=1、username 和剩余 timeout；自定义异步 callback 必须遵守该 timeout，跨线程 SDK 返回必须切回所属 io_context。
- callback 间至少 100ms；按 token 值条件失效，不清除另一连接刚更新的 token。失败的推测认证不直接清空 cache。
- 有缓存时在 hello 中尝试 speculativeAuthenticate。普通操作返回 391 时保留原命令的 guard/watchdog，执行 SASL 后只重发一次；第二次 391 直接返回。

### X.509

`authentication_mechanism::x509` 使用 TLS 客户端证书、`$external` 和可选用户名，不允许 password。
配置 `tls_cert_file` / `tls_key_file`；认证命令与 AWS/OIDC/SCRAM 各自放在独立 transport 实现文件中。

## 新能力验证入口

本地 MongoDB 8.0 副本集测试包含独立 Snappy 协商和压缩优先级用例。
`test_mongodb_discovery` 验证 URI、父域/IDNA、TXT 默认值和恶意记录拒绝；`mongodb_srv_lookup_driver <hostname>` 可单独验证真实系统 DNS 而不连接返回的数据库节点。
`test_mongodb_cloud_auth` 覆盖独立 SigV4 向量、SASL 身份校验、token cache 并发和超时。
`test_mongodb_cloud_handshake` 用 Python 标准库 TLS peer 验证 AWS nonce/签名、OIDC cache/18/391/speculative flow 与 X.509 客户端证书；由 BoringSSL 在测试临时目录生成证书，不需要外部 openssl 命令或云账号。
开启 HTTP 后另有 `test_mongodb_aws_providers` 与 `test_mongodb_oidc_providers`，用注入的 metadata transport 验证完整凭据链，不访问真实用户云凭据。

安装包消费者验证位于 `testing/package/mongodb_application_consumer`：仅链接 `cnetmod::application`，验证 URI 服务注册和注入 metadata provider，防止源树构建掩盖导出依赖缺失。
