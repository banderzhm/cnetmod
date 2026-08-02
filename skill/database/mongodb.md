# MongoDB 协议模块

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
    auto pool() noexcept -> thread_pool&;             // stdexec 线程池
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
