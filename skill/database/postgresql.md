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
    explicit client(io_context&) noexcept;
    auto connect(connection_options options = {}) -> task<result_set>;
    auto query(std::string_view sql) -> task<result_set>;
    auto execute(parameterized_query parameters) -> task<result_set>;
    auto prepare(std::string_view sql, std::string name = {})
        -> task<std::expected<prepared_statement, std::string>>;
    auto execute(const prepared_statement&, std::span<const param_value> = {})
        -> task<result_set>;
    auto copy_from(std::string_view copy_sql, copy_data_source source) -> task<result_set>;
    auto query_batches(std::string_view sql, std::size_t batch_size,
        std::function<task<void>(std::span<const row>)> consume) -> task<result_set>;
    auto terminate() -> task<void>;
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
    auto acquire() -> task<std::expected<pooled_connection, std::error_code>>;
    auto acquire(cancel_token& cancellation)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto close() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto checked_out_count() const noexcept -> std::size_t;
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

### 认证机制

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
    auto pool() noexcept -> thread_pool&;             // stdexec 线程池
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
