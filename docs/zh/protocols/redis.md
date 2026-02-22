# Redis

异步 Redis 客户端，支持连接池，用于缓存、发布/订阅和数据结构。

## 基础用法

```cpp
import cnetmod;
import cnetmod.protocol.redis;
using namespace cnetmod;

task<void> redis_example(io_context& ctx) {
    redis::client client(ctx);
    
    // Connect
    co_await client.connect("127.0.0.1", 6379);
    
    // Optional: authenticate
    // co_await client.auth("password");
    
    // Optional: select database
    // co_await client.select(1);
    
    // SET
    co_await client.set("key", "value");
    
    // GET
    auto value = co_await client.get("key");
    if (value) {
        std::println("Value: {}", *value);
    }
    
    // DEL
    co_await client.del("key");
    
    co_await client.close();
}
```

## 连接池

```cpp
redis::connection_pool pool(ctx, {
    .host     = "127.0.0.1",
    .port     = 6379,
    .password = "password",
    .db       = 0,
    .resp3    = true,
    .initial_size = 2,
    .max_size     = 16,
    .connect_timeout = std::chrono::seconds(10),
    .ping_interval   = std::chrono::minutes(5),
});

// 启动连接池（建立初始连接 + 后台 PING 健康检查）
spawn(ctx, pool.async_run());

task<void> use_pool(redis::connection_pool& pool) {
    // 借用连接（RAII — 析构自动归还）
    auto conn = co_await pool.async_get_connection();
    if (!conn.valid()) {
        // 所有连接忙且已达 max_size
        co_return;
    }
    
    // 通过 operator-> 使用（与 redis::client 相同）
    co_await conn->set("key", "value");
    auto val = co_await conn->get("key");
    
    // conn 析构时自动归还连接池
}
```

### 连接池参数

`redis::pool_params` 镜像 `redis::connect_options` 并增加连接池设置：

- **连接参数**: `host`、`port`、`password`、`username`、`db`、`resp3`
- **池大小**: `initial_size`（默认 1）、`max_size`（默认 16）
- **超时**: `connect_timeout`（10秒）、`retry_interval`（30秒）、`ping_interval`（5分钟）
- **TLS**: `tls`、`tls_verify`、`tls_ca_file`、`tls_cert_file`、`tls_key_file`、`tls_sni`

### 连接池生命周期

1. `async_run()` — 创建 `initial_size` 个连接，然后进入后台 PING 健康检查循环
2. `async_get_connection()` — 返回空闲连接，或创建新连接（最多 `max_size`），或挂起到等待队列
3. `cancel()` — 关闭所有连接并停止连接池

### TLS 连接池

```cpp
redis::connection_pool pool(ctx, {
    .host     = "redis.example.com",
    .port     = 6380,
    .password = "secret",
    .tls      = true,
    .tls_verify = true,
    .tls_ca_file = "/path/to/ca.crt",
    .initial_size = 4,
    .max_size     = 32,
});
```

## 字符串操作

```cpp
// SET with expiration
co_await client.setex("session:123", 3600, "user_data");

// SET if not exists
bool created = co_await client.setnx("lock:resource", "locked");

// GET and SET
auto old_value = co_await client.getset("counter", "0");

// INCR / DECR
auto new_value = co_await client.incr("counter");
co_await client.decr("counter");
co_await client.incrby("counter", 10);
co_await client.decrby("counter", 5);

// MGET / MSET
co_await client.mset({{"key1", "val1"}, {"key2", "val2"}});
auto values = co_await client.mget({"key1", "key2"});
```

## 列表操作

```cpp
// LPUSH / RPUSH
co_await client.lpush("mylist", "item1");
co_await client.rpush("mylist", "item2");

// LPOP / RPOP
auto item = co_await client.lpop("mylist");

// LRANGE
auto items = co_await client.lrange("mylist", 0, -1);  // All items

// LLEN
auto length = co_await client.llen("mylist");

// LTRIM
co_await client.ltrim("mylist", 0, 99);  // Keep first 100 items
```

## 集合操作

```cpp
// SADD
co_await client.sadd("myset", "member1");
co_await client.sadd("myset", "member2");

// SMEMBERS
auto members = co_await client.smembers("myset");

// SISMEMBER
bool exists = co_await client.sismember("myset", "member1");

// SREM
co_await client.srem("myset", "member1");

// SCARD
auto count = co_await client.scard("myset");

// Set operations
auto union_set = co_await client.sunion({"set1", "set2"});
auto inter_set = co_await client.sinter({"set1", "set2"});
auto diff_set = co_await client.sdiff({"set1", "set2"});
```

## 哈希操作

```cpp
// HSET / HGET
co_await client.hset("user:1", "name", "Alice");
co_await client.hset("user:1", "email", "alice@example.com");

auto name = co_await client.hget("user:1", "name");

// HMSET / HMGET
co_await client.hmset("user:2", {
    {"name", "Bob"},
    {"email", "bob@example.com"},
    {"age", "30"}
});

auto fields = co_await client.hmget("user:2", {"name", "email"});

// HGETALL
auto all_fields = co_await client.hgetall("user:1");
for (const auto& [field, value] : all_fields) {
    std::println("{}: {}", field, value);
}

// HDEL
co_await client.hdel("user:1", "email");

// HINCRBY
co_await client.hincrby("user:1", "login_count", 1);
```

## 有序集合操作

```cpp
// ZADD
co_await client.zadd("leaderboard", 100, "player1");
co_await client.zadd("leaderboard", 200, "player2");
co_await client.zadd("leaderboard", 150, "player3");

// ZRANGE (by rank)
auto top_players = co_await client.zrange("leaderboard", 0, 9);  // Top 10

// ZREVRANGE (reverse order)
auto top_scores = co_await client.zrevrange("leaderboard", 0, 9);

// ZRANGEBYSCORE
auto players = co_await client.zrangebyscore("leaderboard", 100, 200);

// ZSCORE
auto score = co_await client.zscore("leaderboard", "player1");

// ZINCRBY
co_await client.zincrby("leaderboard", 10, "player1");

// ZREM
co_await client.zrem("leaderboard", "player1");

// ZCARD
auto count = co_await client.zcard("leaderboard");
```

## 键操作

```cpp
// EXISTS
bool exists = co_await client.exists("key");

// DEL
co_await client.del("key");

// EXPIRE
co_await client.expire("key", 3600);  // Expire in 1 hour

// TTL
auto ttl = co_await client.ttl("key");  // Seconds until expiration

// KEYS (use with caution in production)
auto keys = co_await client.keys("user:*");

// SCAN (better for production)
auto [cursor, keys] = co_await client.scan(0, "user:*", 100);
```

## 发布/订阅

```cpp
task<void> publisher(io_context& ctx) {
    redis::client client(ctx);
    co_await client.connect("127.0.0.1", 6379);
    
    while (true) {
        co_await client.publish("news", "Breaking news!");
        co_await async_sleep(ctx, 5s);
    }
}

task<void> subscriber(io_context& ctx) {
    redis::client client(ctx);
    co_await client.connect("127.0.0.1", 6379);
    
    // Subscribe to channel
    co_await client.subscribe("news");
    
    // Set message handler
    client.on_message([](std::string_view channel, std::string_view message) {
        std::println("Channel {}: {}", channel, message);
    });
    
    // Keep running
    co_await async_sleep(ctx, std::chrono::hours(24));
}
```

## 管道

```cpp
redis::pipeline pipe(client);

// Queue commands
pipe.set("key1", "value1");
pipe.set("key2", "value2");
pipe.get("key1");
pipe.incr("counter");

// Execute all at once
auto results = co_await pipe.execute();

// Process results
for (const auto& result : results) {
    if (result.is_string()) {
        std::println("String: {}", result.as_string());
    } else if (result.is_integer()) {
        std::println("Integer: {}", result.as_integer());
    }
}
```

## 缓存模式

```cpp
task<std::string> get_user_cached(redis::client& cache, mysql::client& db, int user_id) {
    auto cache_key = std::format("user:{}", user_id);
    
    // Try cache first
    auto cached = co_await cache.get(cache_key);
    if (cached) {
        co_return *cached;
    }
    
    // Cache miss: query database
    auto result = co_await db.query(
        std::format("SELECT * FROM users WHERE id = {}", user_id)
    );
    
    if (result.rows().empty()) {
        co_return "";
    }
    
    auto user_data = result.rows()[0]["data"].as_string();
    
    // Store in cache (1 hour TTL)
    co_await cache.setex(cache_key, 3600, user_data);
    
    co_return user_data;
}
```

## 会话存储

```cpp
class session_store {
    redis::client& redis_;
    
public:
    task<void> create_session(std::string_view session_id, std::string_view user_id) {
        auto key = std::format("session:{}", session_id);
        co_await redis_.setex(key, 3600, user_id);  // 1 hour
    }
    
    task<std::optional<std::string>> get_session(std::string_view session_id) {
        auto key = std::format("session:{}", session_id);
        co_return co_await redis_.get(key);
    }
    
    task<void> delete_session(std::string_view session_id) {
        auto key = std::format("session:{}", session_id);
        co_await redis_.del(key);
    }
    
    task<void> refresh_session(std::string_view session_id) {
        auto key = std::format("session:{}", session_id);
        co_await redis_.expire(key, 3600);
    }
};
```

## 速率限制

```cpp
task<bool> check_rate_limit(redis::client& redis, std::string_view user_id, int max_requests) {
    auto key = std::format("rate_limit:{}", user_id);
    
    // Increment counter
    auto count = co_await redis.incr(key);
    
    // Set expiration on first request
    if (count == 1) {
        co_await redis.expire(key, 60);  // 1 minute window
    }
    
    co_return count <= max_requests;
}

// Usage in HTTP handler
srv.get("/api/data", [&](http::request_context& ctx) -> task<void> {
    auto user_id = ctx.header("X-User-ID");
    
    if (!co_await check_rate_limit(redis, user_id, 100)) {
        ctx.resp().set_status(http::status::too_many_requests);
        ctx.resp().json({{"error", "Rate limit exceeded"}});
        co_return;
    }
    
    // Process request...
});
```

## 分布式锁

```cpp
class distributed_lock {
    redis::client& redis_;
    std::string key_;
    std::string value_;
    
public:
    task<bool> acquire(std::chrono::seconds ttl) {
        value_ = generate_unique_id();
        co_return co_await redis_.setnx(key_, value_);
    }
    
    task<void> release() {
        // Only delete if we own the lock
        auto current = co_await redis_.get(key_);
        if (current && *current == value_) {
            co_await redis_.del(key_);
        }
    }
};

// Usage
distributed_lock lock(redis, "resource:123");

if (co_await lock.acquire(30s)) {
    // Critical section
    co_await process_resource();
    co_await lock.release();
} else {
    // Lock not acquired
}
```

## 性能提示

1. **使用 `connection_pool`** 处理并发请求 — RAII handle 自动归还连接
2. **使用管道**在单次往返中处理多个命令
3. **设置适当的 TTL** 以避免内存膨胀
4. **在生产环境使用 SCAN 而不是 KEYS**
5. **尽可能批量操作**
6. **使用 Redis Cluster** 进行水平扩展
7. **使用 INFO 命令监控内存使用**

## 下一步

- **[HTTP 服务器](http.md)** - 缓存 HTTP 响应
- **[MySQL](mysql.md)** - 缓存数据库查询
- **[中间件](../middleware/cache.md)** - HTTP 缓存中间件
