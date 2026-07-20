# Redis Examples

Redis client and connection pool implementations.

## Examples

### redis_client.cpp
Basic Redis client operations:
- **String operations**: GET, SET, INCR, DECR
- **Hash operations**: HGET, HSET, HGETALL, HDEL
- **List operations**: LPUSH, RPUSH, LPOP, RPOP, LRANGE
- **Set operations**: SADD, SREM, SMEMBERS, SISMEMBER
- **Sorted Set operations**: ZADD, ZRANGE, ZREM
- **Key operations**: DEL, EXISTS, EXPIRE, TTL

### redis_pool.cpp
Redis connection pool with MySQL-style architecture:
- **P0**: Per-connection autonomous lifecycle
- **P1**: Demand-driven dynamic scaling
- **P2**: std::deque for stable addresses
- **P4**: Lock-free fast path with atomic operations
- **P6**: Bitmap-based O(1) idle connection lookup
- Automatic reconnection
- Health checking (PING)
- Configurable timeouts

### redis_sharded_pool.cpp
Sharded Redis connection pool for high concurrency:
- **N independent shards**: Reduces lock contention by N times
- **Round-robin shard selection**: Load balancing
- **io_context binding**: Thread affinity
- **Cross-shard connection stealing**: Dynamic load balancing
- Single-context and multi-context modes
- Scalable to hundreds of concurrent operations

## Architecture

### Connection Pool Optimizations

```
P0: Autonomous Lifecycle
┌─────────────────┐
│  Connection 1   │ ──> Health Check Task
│  Connection 2   │ ──> Health Check Task
│  Connection 3   │ ──> Health Check Task
└─────────────────┘

P1: Dynamic Scaling
Idle ──> Busy ──> Create New (if needed)
Busy ──> Idle ──> Shrink (if excess)

P4: Lock-free Fast Path
atomic<size_t> idle_count_
atomic<size_t> busy_count_
Fast path: no mutex for metrics

P6: Bitmap O(1) Lookup
[1][0][1][0][1] ──> Find first 1 in O(1)
```

### Sharded Pool Architecture

```
┌──────────────────────────────────────┐
│         Sharded Pool                 │
├──────────────────────────────────────┤
│  Shard 0  │  Shard 1  │  Shard 2    │
│  (Pool)   │  (Pool)   │  (Pool)     │
├───────────┼───────────┼─────────────┤
│  Conn 1   │  Conn 1   │  Conn 1     │
│  Conn 2   │  Conn 2   │  Conn 2     │
│  Conn 3   │  Conn 3   │  Conn 3     │
└──────────────────────────────────────┘
     ↓           ↓           ↓
  io_ctx 0    io_ctx 1    io_ctx 2
```

## Building

```bash
cd build
cmake --build . --target redis_client
cmake --build . --target redis_pool
cmake --build . --target redis_sharded_pool
```

## Running

```bash
# Start Redis server first
redis-server

# Run examples
./redis/redis_client
./redis/redis_pool
./redis/redis_sharded_pool
```

## Configuration

Update connection parameters in the example files:
```cpp
redis::connection_config config;
config.host = "127.0.0.1";
config.port = 6379;
config.password = "";  // Optional
config.database = 0;
```

## Performance Tips

1. **Use connection pool** for production applications
2. **Use sharded pool** for high-concurrency scenarios (>100 concurrent ops)
3. **Configure pool size** based on workload:
   - `initial_size`: Typical concurrent operations
   - `max_size`: Peak concurrent operations
4. **Enable pipelining** for batch operations
5. **Set appropriate timeouts** based on network latency
