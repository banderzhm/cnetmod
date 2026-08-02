# gRPC

> 基于 HTTP/2 的高性能 gRPC 框架，支持 unary/streaming 调用、protobuf 编解码、健康检查、反射及服务治理。

**import**: `import cnetmod.protocol.grpc;`
**CMake**: `-DCNETMOD_ENABLE_GRPC=ON`
**依赖**: `cnetmod.protocol.http`、`cnetmod.io.io_context`、`cnetmod.coro.task`
**源码**: `src/protocol/grpc/`

## 场景导航

- 我要做 gRPC 服务端 → [看这里](#场景1grpc-服务端)
- 我要做 gRPC 客户端 → [看这里](#场景2grpc-客户端)
- 我要做流式调用 → [看这里](#场景2grpc-客户端)
- 我要做认证拦截器 → [看这里](#场景3拦截器)
- 我要做服务治理 → [看这里](#场景4governance-治理)

## 核心类型

**`grpc::status_code`** — 标准 gRPC 状态码：`ok(0)`、`cancelled(1)`、`invalid_argument(3)`、`not_found(5)`、`unavailable(14)`、`unauthenticated(16)` 等 17 种

**`grpc::byte_buffer`** — `std::vector<std::byte>` 类型别名

**`grpc::metadata`** — `std::multimap<std::string, std::string>`，gRPC 请求/响应头

**`grpc::status`** — 调用状态：
```cpp
struct status {
    status_code code; std::string message; metadata trailers;
    auto ok() const noexcept -> bool;
};
```

**`grpc::call_kind`** — 调用类型：`unary`、`client_streaming`、`server_streaming`、`bidi_streaming`

**`grpc::compression_algorithm`** — 压缩算法：`identity`、`gzip`

**`grpc::call_options`** — 调用选项：`headers`、`timeout`、`compression`

**`grpc::call_context`** — 服务端调用上下文：`service`、`method`、`path`、`headers`、`timeout`、`started`、`deadline_exceeded()`

**`grpc::unary_request` / `unary_response`** — Unary 请求/响应载体

**`grpc::streaming_request` / `streaming_response`** — Streaming 请求/响应载体（多消息）

## API 参考

### protobuf 编解码

```cpp
// varint 编码
auto encode_varint(std::uint64_t value) -> byte_buffer;
auto decode_varint(std::span<const std::byte> data, std::size_t& pos) -> std::optional<std::uint64_t>;
auto zigzag_encode(std::int64_t value) noexcept -> std::uint64_t;
auto zigzag_decode(std::uint64_t value) noexcept -> std::int64_t;

// 字段序列化
void append_key(byte_buffer& out, std::uint32_t number, wire_type type);
void append_uint64(byte_buffer& out, std::uint32_t number, std::uint64_t value);
void append_string(byte_buffer& out, std::uint32_t number, std::string_view value);
void append_bytes(byte_buffer& out, std::uint32_t number, std::span<const std::byte> value);

// proto schema 解析
auto parse_schema(std::string_view proto_text) -> std::expected<file_def, std::error_code>;
auto decode_message(std::span<const std::byte> data) -> std::expected<std::vector<field>, std::error_code>;
```

### gRPC 帧编解码

```cpp
auto encode_frame(std::span<const std::byte> payload, bool compressed = false) -> std::expected<byte_buffer, std::error_code>;
auto decode_frames(std::span<const std::byte> data) -> std::expected<std::vector<message_frame>, std::error_code>;

// 增量流式解码器
class stream_decoder {
    auto feed(std::span<const std::byte> bytes) -> std::expected<std::vector<message_frame>, std::error_code>;
    auto buffered_bytes() const noexcept -> std::size_t;
};

// 高级编解码器
class message_stream_encoder {
    explicit message_stream_encoder(compression_algorithm compression = compression_algorithm::identity);
    auto encode(std::span<const std::byte> message) -> std::expected<byte_buffer, status>;
};

class message_stream_decoder {
    explicit message_stream_decoder(codec_options options = {});
    auto feed(std::span<const std::byte> bytes) -> std::expected<std::vector<byte_buffer>, status>;
};
```

### `grpc::service_router` — 服务端路由

```cpp
explicit service_router(server_options options);
void add_unary(std::string service, std::string method, unary_handler handler);
void add_client_streaming(std::string service, std::string method, streaming_handler handler);
void add_server_streaming(std::string service, std::string method, server_streaming_handler handler);
void add_bidi_streaming(std::string service, std::string method, streaming_handler handler);
auto make_http_handler() const -> http::handler_fn;
```

**handler 签名**:
```cpp
using unary_handler = std::function<task<std::expected<byte_buffer, status>>(std::span<const std::byte>, const call_context&)>;
using streaming_handler = std::function<task<std::expected<std::vector<byte_buffer>, status>>(std::span<const byte_buffer>, const call_context&)>;
using server_streaming_handler = std::function<task<std::expected<std::vector<byte_buffer>, status>>(std::span<const std::byte>, const call_context&)>;
using server_interceptor = std::function<std::expected<void, status>(const call_context&)>;
```

**`server_options`**: `max_receive_message_bytes`、`max_send_message_bytes`、`accept_gzip`、`interceptors`、`governance`

### `grpc::client` — 客户端

```cpp
explicit client(io_context& ctx, std::string base_url, client_options opts);
auto unary(unary_request req) -> task<std::expected<unary_response, status>>;
auto client_streaming(streaming_request req) -> task<std::expected<unary_response, status>>;
auto server_streaming(unary_request req) -> task<std::expected<streaming_response, status>>;
auto bidi_streaming(streaming_request req) -> task<std::expected<streaming_response, status>>;
void close() noexcept;
```

**`client_options`**: `http`（HTTP/2 选项）、`accept_gzip`、`default_compression`、`request_interceptors`、`response_interceptors`

### 健康检查

```cpp
namespace grpc::health {
    enum class serving_status : std::uint64_t { unknown, serving, not_serving, service_unknown };
    class registry {
        void set(std::string service, serving_status status);
        auto get(std::string_view service) const -> serving_status;
        void set_default(serving_status status);
    };
    void register_service(service_router& router, registry& registry);
}
```

### 反射服务

```cpp
namespace grpc::reflection {
    void install_service(service_router& router, std::vector<std::string> services);
    auto decode_request(std::span<const std::byte> payload) -> std::expected<reflection_request, status>;
    auto encode_list_services_response(std::span<const std::byte> original_request, std::span<const std::string> services) -> byte_buffer;
}
```

### 服务治理（`cnetmod.protocol.grpc.governance.*`）

| 组件 | 类/结构 | 关键 API |
|---|---|---|
| **endpoint** | `endpoint` | `id`, `url`, `weight`, `priority`, `state`, `is_available()` |
| **discovery** | `static_discovery` | `snapshot()`, `replace_snapshot()` |
| **load_balancer** | `load_balancer` | `pick(const static_discovery&) -> std::optional<endpoint>` (加权轮询) |
| **retry** | `retry_policy` | `max_attempts`, `initial_backoff`, `backoff_multiplier`, `allows_retry()`, `backoff_for()` |
| | `retry_budget` | `try_acquire()`, `record_success()`, `available_tokens()` |
| **circuit_breaker** | `circuit_breaker` | `try_acquire()`, `record_success()`, `record_failure()`, `state()` (closed/open/half_open) |
| **admission** | `concurrency_limiter` | `try_acquire() -> std::optional<guard>`, `limit()`, `in_flight()` |
| | `token_bucket` | `try_consume(double tokens)` |
| | `rate_limit_registry` | `set_limit(service, method, rate_limit)`, `try_consume()` |
| **observability** | `call_statistics` | `record_started()`, `record_completed(latency, failed)`, `snapshot()` |
| | `call_statistics_registry` | `for_method(service, method) -> shared_ptr<call_statistics>` |
| **server_policy** | `server_policy` | `begin(call_context) -> expected<call_guard, status>`, `concurrency()`, `rate_limits()`, `statistics()` |
| **governed_client** | `governed_client` | `governed_client(ctx, discovery, balancer, retry, budget, breaker, options)`, `unary(req, idempotent)` |

## 场景 1：gRPC 服务端

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;

auto echo_handler(std::span<const std::byte> payload, const grpc::call_context&)
    -> cnetmod::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    co_return grpc::byte_buffer(payload.begin(), payload.end());
}

int main() {
    auto ctx = cnetmod::make_io_context();
    grpc::service_router grpc_router(grpc::server_options{.accept_gzip = true});
    grpc_router.add_unary("example.Echo", "Say", echo_handler);

    cnetmod::http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    cnetmod::http::server srv(*ctx);
    srv.listen("0.0.0.0", 50051);
    srv.set_router(std::move(router));
    cnetmod::spawn(*ctx, srv.run());
    ctx->run();
}
```

## 场景 2：gRPC 客户端

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;

auto run(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    grpc::client cli(ctx, "http://127.0.0.1:50051");
    auto resp = co_await cli.unary({
        .service = "example.Echo",
        .method = "Say",
        .payload = std::vector<std::byte>{std::byte{'h'}, std::byte{'i'}},
        .timeout = std::chrono::milliseconds(5000),
    });
    if (resp && resp->st.ok()) {
        std::println("got {} bytes", resp->payload.size());
    }
}
```

## 场景 3：拦截器

```cpp
import std;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;

auto require_bearer(std::string token) -> grpc::server_interceptor {
    return [token](const grpc::call_context& call) -> std::expected<void, grpc::status> {
        if (grpc::metadata_value(call.headers, "authorization") != token)
            return std::unexpected(grpc::make_status(grpc::status_code::unauthenticated, "bad token"));
        return {};
    };
}

auto inject_bearer(std::string token) -> grpc::client_request_interceptor {
    return [token](grpc::client_call& call) -> std::expected<void, grpc::status> {
        call.headers.emplace("authorization", token);
        return {};
    };
}
```

## 场景 4：governance 治理

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;
namespace gov = cnetmod::grpc::governance;

void governance_demo(cnetmod::io_context& ctx) {
    gov::static_discovery discovery({
        gov::endpoint("svc-1", "http://10.0.0.1:50051", 3),
        gov::endpoint("svc-2", "http://10.0.0.2:50051", 1),
    });
    gov::load_balancer balancer;
    gov::retry_policy retry{.max_attempts = 3, .retryable_status_codes = {grpc::status_code::unavailable}};
    gov::circuit_breaker breaker(gov::circuit_breaker_config{.failure_threshold = 5});

    gov::governed_client client(ctx, discovery, balancer, retry, nullptr, &breaker);
}
```

## 连接池（生产级用法）

### 替代方案：governed_client 多端点管理

gRPC 基于 HTTP/2 多路复用，单个 `grpc::client` 已复用底层 HTTP/2 连接，**无需传统连接池**。生产环境通过 `governed_client` + `static_discovery` + `load_balancer` 实现多端点负载均衡。

**`governed_client` API**（来自 `governed_client.cppm`）：

```cpp
class governed_client {
    governed_client(io_context& ctx, static_discovery& discovery,
        load_balancer& balancer, retry_policy retry = {},
        retry_budget* budget = nullptr,
        circuit_breaker* breaker = nullptr,
        client_options options = {});
    auto unary(unary_request req, bool idempotent = false)
        -> task<std::expected<unary_response, status>>;
};
```

**生产级配置示例**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.grpc;

namespace grpc = cnetmod::grpc;
namespace gov = cnetmod::grpc::governance;

auto run_production_client(cnetmod::io_context& ctx) -> cnetmod::task<void> {
    // 1. 服务发现 — 配置多后端端点
    gov::static_discovery discovery({
        gov::endpoint("svc-1", "http://10.0.0.1:50051", /*weight=*/3),
        gov::endpoint("svc-2", "http://10.0.0.2:50051", /*weight=*/1),
        gov::endpoint("svc-3", "http://10.0.0.3:50051", /*weight=*/2),
    });

    // 2. 加权轮询负载均衡
    gov::load_balancer balancer;

    // 3. 重试策略
    gov::retry_policy retry{
        .max_attempts = 3,
        .initial_backoff = std::chrono::milliseconds(100),
        .max_backoff = std::chrono::milliseconds(5000),
        .backoff_multiplier = 2.0,
        .jitter = 0.2,
        .retryable_status_codes = {grpc::status_code::unavailable},
    };

    // 4. 重试预算 — 防止重试风暴
    gov::retry_budget budget(gov::retry_budget_config{
        .max_tokens = 10,
        .token_ratio = 1,
    });

    // 5. 熔断器 — 连续失败后熔断
    gov::circuit_breaker breaker(gov::circuit_breaker_config{
        .failure_threshold = 5,
        .success_threshold = 2,
        .open_duration = std::chrono::milliseconds(30'000),
        .half_open_max_requests = 1,
    });

    // 6. 创建治理客户端
    gov::governed_client client(ctx, discovery, balancer,
        retry, &budget, &breaker);

    // 7. 发起调用 — 自动负载均衡 + 重试 + 熔断
    auto resp = co_await client.unary({
        .service = "example.Echo",
        .method = "Say",
        .payload = std::vector<std::byte>{std::byte{'h'}, std::byte{'i'}},
        .timeout = std::chrono::milliseconds(5000),
    });

    if (resp && resp->st.ok())
        std::println("got {} bytes", resp->payload.size());
}
```

## 多核服务器部署

### server_context + http::server 模式

gRPC 运行在 HTTP/2 之上，通过 `http::server(server_context&)` 实现多核部署。

**API 签名**（来自 `http_server.cppm` + `grpc_server.cppm`）：

```cpp
// http::server 多核构造
explicit http::server(server_context& sctx);

// gRPC service_router
explicit service_router(server_options options);
void add_unary(std::string service, std::string method, unary_handler handler);
auto make_http_handler() const -> http::handler_fn;
```

**`server_policy` 服务端治理**（来自 `server_policy.cppm`）：

```cpp
class server_policy {
    explicit server_policy(server_policy_options options = {});
    auto begin(const call_context& context) -> std::expected<call_guard, status>;
    auto concurrency() const noexcept -> const concurrency_limiter&;
    auto rate_limits() noexcept -> rate_limit_registry&;
    auto statistics() noexcept -> call_statistics_registry&;
};

struct server_policy_options {
    std::size_t max_concurrent_calls = SIZE_MAX;
};
```

**生产级多核 gRPC 服务器**：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc;

namespace cn = cnetmod;
namespace grpc = cnetmod::grpc;
namespace gov = cnetmod::grpc::governance;

auto echo_handler(std::span<const std::byte> payload, const grpc::call_context&)
    -> cn::task<std::expected<grpc::byte_buffer, grpc::status>>
{
    co_return grpc::byte_buffer(payload.begin(), payload.end());
}

int main() {
    cn::net_init net;

    // 4 worker 线程
    cn::server_context sctx(4, 4);

    // 服务端治理 — 并发限制 + 速率限制 + 可观测性
    auto policy = std::make_shared<gov::server_policy>(gov::server_policy_options{
        .max_concurrent_calls = 1000,
    });

    // 按方法设置速率限制
    policy->rate_limits().set_limit(
        "example.Echo", "Say",
        gov::rate_limit{.tokens_per_second = 500.0, .burst = 100.0});

    // gRPC 路由器
    grpc::service_router grpc_router(grpc::server_options{
        .max_receive_message_bytes = 4 * 1024 * 1024,
        .max_send_message_bytes = 4 * 1024 * 1024,
        .accept_gzip = true,
        .governance = policy,
    });
    grpc_router.add_unary("example.Echo", "Say", echo_handler);

    // HTTP 路由器挂载 gRPC
    cn::http::router router;
    router.any("/*path", grpc_router.make_http_handler());

    // 多核 HTTP 服务器
    cn::http::server srv(sctx);
    auto lr = srv.listen("0.0.0.0", 50051);
    if (!lr) {
        std::println("listen failed: {}", lr.error().message());
        return 1;
    }
    srv.set_router(std::move(router));

    cn::spawn(sctx.accept_io(), srv.run());

    // 定期输出可观测性统计
    cn::spawn(sctx.accept_io(), [&policy](cn::io_context& io) -> cn::task<void> {
        while (true) {
            co_await cn::async_sleep(io, std::chrono::seconds(30));
            auto snap = policy->statistics().snapshot("example.Echo", "Say");
            if (snap) {
                std::println("Echo/Say: started={} completed={} failed={} inflight={}",
                    snap->started, snap->completed, snap->failed, snap->in_flight);
            }
        }
    }(sctx.accept_io()));

    sctx.run();
}
```

## Do's & Don'ts

| Do | Don't |
|---|---|
| 服务端 handler 检查 `call_context::deadline_exceeded()` | 不要在 handler 中执行阻塞操作 |
| 使用 `request_interceptors` 注入认证头 | 不要硬编码 token 到业务逻辑 |
| 生产环境启用 `accept_gzip` 减少带宽 | 不要设置过大的 `max_message_bytes` |
| 使用 `governed_client` 集成治理 | 不要手动实现重试/熔断逻辑 |
| 注册健康检查和反射便于调试 | 不要忽略 `status_code::unavailable` 重试 |
| 多核部署使用 `http::server(server_context&)` | 不要在单线程 server 上跑 CPU 密集型处理 |
| 配置 `server_policy` 限制并发和速率 | 不要无限制接受请求导致过载 |

## 参考示例

- `examples/grpc/security_interceptor.cpp` — mTLS + Bearer Token 拦截器
