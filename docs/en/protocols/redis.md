# Redis

Async Redis client with connection pool for caching, pub/sub, and data structures.

## Basic Usage

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

## Connection Pool

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

// Start pool (establishes initial connections + background PING health check)
spawn(ctx, pool.async_run());

task<void> use_pool(redis::connection_pool& pool) {
    // Borrow a connection (RAII — auto-returned on destruction)
    auto conn = co_await pool.async_get_connection();
    if (!conn.valid()) {
        // All connections busy and pool is at max_size
        co_return;
    }
    
    // Use via operator-> (same as redis::client)
    co_await conn->set("key", "value");
    auto val = co_await conn->get("key");
    
    // conn returned to pool when destroyed
}
```

### Pool Parameters

`redis::pool_params` mirrors `redis::connect_options` with additional pool settings:

- **Connection**: `host`, `port`, `password`, `username`, `db`, `resp3`
- **Pool size**: `initial_size` (default 1), `max_size` (default 16)
- **Timeout**: `connect_timeout` (10s), `retry_interval` (30s), `ping_interval` (5min)
- **TLS**: `tls`, `tls_verify`, `tls_ca_file`, `tls_cert_file`, `tls_key_file`, `tls_sni`

### Pool Lifecycle

1. `async_run()` — Creates `initial_size` connections, then enters background PING health check loop
2. `async_get_connection()` — Returns idle connection, or creates new one (up to `max_size`), or suspends on waiter queue
3. `cancel()` — Closes all connections and stops the pool

### TLS with Connection Pool

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

## String Operations

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

## List Operations

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

## Set Operations

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

## Hash Operations

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

## Sorted Set Operations

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

## Key Operations

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

## Pub/Sub

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

## Pipeline

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

## Caching Pattern

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

## Session Store

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

## Rate Limiting

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

## Distributed Lock

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

## Performance Tips

1. **Use `connection_pool`** for concurrent requests — RAII handles auto-return connections
2. **Use pipeline** for multiple commands in a single round-trip
3. **Set appropriate TTL** to avoid memory bloat
4. **Use SCAN instead of KEYS** in production
5. **Batch operations** when possible
6. **Use Redis Cluster** for horizontal scaling
7. **Monitor memory usage** with INFO command

## Next Steps

- **[HTTP Server](http.md)** - Cache HTTP responses
- **[MySQL](mysql.md)** - Cache database queries
- **[Middleware](../middleware/cache.md)** - HTTP cache middleware
