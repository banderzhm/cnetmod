# ORM 模型定义 / CRUD / 迁移

> cnetmod 协议无关 SQL ORM，支持 MySQL / PostgreSQL。
> 模块: `import cnetmod.protocol.mysql;` + `#include <cnetmod/orm.hpp>`

## 核心原则

- 模型定义用 `CNETMOD_MODEL` + `CNETMOD_FIELD` 宏（编译期反射）
- CRUD 操作通过 `mysql_session` 或 `base_mapper<T>`
- 流式查询用 `query_wrapper<T>`（支持成员指针类型安全）
- DDL 迁移用 `mysql_synchronize_schema<T>()`
- XML mapper 提供 MyBatis 风格动态 SQL

## 1. 模型定义

```cpp
#include <cnetmod/orm.hpp>
import std;
import cnetmod.protocol.mysql;

struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
    std::time_t created_at = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_),
    CNETMOD_FIELD(created_at, "created_at", timestamp, NULLABLE))
```

### CNETMOD_FIELD 参数

`CNETMOD_FIELD(member, "col", type [, flags [, strategy]])`

列类型后缀: `bigint`, `int_`, `varchar`, `text`, `double_`, `char_`, `timestamp`, `tinyint`, `boolean_`

### 字段标志

| 宏 | 含义 | 宏 | 含义 |
|----|------|----|------|
| `PK` | 主键 | `VERSION` | 乐观锁版本号 |
| `AUTO_INC` | 自增 | `LOGIC_DELETE` | 软删除标记 |
| `NULLABLE` | 允许 NULL | `FILL_INSERT` | 插入时自动填充 |
| `UNIQUE_KEY` | 唯一约束 | `FILL_INSERT_UPDATE` | 插入+更新填充 |
| `TENANT_ID` | 多租户字段 | | |

标志可组合: `PK | AUTO_INC`。

## 2. ID 生成策略

### UUID 主键

```cpp
struct Tag { orm::uuid id; std::string name; };
CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id, "id", char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar))
```

- `orm::uuid` — 128 位 UUID，`to_string()` / `from_string()`
- `uuid_v4()` — 生成随机 UUID v4
- DDL 生成 `CHAR(36)`

### Snowflake 主键

```cpp
struct Event { std::int64_t id = 0; std::string title; };
CNETMOD_MODEL(Event, "events",
    CNETMOD_FIELD(id, "id", bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar))
```

- `snowflake_generator(uint16_t machine_id)` — 构造（0~1023）
- `next_id() -> int64_t` — 生成 ID（非线程安全）

创建 session 时传入: `orm::mysql_session db(cli, snowflake);`

## 3. mysql_session — 异步 ORM 会话

```cpp
using mysql_session = basic_db_session<mysql::client>;
mysql_session(mysql::client& cli);
mysql_session(mysql::client& cli, snowflake_generator& sf);
```

| 方法 | 签名 |
|------|------|
| `find_all<T>()` | `-> task<orm_result<T>>` |
| `find_by_id<T>(param_value)` | `-> task<orm_result<T>>` |
| `find(const select_builder<T>&)` | `-> task<orm_result<T>>` |
| `find(const query_wrapper<T>&)` | `-> task<orm_result<T>>` |
| `insert(T&)` | `-> task<orm_result<T>>` |
| `insert_many(span<T>)` | `-> task<orm_result<T>>` |
| `update(const T&)` | `-> task<orm_result<T>>` |
| `update(const update_wrapper<T>&)` | `-> task<orm_result<T>>` |
| `remove(const T&)` | `-> task<orm_result<T>>` |
| `remove_by_id<T>(param_value)` | `-> task<orm_result<T>>` |
| `remove(const delete_builder<T>&)` | `-> task<orm_result<T>>` |
| `remove(const query_wrapper<T>&)` | `-> task<orm_result<T>>` |
| `count(const query_wrapper<T>&)` | `-> task<expected<size_t, string>>` |
| `create_table<T>()` / `drop_table<T>()` | `-> task<orm_result<T>>` |
| `raw_query(sql)` | `-> task<result_set>` |
| `transaction(Func&&)` | `-> task<result_set>` |

### orm_result<T>

```cpp
template <class T> struct orm_result {
    std::vector<T> data;
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
    std::string error_msg;
    auto ok() const noexcept -> bool;
    auto is_err() const noexcept -> bool;
    auto empty() const noexcept -> bool;
    auto first() const -> std::optional<T>;
};
```

### 示例

```cpp
orm::mysql_session db(cli, snowflake);

Article a; a.title = "Hello"; a.status = 1;
auto r = co_await db.insert(a);           // a.id 自动回填
auto all = co_await db.find_all<Article>();
auto one = co_await db.find_by_id<Article>(orm::param_value::from_int(42));
a.view_count += 100;
co_await db.update(a);
co_await db.remove(a);
co_await db.remove_by_id<Article>(orm::param_value::from_int(1));
```

## 4. base_mapper<T> — MyBatis-Plus 风格

```cpp
orm::mysql_base_mapper<User> mapper(cli);

co_await mapper.insert(user);
auto opt = co_await mapper.select_by_id(42);
auto list = co_await mapper.select_list();
auto cnt = co_await mapper.select_count();
bool exists = co_await mapper.exists_by_id(42);
co_await mapper.update_by_id(user);
co_await mapper.update_selective(user);   // 仅更新非 null 字段
co_await mapper.delete_by_id(42);
co_await mapper.delete_batch_ids(id_vec);
auto page = co_await mapper.select_page(1, 20, wrapper);
```

主要方法: `insert`, `insert_get_id`, `insert_batch`, `delete_by_id`, `delete_batch_ids`, `update_by_id`, `update_selective`, `select_by_id`, `select_batch_ids`, `select_list`, `select_one`, `select_count`, `exists_by_id`, `select_page`, `delete_by_wrapper`, `update_by_wrapper`。

## 5. query_wrapper<T> — 流式查询

```cpp
// 成员指针（类型安全）
auto qw = orm::query_wrapper<User>{}
    .eq(&User::status, 1)
    .contains(&User::name, "Alice")
    .order_by_desc(&User::created_at)
    .limit(10);

// 字符串列名
auto qw2 = orm::query_wrapper<User>{}
    .eq("status", 1)
    .like("name", "%Alice%")
    .between("age", 18, 65)
    .in("role", std::vector<std::string>{"admin", "editor"})
    .order_by_desc("created_at")
    .limit(20).offset(40);
```

### 条件方法

| 方法 | SQL | 方法 | SQL |
|------|-----|------|-----|
| `eq` / `ne` | `=` / `!=` | `like` / `not_like` | `LIKE` |
| `gt` / `ge` / `lt` / `le` | `>` / `>=` / `<` / `<=` | `is_null` / `is_not_null` | `IS NULL` |
| `in` / `not_in` | `IN` / `NOT IN` | `between` / `not_between` | `BETWEEN` |
| `starts_with` / `ends_with` / `contains` | LIKE 变体 | `is_true` / `is_false` | `IS TRUE/FALSE` |
| `raw(sql)` | 原始 SQL（慎用） | `when(bool, fn)` | 条件执行 |

### 逻辑 / 排序 / 聚合

`and_()` / `or_()` 切换连接符 · `and_(nested)` / `or_(nested)` 嵌套条件组 · `order_by_asc` / `order_by_desc` · `limit` / `offset` · `select({...})` 指定列 · `group_by` · `having` · `inner_join` / `left_join` / `right_join` / `full_outer_join` · `select_count` / `select_sum` / `select_avg` / `select_min` / `select_max`

### 构建 SQL

```cpp
auto [sql, params] = qw.build_select_sql();          // MySQL 默认
auto [sql, params] = qw.build_select_sql(sql_dialect::postgresql);
auto [sql, params] = qw.build_count_sql();
auto [sql, params] = qw.build_delete_sql();
auto [sql, params] = qw.build_update_sql(entity);
```

### update_wrapper<T>

```cpp
auto uw = orm::update_wrapper<User>{}
    .set(&User::name, "Bob")
    .eq(&User::id, 42);
co_await mapper.update_by_wrapper(uw);
```

## 6. 查询构建器

```cpp
auto qb = orm::mysql_select<Article>()
    .where("`status` = {}", {orm::param_value::from_int(1)})
    .order_by("`view_count` DESC")
    .limit(10);
auto result = co_await db.find(qb);

auto del = orm::mysql_delete<Article>()
    .where("`status` = {}", {orm::param_value::from_int(0)});
co_await db.remove(del);
```

## 7. DDL 自动迁移

```cpp
auto result = co_await orm::mysql_synchronize_schema<Product>(cli);
if (result.is_err()) { /* handle */ }
if (result.created)
    std::println("表已创建");
else
    std::println("应用了 {} 项变更", result.diff.changes.size());
```

对比 C++ 模型与数据库表结构，自动 ADD / DROP / MODIFY 列。

## 8. XML Mapper（动态 SQL）

```cpp
orm::mapper_registry registry;
registry.load_file("mappers/user_mapper.xml");
orm::mysql_mapper_session session(cli, registry);

auto result = co_await session.query<User>("UserMapper.findByCondition",
    orm::param_context::from_map({
        {"name", orm::param_value::from_string("Alice")},
        {"status", orm::param_value::from_int(1)}}));

co_await session.execute("UserMapper.insertUser", new_user);
```

XML 动态标签: `<if>`, `<where>`, `<set>`, `<trim>`, `<foreach>`, `<choose>/<when>/<otherwise>`, `<include>`, `<bind>`。

## 9. 自动填充 / 软删除 / 多租户

### 自动填充
`CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT)` — `fill_strategy`: `current_timestamp`, `current_date`, `current_time`, `uuid`, `custom`。`global_auto_fill_interceptor()` 获取全局实例。

### 软删除
`CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)` — `logical_delete_interceptor` 自动将 DELETE 转为 `UPDATE SET deleted=1`，SELECT 追加 `deleted=0`。`global_logical_delete_interceptor()`。

### 多租户
`CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID)` — `tenant_context::set_tenant_id(id)` 设置线程级租户；`tenant_guard guard(id)` RAII 守卫；`multi_tenant_interceptor` 自动注入条件。`global_multi_tenant_interceptor()`。

## 10. database_session<Client> — 协议无关会话

```cpp
template <asynchronous_database_client Client>
class database_session {
    explicit database_session(Client& client);
    auto query(std::string_view sql) -> task<query_result>;
    auto execute(std::string_view sql) -> task<query_result>;
    auto execute(parameterized_query) -> task<query_result>;
    auto transaction(Func&&) -> task<query_result>;
    auto transaction(Func&&, isolation_level) -> task<query_result>;
};
```

## CMake 启用

```cmake
-DCNETMOD_ENABLE_ORM=ON     # ORM（默认 ON）
-DCNETMOD_ENABLE_MYSQL=ON   # MySQL 协议
```

## 连接池（生产级用法）

### MySQL connection_pool

ORM 的 `mysql_session` 接受 `mysql::client&`，而 `mysql::connection_pool` 提供的 `pooled_connection` 可通过 `->` 操作符获取 `mysql::client&`，两者天然集成。

**Pool API**（来自 `mysql_pool.cppm`）：

```cpp
// 连接池参数
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string username, password, database;
    ssl_mode ssl = ssl_mode::enable;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(20);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
    // ...
};

// RAII 连接句柄 — 析构时自动归还
class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> mysql::client&;
    auto operator->() noexcept -> mysql::client*;
    void return_without_reset();
};

// 连接池
class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
};

// 分片连接池（多 worker 专用）
class sharded_connection_pool {
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params);
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params,
        std::size_t num_shards);
    auto async_run() -> task<void>;
    auto async_get_connection(io_context& io) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;
};
```

**单线程 + 连接池示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    // 创建连接池
    cn::mysql::connection_pool pool(ctx, cn::mysql::pool_params{
        .host = "127.0.0.1",
        .port = 3306,
        .username = "root",
        .password = "secret",
        .database = "myapp",
        .initial_size = 2,
        .max_size = 16,
    });

    // 启动连接池后台维护
    cn::spawn(ctx, pool.async_run());
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(100));

    // 从池中获取连接
    auto conn = co_await pool.async_get_connection();
    if (!conn) {
        std::println("get connection failed: {}", conn.error().message());
        co_return;
    }

    // 用 pooled_connection 创建 ORM session
    orm::mysql_session db(conn->get());

    Article a;
    a.title = "Hello ORM";
    a.status = 1;
    auto r = co_await db.insert(a);
    std::println("inserted id={}", a.id);

    auto all = co_await db.find_all<Article>();
    std::println("total: {}", all.data.size());

    // pooled_connection 析构时自动归还连接池
}
```

## 多核服务器部署

### sharded_connection_pool + server_context

生产环境中，每个 worker 线程使用 `sharded_connection_pool` 获取本分片连接，避免跨线程竞争。

**架构**：

```
server_context
├── accept_io()          — 接受 HTTP 请求
├── worker_io[0]         — sharded_pool shard[0]
├── worker_io[1]         — sharded_pool shard[1]
├── worker_io[2]         — sharded_pool shard[2]
└── worker_io[3]         — sharded_pool shard[3]
```

**生产级 CRUD 服务示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.http;
import cnetmod.protocol.mysql;
#include <cnetmod/orm.hpp>

namespace cn = cnetmod;
namespace mysql = cnetmod::mysql;

// 模型定义
struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_))

// 全局分片连接池指针（worker 共享）
mysql::sharded_connection_pool* g_pool = nullptr;

// 处理 GET /users — 查询所有用户
auto handle_get_users(cn::io_context& io, const cn::http::request& req)
    -> cn::task<cn::http::response>
{
    auto conn = co_await g_pool->async_get_connection(io);
    if (!conn)
        co_return cn::http::make_json_response(500, R"({"error":"db unavailable"})");

    orm::mysql_session db(conn->get());
    auto result = co_await db.find_all<User>();

    // 构建 JSON 响应...
    co_return cn::http::make_json_response(200, "[...]");
}

// 处理 POST /users — 创建用户
auto handle_create_user(cn::io_context& io, const cn::http::request& req)
    -> cn::task<cn::http::response>
{
    auto conn = co_await g_pool->async_get_connection(io);
    if (!conn)
        co_return cn::http::make_json_response(500, R"({"error":"db unavailable"})");

    orm::mysql_session db(conn->get());

    User user;
    user.name = "Alice";
    user.status = 1;
    auto r = co_await db.insert(user);

    co_return cn::http::make_json_response(201,
        std::format(R"({{"id":{}}})", user.id));
}

int main() {
    cn::net_init net;

    // 4 worker 线程
    cn::server_context sctx(4, 4);

    // 分片连接池 — 每个 worker 一个分片，避免锁竞争
    mysql::sharded_connection_pool pool(
        sctx.worker_ios(),
        mysql::pool_params{
            .host = "127.0.0.1",
            .port = 3306,
            .username = "root",
            .password = "secret",
            .database = "myapp",
            .initial_size = 4,     // 每分片初始连接
            .max_size = 32,        // 每分片最大连接
            .ssl = mysql::ssl_mode::disable,
        });
    g_pool = &pool;

    // HTTP 路由
    cn::http::router router;
    router.get("/users", [](cn::io_context& io, const cn::http::request& req)
        -> cn::task<cn::http::response> {
        co_return co_await handle_get_users(io, req);
    });
    router.post("/users", [](cn::io_context& io, const cn::http::request& req)
        -> cn::task<cn::http::response> {
        co_return co_await handle_create_user(io, req);
    });

    cn::http::server srv(sctx);
    srv.listen("0.0.0.0", 8080);
    srv.set_router(std::move(router));

    // 启动连接池和服务器
    cn::spawn(sctx.accept_io(), pool.async_run());
    cn::spawn(sctx.accept_io(), srv.run());

    sctx.run();
}
```

> **关键模式**：`async_get_connection(io)` 传入当前 worker 的 `io_context`，分片池优先从对应分片获取连接，避免跨线程竞争。

### 每 worker 独立 session 模式

如果不想使用分片池，也可以为每个 worker 创建独立的 `connection_pool` + `mysql_session`：

```cpp
// 在 worker 启动时为每个 io_context 创建独立连接池
for (auto* worker_io : sctx.worker_ios()) {
    auto* pool = new mysql::connection_pool(*worker_io, mysql::pool_params{
        .host = "127.0.0.1",
        .username = "root",
        .password = "secret",
        .database = "myapp",
        .max_size = 8,
    });
    cn::spawn(*worker_io, pool->async_run());
    // pool 与 worker_io 生命周期一致
}
```

> **推荐**：大多数场景使用 `sharded_connection_pool` 更简洁；独立池适合需要不同配置的混合负载。

## Do's & Don'ts（连接池补充）

| Do | Don't |
|---|---|
| 多核使用 `sharded_connection_pool` + `worker_ios()` | 不要跨 worker 共享单个 `connection_pool` |
| `pooled_connection` 用完自动归还，作用域控制在最小 | 不要长期持有 `pooled_connection` 不放 |
| 合理设置 `max_size` 避免数据库连接耗尽 | 不要设置 `max_size` 超过数据库 `max_connections` |
| 使用 `pool_timeout` 防止获取连接无限等待 | 不要忽略 `async_get_connection()` 的错误 |
