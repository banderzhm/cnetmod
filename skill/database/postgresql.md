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
| ORM 映射 | Application `repository<T>` + PostgreSQL gateway（见 [database-orm.md](database-orm.md)） |

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

### ORM 集成

PostgreSQL 只提供协议客户端、连接池、方言和结果适配器。Application
通过统一的 `repository<T>` 绑定 PostgreSQL `session_gateway`，因此业务
代码不依赖 `postgresql_session` 或 PostgreSQL 专属结果类型。

```cpp
auto users = runtime.repository<User>(
    "primary", {}, application::database_provider::postgresql);
auto user = co_await users->save(User{.name = "Alice", .email = "alice@example.com"});
auto page = co_await users->page(query_wrapper<User>{}.eq(&User::status, 1), 1, 20);
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
