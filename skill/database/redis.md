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
| Spring 风格值/Hash/Set 操作 | `redis_template` |
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

    auto push(std::span<const std::string> arguments) -> bool;

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
    auto is_reusable() const noexcept -> bool;
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

连接池等待者的取消和池停止通知使用等待协程帧内的投递节点，不为通知本身分配堆内存。
取消仅投递原等待者，原协程恢复后取得协程锁并移除登记，不启动 detached 清理协程。
连接分配、调用者取消和池停止通过同一个 pending 标志竞争完成权，只有获胜者投递。
调用者仍必须等待获取连接的任务结束后再销毁池和事件循环；此机制不支持强制销毁在途任务。

归还连接遇到池锁竞争时，使用连接节点内的投递通知，不创建 detached 归还协程。
待处理归还计入 `pending_maintenance()`，`cancel()` 等待已登记通知完成。
这不替代外部借出连接的生命周期管理：所有 lease 仍须在池销毁前归还。

`checked_out_count()` 在所属执行线程扫描节点，统计正常借出及停止后仍被持有的连接，
不为每次借用增加计数器操作。停止后归还的连接会关闭而不重新进入空闲池。
Application Redis 服务停止时按调用方 deadline 等待 lease；超时返回错误并保留服务状态，
归还后可再次调用停止。该约定仍不允许销毁外部正在使用的池。

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
    auto connect(connect_options seed, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
    auto refresh_slots() -> task<std::expected<void, std::string>>;
    auto cmd_for_key(std::vector<std::string> args, std::string_view key, std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto cmd_for_keys(std::vector<std::string> args,
        std::span<const std::string_view> keys, cancel_token& cancellation,
        std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::error_code>>;
    auto pipeline(std::span<const cluster_pipeline_item> items)
        -> task<std::expected<std::vector<resp3_node>, std::string>>;
    auto pipeline_ordered(std::span<const cluster_pipeline_item> items,
        cancel_token& cancellation)
        -> task<std::expected<std::vector<std::vector<resp3_node>>, std::error_code>>;
    void close() noexcept;
    auto slots() const noexcept -> const cluster_slot_cache&;
};
```

### `redis_template` — 业务语义门面

`redis_template` 通过 `import cnetmod.protocol.redis;` 导出。它复用
`connection_pool`、`client::exchange()` 与 `client::is_reusable()`，不创建协议实现或
执行域。子模块的合法名称是 `cnetmod.protocol.redis:redis_template`；C++ 关键字
`template` 不能直接作为模块名分段。

```cpp
redis::redis_template cache{pool, {
    .ns = {.prefix = "orders:"},
    .default_ttl = std::chrono::minutes{10},
    .scan_page = 128,
    .scan_limit = 100000,
}};

auto order = co_await cache.get_as<order_record>("42");
auto saved = co_await cache.set_as("42", value);

auto batch = cache.pipeline();
batch.get("42").exists("43").incr("revision");
auto replies = co_await cache.execute(batch);
```

公开操作提供无令牌便利重载以及 `cancel_token&` 重载。所有取连接路径（包括
`redis_template` 便利重载）均受连接池的 `pool_timeout` 约束；Redis 断线时等待
可用连接不会无限挂起。模板的 `template_options.operation_timeout` 默认 5 秒，
限制每次 RESP 命令的完整写入与响应；即使连接成功后服务端不回包也会超时并
丢弃该连接。设置为零可关闭命令级限制。若需要更短的请求级总预算，在所属
`io_context` 上用 `with_timeout` / `with_deadline` 包装带令牌重载；取消会传到连接获取
及完整 RESP exchange。任何未完整 exchange 都关闭连接，池只重新发布
`is_reusable()` 为真的 lease。

- `get` / `hget` 将 Redis nil 映射为成功的 `std::optional{}`，不映射成错误。
- `mget` 保持与输入逐位对应，内部消化 RESP aggregate 根节点。
- `hgetall` 同时规范化 RESP2 array 和 RESP3 map。
- `sscan_all` 循环游标、保持首次出现顺序、去重，并在超过 `scan_limit` 时整体失败。
- Pipeline 只执行一次 `exchange()`，返回
  `std::vector<std::expected<reply, std::error_code>>`；Redis 单条错误不会覆盖其他条。
- `json_codec` 是 `get_as` / `set_as` 的 Glaze-only 默认 JSON codec；Redis 模块只通过 `cnetmod.json` 解析和序列化。若业务需要非 JSON 值编码，可显式传入 Redis 值 codec，但这不会替换框架 JSON 引擎。
- 配置 `span_exporter` 后，每条命令产生 CLIENT span，只记录
  `db.system.name=redis` 与 `db.operation.name`，不记录 key、value 或服务端错误正文。

Application 的 `redis_service::make_template(options, parent)` 自动复用服务连接池及
Telemetry Hub 的 span exporter；调用方只显式传递当前协程的 trace parent。

### 分布式锁

`redis_template` 提供所有权安全的单 Redis 分布式锁。获取使用一条 Lua 脚本原子执行
`SET key token NX PX lease` 与 fencing counter 递增；续租与释放分别使用 Lua 比较随机
owner token 后再执行 `PEXPIRE`/`DEL`。锁过期后，旧持有者不能续租或删除新持有者的锁。

```cpp
auto acquired = co_await cache.lock("settlement:{tenant-42}", {
    .lease = std::chrono::seconds{15},
    .wait_timeout = std::chrono::seconds{2},
    .retry_interval = std::chrono::milliseconds{50},
    .retry_jitter = 0.2,
}, cancellation);
if (!acquired)
    co_return std::unexpected(acquired.error());

const auto fence = acquired->fencing_token();
auto stored = co_await write_with_fencing_token(fence, cancellation);
auto released = co_await acquired->release(cancellation);
```

- `try_lock()` 只尝试一次；被占用返回成功的 `std::optional{}`，协议/网络失败仍为错误。
- `lock()` 在 `wait_timeout` 内进行带 jitter 的有界重试；超时返回 `errc::timed_out`。
- Application 创建的模板自动绑定其事件循环，因此支持等待重试。直接构造模板时，如需
  `wait_timeout > 0`，应把所属 `io_context*` 作为最后一个构造参数传入。
- `distributed_lock` 仅可移动。析构函数不会隐藏异步网络 I/O，正常路径必须显式
  `co_await release()`；释放失败时按业务重试或等待 lease 到期。
- `renew()`/`release()` 返回 `false` 表示 owner token 已不匹配，即调用方已经失去所有权。
- fencing token 必须随受保护写入传给存储端，并由存储端拒绝小于已见最大值的旧 token；
  仅凭 Redis lease 不能阻止暂停过久的旧进程在恢复后继续写外部系统。
- Redis Cluster 场景中的锁键应使用 hash tag，例如
  `settlement:{tenant-42}`，确保锁键及其 `:fence` 键落在同一 slot。
- 不把锁用于长事务；lease 应覆盖单次临界区，并为最坏延迟留出余量。

`is_open()` 只表示传输层 socket 尚未关闭；连接池使用更严格的
`is_reusable()`，同时要求不存在未消费的 RESP 数据。`cmd`、`exec`、`pipe` 和
`exchange` 在写入后发生解析错误、取消、EOF 或检测到多余应答时都会关闭连接，防止
残留帧被下一位借用者误认为自己的响应。

Cluster 只支持逻辑数据库 0，`connect_options::db != 0` 会在网络 I/O 前失败。客户端维护
16384 槽缓存，处理 MOVED，并在 ASKING 成功后把原命令真正重发到迁移目标。跨节点
pipeline 会先按节点批量发送，再按调用者原始顺序恢复每条完整 RESP 响应。

多 key 命令必须同槽。使用 `make_cluster_key("session", tenant, key)` 生成
`session:{tenant}:key`，并用 `keys_share_slot()` 在发送前验证。Redis Cluster 已经负责
Redis 数据分片和故障转移，不要再使用 SELECT/多 DB 模拟分库；业务隔离使用 key
namespace，原子多 key 操作用 hash tag。

Application 自动装配使用 `type: redis` 和 `mode: cluster`：

```json
{
  "type": "redis",
  "instance": "sessions",
  "enabled": true,
  "mode": "cluster",
  "database": 0,
  "seeds": [
    {"host": "redis-0.internal", "port": 6379},
    {"host": "redis-1.internal", "port": 6379}
  ],
  "password": "${REDIS_PASSWORD}"
}
```

`redis_cluster_service` 依次尝试 seed、缓存槽表、用 PING 与完整槽覆盖做健康判断，并在
停止时关闭 seed 和节点连接。恢复预算与 required/optional 语义沿用统一生命周期。

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
    auto pool() noexcept -> thread_pool&;             // cnetmod CPU 线程池
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

## 本地真实服务测试

`test_application_redis_live` 只连接显式指定端口的 `127.0.0.1`，执行 RESP3 建连、
健康 PING、三次关闭自身借出的连接后重新建连，以及受监管停止，不创建键或修改服务配置。设置 `CNETMOD_REDIS_INTEGRATION=1`
及 `CNETMOD_REDIS_TEST_PORT` 后通过 CTest 运行；未启用时返回 77，由 CTest 标记 skipped。
端口缺失或非法直接失败。测试服务须自行启动、隔离和回收；该入口不验证服务端宕机恢复，
也不意味着 Redis/Valkey 的真实服务验收已经通过。

## 参考示例

- `examples/redis/redis_client.cpp` — 基本命令 + request 构建器 + Pipeline + 阻塞调用桥接
- `examples/redis/redis_pool.cpp` — 连接池：单/多线程获取连接
- `examples/redis/redis_sharded_pool.cpp` — 分片连接池 + server_context 多核场景
- `examples/http/multicore_http.cpp` — `server_context` 多核架构参考
