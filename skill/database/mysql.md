# MySQL 协议模块

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

    // 4 分片，适合 4 worker 线程
    mysql::sharded_connection_pool pool(ctx, params, 4);
    co_await pool.async_run();

    // 获取连接（自动选择分片）
    auto conn_r = co_await pool.async_get_connection();
    if (conn_r) {
        auto rs = co_await (*conn_r)->query("SELECT COUNT(*) FROM orders");
        if (rs.has_rows())
            std::println("订单总数: {}", rs.rows[0][0].to_string());
    } // pooled_connection 析构时自动归还

    // 绑定到特定 io_context（用于多 worker 场景）
    auto conn2_r = co_await pool.async_get_connection(ctx);
    if (conn2_r) {
        auto rs = co_await (*conn2_r)->execute(
            mysql::with_params("UPDATE orders SET status = {} WHERE id = {}",
                {mysql::param_value::from_string("shipped"),
                 mysql::param_value::from_int(1024)}));
    }

    co_await pool.cancel();
    ctx.stop();
}
```

## 多核服务器部署

### server_context 模式

`server_context` 提供多核部署架构：

- **1 个 accept 线程** — 专职接受新连接（`accept_io()`）
- **N 个 worker 线程** — round-robin 处理业务请求（`next_worker_io()`）
- **M 个 stdexec 线程池** — 卸载 CPU 密集操作（`pool()`）

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
