# Redis

> 异步 Redis 客户端，支持 RESP3 协议、连接池、分片池、集群路由及 Pipeline。

**import**: `import cnetmod.protocol.redis;`
**CMake**: `-DCNETMOD_ENABLE_REDIS=ON`
**源码**: `src/protocol/redis/`

## 场景导航

| 场景 | 推荐入口 |
|------|----------|
| 简单命令（GET/SET/HSET 等） | `client::cmd` |
| Pipeline 批量命令 | `client::pipe` |
| 请求构建器（复杂/流水线） | `request` + `client::exec` |
| 连接池 | `connection_pool` |
| 多核分片连接池 | `sharded_connection_pool` |
| 集群路由（MOVED/ASK） | `cluster_client` |
| Pub/Sub | `client::subscribe` / `client::psubscribe` |

## API 参考

### Redis Value 类型 (`resp3_node`)

RESP3 协议类型枚举：

```cpp
enum class resp3_type {
    array, push, set, map, attribute,
    simple_string, simple_error, number, doublean, boolean, big_number,
    null, blob_error, verbatim_string, blob_string, streamed_string_part, invalid
};

auto to_code(resp3_type type) noexcept -> char;
auto to_type(char code) noexcept -> resp3_type;
auto is_aggregate(resp3_type type) noexcept -> bool;
auto type_name(resp3_type type) noexcept -> std::string_view;
```

**`resp3_node`** — 解析后的 RESP3 节点：

```cpp
struct resp3_node {
    resp3_type data_type = resp3_type::invalid;
    std::size_t aggregate_size = 0;
    std::size_t depth = 0;
    std::string value;
    auto is_error() const noexcept -> bool;
    auto is_null() const noexcept -> bool;
    auto is_aggregate() const noexcept -> bool;
    auto as_integer() const noexcept -> std::int64_t;
    auto as_double() const noexcept -> double;
    auto as_bool() const noexcept -> bool;
    auto to_string() const -> std::string;
};
```

**辅助函数**:
```cpp
auto first_value(const std::vector<resp3_node>& nodes) noexcept -> std::string_view;
auto all_values(const std::vector<resp3_node>& nodes) -> std::vector<std::string_view>;
auto is_ok(const std::vector<resp3_node>& nodes) noexcept -> bool;
auto has_error(const std::vector<resp3_node>& nodes) noexcept -> bool;
auto error_message(const std::vector<resp3_node>& nodes) noexcept -> std::string_view;
```

### `redis_errc` — 错误码

```cpp
enum class redis_errc {
    success = 0, invalid_data_type, not_a_number, exceeds_max_nested_depth,
    unexpected_bool_value, empty_field, incompatible_size, not_a_double,
    resp3_simple_error, resp3_blob_error, resp3_null,
    not_connected, resolve_timeout, connect_timeout, pong_timeout,
    ssl_handshake_timeout, unknown_error
};
```

### `request` — 请求构建器

支持单命令、Pipeline 多命令、range 批量参数。

```cpp
class request {
    request() = default;

    /// 追加命令（可变参数）
    template <class... Ts> void push(std::string_view cmd, Ts const&... args);

    /// 追加命令：cmd key [range elements...]
    template <class ForwardIterator>
    void push_range(std::string_view cmd, std::string_view key,
        ForwardIterator begin, ForwardIterator end);

    /// 追加命令：cmd key [range container]
    template <class Range>
    void push_range(std::string_view cmd, std::string_view key, const Range& range);

    /// 追加键值对范围：cmd key [k1 v1 k2 v2 ...]
    template <class ForwardIterator>
    void push_range_pairs(std::string_view cmd, std::string_view key,
        ForwardIterator begin, ForwardIterator end);

    auto payload() const noexcept -> std::string_view;
    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    void clear();
    void reserve(std::size_t n);
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.redis;

using cn::redis::request;

request req;
req.push("SET", "key", "value");
req.push("GET", "key");
req.push("HSET", "hash", "field1", 100, "field2", 200);

// Pipeline 多命令
request multi;
multi.push("SET", "a", "alpha");
multi.push("SET", "b", "beta");
multi.push("MGET", "a", "b");
```

### `resp3_parser` — RESP 解析器

```cpp
class resp3_parser {
    static constexpr std::size_t max_embedded_depth = 5;
    resp3_parser();
    auto consume(std::string_view data, std::error_code& ec) -> std::optional<resp3_node>;
    auto done() const noexcept -> bool;
    auto consumed() const noexcept -> std::size_t;
    auto is_parsing() const noexcept -> bool;
    void reset();
};

auto parse_response(std::string_view data, std::size_t expected_responses = 1)
    -> std::expected<std::vector<resp3_node>, std::error_code>;
```

### `connect_options` — 连接配置

```cpp
struct connect_options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password;
    std::string username;
    std::uint32_t db = 0;
    bool resp3 = true;
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
};
```

### `client` — 异步 Redis 客户端

```cpp
class client {
    explicit client(io_context& ctx) noexcept;
    auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>>;
    auto is_open() const noexcept -> bool;
    void close() noexcept;

    /// 执行 request 构建器（支持 pipeline）
    auto exec(const request& req) -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// 快捷单命令
    auto cmd(std::initializer_list<std::string_view> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd(std::span<const std::string> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// 跟随集群 MOVED/ASK 重定向
    auto cmd_follow_redirect(std::vector<std::string> args, std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// Pipeline（initializer_list 语法）
    auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// Pub/Sub
    auto subscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto unsubscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto psubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto punsubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    void on_push(push_callback cb);
    auto receive_push() -> task<std::expected<std::vector<resp3_node>, std::string>>;

    /// Sentinel
    auto sentinel_get_master_addr_by_name(std::string_view master)
        -> task<std::expected<endpoint_info, std::string>>;

    /// 集群工具
    auto is_resp3() const noexcept -> bool;
    static auto key_slot(std::string_view key) noexcept -> std::uint16_t;
    static auto parse_redirect(const std::vector<resp3_node>& nodes) -> std::optional<cluster_redirect>;
    static auto parse_cluster_slots(const std::vector<resp3_node>& nodes)
        -> std::expected<std::vector<cluster_slot_range>, std::string>;
};
```

### `connection_pool` — 连接池

```cpp
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password, username;
    std::uint32_t db = 0;
    bool resp3 = true;
    std::size_t initial_size = 1;
    std::size_t max_size = 16;
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
    std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto operator->() noexcept -> client*;
    // RAII: 析构时自动归还连接池
};

class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection() -> std::expected<pooled_connection, std::error_code>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};
```

### `sharded_connection_pool` — 分片连接池

适用于多核 `server_context` 场景，每个 worker `io_context` 绑定独立分片。

```cpp
class sharded_connection_pool {
    sharded_connection_pool(io_context& ctx, pool_params params, std::size_t num_shards = 4);
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params);
    sharded_connection_pool(std::vector<io_context*> worker_contexts, pool_params params, std::size_t num_shards);
    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io, cancel_token& token) -> task<std::expected<pooled_connection, std::error_code>>;
    auto cancel() -> task<void>;
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto shard_count() const noexcept -> std::size_t;
};
```

### 集群路由 (`cluster_client`)

```cpp
struct endpoint_info { std::string host; std::uint16_t port = 0; };
enum class redirect_kind { moved, ask };
struct cluster_redirect { redirect_kind kind; std::uint16_t slot; endpoint_info endpoint; };
struct cluster_slot_range {
    std::uint16_t start, end;
    endpoint_info master;
    std::vector<endpoint_info> replicas;
};
struct cluster_pipeline_item { std::vector<std::string> args; std::string key; };

class cluster_slot_cache {
    void clear();
    void update(const std::vector<cluster_slot_range>& ranges);
    void update_slot(std::uint16_t slot, endpoint_info endpoint);
    auto endpoint_for_slot(std::uint16_t slot) const -> std::optional<endpoint_info>;
    auto endpoint_for_key(std::string_view key) const -> std::optional<endpoint_info>;
    auto covered_slots() const noexcept -> std::size_t;
};

class cluster_client {
    explicit cluster_client(io_context& ctx) noexcept;
    auto connect(connect_options seed) -> task<std::expected<void, std::string>>;
    auto refresh_slots() -> task<std::expected<void, std::string>>;
    auto cmd_for_key(std::vector<std::string> args, std::string_view key, std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto pipeline(std::span<const cluster_pipeline_item> items)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto slots() const noexcept -> const cluster_slot_cache&;
};
```

## 场景 1：基本命令

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;
using cn::redis::first_value;
using cn::redis::is_ok;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::redis::client r(ctx);
    auto result = co_await r.connect({
        .host = "127.0.0.1",
        .port = 6379,
        .password = "your-password",
        .db = 0,
    });
    if (!result) {
        std::println("连接失败: {}", result.error());
        ctx.stop();
        co_return;
    }

    // 快捷命令
    auto pong = co_await r.cmd({"PING"});
    auto set_r = co_await r.cmd({"SET", "mykey", "hello"});
    auto get_r = co_await r.cmd({"GET", "mykey"});
    if (get_r) std::println("GET mykey = {}", first_value(*get_r));

    // Hash 操作
    (void)co_await r.cmd({"HSET", "user:1", "name", "Alice", "score", "100"});
    auto hall = co_await r.cmd({"HGETALL", "user:1"});

    r.close();
    ctx.stop();
}
```

## 场景 2：Pipeline

```cpp
// Pipeline 语法：一次发送多个命令
auto replies = co_await r.pipe({
    {"SET", "p:a", "alpha"},
    {"SET", "p:b", "beta"},
    {"SET", "p:c", "gamma"},
    {"MGET", "p:a", "p:b", "p:c"},
    {"DEL", "p:a", "p:b", "p:c"},
});

auto vals = cn::redis::all_values(*replies);
for (std::size_t i = 0; i < vals.size(); ++i)
    std::println("[{}] {}", i, vals[i]);
```

## 场景 3：request 构建器

```cpp
using cn::redis::request;

// 单命令
request req;
req.push("SET", "rb:key", "value123");
auto set_r = co_await r.exec(req);

// Pipeline 多命令
request multi;
multi.push("SET", "rb:a", "alpha");
multi.push("SET", "rb:b", "beta");
multi.push("GET", "rb:a");
multi.push("GET", "rb:b");
multi.push("DEL", "rb:a", "rb:b", "rb:key");
auto multi_r = co_await r.exec(multi);
std::println("executed {} commands, got {} nodes", multi.size(), multi_r->size());

// 批量 range
std::vector<std::string> fields = {"f1", "v1", "f2", "v2", "f3", "v3"};
request hmset;
hmset.push_range_pairs("HSET", "myhash", fields.begin(), fields.end());
co_await r.exec(hmset);
```

## 场景 4：连接池

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.redis;

namespace cn = cnetmod;

auto pool_demo(cn::io_context& ctx) -> cn::task<void> {
    cn::redis::connection_pool pool(ctx, {
        .host = "127.0.0.1",
        .port = 6379,
        .password = "your-password",
        .initial_size = 4,
        .max_size = 32,
    });

    cn::spawn(ctx, pool.async_run());

    // 获取连接（RAII 自动归还）
    auto conn = co_await pool.async_get_connection();
    if (conn) {
        auto result = co_await conn->cmd({"SET", "pool:key", "pooled!"});
        auto get_r = co_await conn->cmd({"GET", "pool:key"});
        if (get_r) std::println("GET = {}", cn::redis::first_value(*get_r));
    } // conn 析构时自动归还

    co_await pool.cancel();
    ctx.stop();
}
```

## 场景 5：集群客户端

```cpp
cn::redis::cluster_client cluster(ctx);
co_await cluster.connect({.host = "node1", .port = 7000});

// 自动根据 key 的 slot 路由到正确节点
auto result = co_await cluster.cmd_for_key(
    {"GET", "user:100"}, "user:100");

// Pipeline（按 key 分组路由）
std::vector<cn::redis::cluster_pipeline_item> items = {
    {.args = {"SET", "k1", "v1"}, .key = "k1"},
    {.args = {"SET", "k2", "v2"}, .key = "k2"},
};
auto pipe_r = co_await cluster.pipeline(items);
```

## 连接池（生产级用法）

### Pool API 详解

Redis 连接池提供 `connection_pool`（单核）和 `sharded_connection_pool`（多核分片）两种模式。

**`connection_pool`** — 单 io_context 连接池：

```cpp
struct pool_params {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password, username;
    std::uint32_t db = 0;
    bool resp3 = true;
    std::size_t initial_size = 1;           // 初始连接数
    std::size_t max_size = 16;              // 最大连接数
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(10);
    std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);     // 等待连接超时
    std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);  // 重连间隔
    std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);      // 心跳间隔
    std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file, tls_cert_file, tls_key_file, tls_sni;
};

class connection_pool {
    connection_pool(io_context& ctx, pool_params params);
    auto async_run() -> task<void>;                  // 启动池（必须先调用）
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto try_get_connection()
        -> std::expected<pooled_connection, std::error_code>; // 非阻塞获取
    auto cancel() -> task<void>;                     // 关闭池
    auto size() const noexcept -> std::size_t;
    auto idle_count() const noexcept -> std::size_t;
    auto waiter_count() const noexcept -> std::size_t;
};

class pooled_connection {
    auto valid() const noexcept -> bool;
    auto get() noexcept -> client&;
    auto operator->() noexcept -> client*;
    // RAII: 析构时自动归还连接池
};
```

**`sharded_connection_pool`** — 多核分片连接池（每个 worker io_context 绑定独立分片）：

```cpp
class sharded_connection_pool {
    // 单 io_context + 指定分片数
    sharded_connection_pool(io_context& ctx, pool_params params,
        std::size_t num_shards = 4);
    // 多 worker io_context，每个 worker 一个分片（推荐）
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params);
    // 多 worker io_context + 指定分片数
    sharded_connection_pool(std::vector<io_context*> worker_contexts,
        pool_params params, std::size_t num_shards);

    auto async_run() -> task<void>;
    auto async_get_connection() -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(cancel_token& token)
        -> task<std::expected<pooled_connection, std::error_code>>;
    auto async_get_connection(io_context& io)
        -> task<std::expected<pooled_connection, std::error_code>>;
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
import cnetmod.protocol.redis;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void>
{
    cn::redis::pool_params params;
    params.host = "redis.example.com";
    params.port = 6379;
    params.password = "production_secret";
    params.db = 0;
    params.initial_size = 16;        // 大初始池
    params.max_size = 128;           // 高并发池上限
    params.ping_interval = std::chrono::minutes(30);  // 保活心跳

    // 4 分片，适合 4 worker 线程
    cn::redis::sharded_connection_pool pool(ctx, params, 4);
    co_await pool.async_run();

    // 等待连接建立
    co_await cn::async_sleep(ctx, std::chrono::milliseconds(500));
    std::println("分片池就绪: shards={}, total={}, idle={}",
        pool.shard_count(), pool.size(), pool.idle_count());

    // 自动分片选择
    auto conn_r = co_await pool.async_get_connection();
    if (conn_r) {
        co_await conn_r->cmd({"SET", "app:config:version", "2.1"});
        auto val = co_await conn_r->cmd({"GET", "app:config:version"});
        if (val && !val->empty())
            std::println("version = {}", (*val)[0].value);
    }

    // 绑定 io_context（多 worker 场景推荐）
    auto conn2_r = co_await pool.async_get_connection(ctx);
    if (conn2_r) {
        // Pipeline 批量操作
        auto replies = co_await conn2_r->pipe({
            {"SET", "session:abc", "data", "EX", "3600"},
            {"SET", "session:def", "data", "EX", "3600"},
            {"MGET", "session:abc", "session:def"}
        });
    }

    co_await pool.cancel();
    ctx.stop();
}
```

## 多核服务器部署

### server_context 模式

Redis 分片连接池天然支持多核架构：使用 `sharded_connection_pool` + `server_context`，每个 worker 线程绑定独立分片，避免锁竞争。

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

**Redis 多核部署示例**:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.redis;

namespace cn = cnetmod;

constexpr unsigned WORKER_THREADS = 4;
constexpr std::uint16_t PORT = 9090;

auto handle_redis_command(cn::redis::sharded_connection_pool& pool,
                          cn::io_context& worker_io) -> cn::task<void>
{
    // 绑定到当前 worker 的分片获取连接（零跨线程开销）
    auto conn_r = co_await pool.async_get_connection(worker_io);
    if (!conn_r) {
        std::println("获取连接失败: {}", conn_r.error().message());
        co_return;
    }

    auto& conn = *conn_r;

    // 高并发写入
    co_await conn->cmd({"SET", "metrics:requests", "1", "EX", "60"});
    co_await conn->cmd({"INCR", "metrics:total_requests"});

    // Pipeline 批量读取
    auto results = co_await conn->pipe({
        {"GET", "app:config:feature_a"},
        {"GET", "app:config:feature_b"},
        {"GET", "app:config:feature_c"}
    });
    if (results && !results->empty()) {
        for (auto& node : *results)
            std::println("config value: {}", node.value);
    }
}

auto main() -> int
{
    std::println("=== Redis 多核服务 ===");
    std::println("Workers: {}, Pool threads: {}", WORKER_THREADS, WORKER_THREADS);

    cn::net_init net;

    // 1. 创建多核 server_context：4 worker + 4 pool 线程
    cn::server_context sctx(WORKER_THREADS, WORKER_THREADS);

    // 2. 使用 worker io_context 列表创建分片池（每 worker 一个分片）
    cn::redis::pool_params params;
    params.host = "redis.example.com";
    params.port = 6379;
    params.password = "production_secret";
    params.db = 0;
    params.initial_size = WORKER_THREADS * 4;   // 每 worker 4 个初始连接
    params.max_size = WORKER_THREADS * 32;      // 每 worker 最多 32 个连接
    params.ping_interval = std::chrono::minutes(30);

    cn::redis::sharded_connection_pool pool(sctx.worker_ios(), params);

    // 3. 在 accept_io 上启动连接池
    cn::spawn(sctx.accept_io(), pool.async_run());

    // 4. 接受 TCP 连接，round-robin 分发到 worker
    cn::spawn(sctx.accept_io(), [&]() -> cn::task<void> {
        auto listener = cn::tcp_listener::create(sctx.accept_io());
        listener.bind("0.0.0.0", PORT);
        listener.listen(4096);

        std::println("Redis 代理监听 0.0.0.0:{}", PORT);

        while (true) {
            auto [sock, addr] = co_await listener.accept();
            auto& worker = sctx.next_worker_io();  // round-robin
            cn::spawn(worker, [&pool, &worker]() -> cn::task<void> {
                co_await handle_redis_command(pool, worker);
            });
        }
    }());

    // 5. 阻塞运行（accept 线程 + worker 线程）
    sctx.run();
    return 0;
}
```

## Do's & Don'ts

| Do | Don't |
|---|---|
| 使用 `request` 构建器实现 Pipeline 批量操作 | 不要在循环中逐条 `cmd` 发送大量命令 |
| 连接池使用 `pooled_connection` RAII 自动归还 | 不要手动管理连接的释放 |
| 集群环境使用 `cluster_client` 自动处理重定向 | 不要忽略 MOVED/ASK 重定向错误 |
| 使用 `first_value` / `is_ok` 辅助函数解析结果 | 不要假设 `resp3_node` 的 value 字段总是有效 |
| 长连接启用 `ping_interval` 保活 | 不要在高并发场景为每个请求创建新 client |
| 多核场景使用 `sharded_connection_pool` + `server_context` | 不要在多 worker 场景使用单 `connection_pool` |
| 通过 `async_get_connection(io_context&)` 绑定 worker 分片 | 不要让请求跨 worker 分片获取连接 |

## 参考示例

- `examples/redis/redis_client.cpp` — 基本命令 + request 构建器 + Pipeline + stdexec 桥接
- `examples/redis/redis_pool.cpp` — 连接池：单/多线程获取连接
- `examples/redis/redis_sharded_pool.cpp` — 分片连接池 + server_context 多核场景
- `examples/http/multicore_http.cpp` — `server_context` 多核架构参考
