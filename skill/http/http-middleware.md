# HTTP Middleware

> HTTP 中间件管道系统，提供认证、限流、压缩、日志、防火墙等 18 个可组合中间件。

**import**: `import cnetmod.protocol.http.middleware;`
**CMake**: `-DCNETMOD_ENABLE_HTTP=ON`
**源码**: `src/protocol/http/middleware/`

## 场景导航
- 我要了解中间件管道顺序 → [看这里](#中间件管道顺序)
- 我要处理跨域请求 → [cors](#1-cors--跨域)
- 我要做 JWT 认证 → [jwt_auth](#2-jwt_auth--jwt-认证)
- 我要做权限授权 → [authorization](#3-authorization--授权)
- 我要限制请求频率 → [rate_limiter](#4-rate_limiter--速率限制)
- 我要 gzip 压缩响应 → [compress](#5-compress--gzip-压缩)
- 我要限制请求体大小 → [body_limit](#6-body_limit--请求体限制)
- 我要注入请求 ID → [request_id](#7-request_id--请求-id)
- 我要接入 W3C Trace Context / OpenTelemetry → [tracing](#w3c-trace-context--opentelemetry-bridge)
- 我要记录访问日志 → [access_log](#8-access_log--访问日志)
- 我要采集 Prometheus 指标 → [metrics](#9-metrics--指标采集)
- 我要控制请求超时 → [timeout](#10-timeout--超时控制)
- 我要优雅关闭服务 → [graceful_shutdown](#11-graceful_shutdown--优雅关闭)
- 我要设置 IP 防火墙 → [ip_firewall](#12-ip_firewall--ip-防火墙)
- 我要过滤 IP → [ip_filter](#13-ip_filter--ip-过滤)
- 我要缓存响应 → [cache_store / http_cache](#14-cache_store--缓存存储)
- 我要做健康检查 → [health_check](#15-health_check--健康检查)
- 我要处理文件上传 → [upload](#16-upload--文件上传)
- 我要 panic 恢复 → [recover](#17-recover--panic-恢复)

## 中间件类型

```cpp
using handler_fn = std::function<task<void>(request_context&)>;
using next_fn = std::function<task<void>()>;
using middleware_fn = std::function<task<void>(request_context&, next_fn)>;
```

注册方式统一：`server.use(middleware_fn)`。中间件按注册顺序形成洋葱模型管道。

## 中间件管道顺序

**推荐注册顺序**（外层 → 内层）：

```
1. recover         ← 最外层：捕获所有异常
2. request_timeout ← 超时检测（包裹 handler 执行）
3. tracing         ← 解析 traceparent，建立 server span
4. access_log      ← 记录请求/响应日志
5. cors            ← 处理 OPTIONS 预检
6. request_id      ← 注入 X-Request-ID
7. ip_firewall     ← IP 封禁检查（check_middleware）
8. ip_filter       ← IP 黑白名单
9. rate_limiter    ← 频率限制
10. body_limit     ← 请求体大小
11. compress       ← gzip 压缩
12. metrics        ← 指标采集
13. jwt_auth       ← 认证
14. authorization  ← 授权
15. upload         ← 文件上传解析
16. handler        ← 业务逻辑
17. ip_firewall    ← 违规追踪（track_middleware，最内层）
```

```cpp
import std;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware;

using namespace cnetmod;
using namespace cnetmod::http;

server srv(*ctx);
srv.use(recover());
srv.use(request_timeout(std::chrono::seconds{5}));
srv.use(access_log());
srv.use(cors());
srv.use(request_id());
srv.use(body_limit(2 * 1024 * 1024));
srv.use(compress());
srv.use(jwt_auth({.verify = my_verify}));  // 公开路由用 endpoint_metadata 声明
srv.set_router(std::move(r));
```

---

## API 参考

### 1. cors — 跨域

**签名**: `auto cors(cors_options opts = {}) -> http::middleware_fn`

```cpp
struct cors_options {
    std::vector<std::string> allow_origins = {"*"};
    std::vector<std::string> allow_methods = {"GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"};
    std::vector<std::string> allow_headers = {"Content-Type", "Authorization", "X-Request-ID"};
    std::vector<std::string> expose_headers = {"X-Request-ID"};
    bool allow_credentials = false;
    int max_age = 86400;
};
```

**行为**: OPTIONS 预检自动返回 204；其他请求添加 CORS 头后调用 `next()`。

```cpp
srv.use(cors({
    .allow_origins = {"https://app.example.com"},
    .allow_credentials = true,
    .max_age = 3600,
}));
```

### 2. jwt_auth — JWT 认证

**签名**: `auto jwt_auth(jwt_auth_options opts) -> http::middleware_fn`

```cpp
enum class jwt_auth_mode { required, optional, skip };
enum class invalid_optional_credentials { reject, continue_anonymous };
struct jwt_auth_options {
    std::function<bool(std::string_view token)> verify;
    std::function<jwt_auth_mode(const http::request_context&)> mode_for;  // 可选覆盖
    jwt_auth_mode unmatched = jwt_auth_mode::skip;
    invalid_optional_credentials invalid_optional = invalid_optional_credentials::reject;
    std::string header_name = "Authorization";
    std::string token_prefix = "Bearer ";
    std::function<task<std::expected<void, jwt_auth_failure>>(
        http::request_context&, std::string_view)> authenticate_async;
    std::function<void(http::request_context&,
        const jwt_auth_failure&)> on_failure;
};
```

**行为**: 是否需要凭据在注册路由时用端点元数据声明，路由器先匹配路由，中间件再通过
`request_context::endpoint()` 读取（`endpoint_authentication_mode()` 公开同一规则）：

| 匹配到的端点 | 模式 |
|---|---|
| 声明 `http::allow_anonymous` | `skip`：不解析凭据 |
| 声明 `http::optional_authentication` | `optional` |
| 未声明认证元数据 | `required` |
| 没有匹配任何路由 | `unmatched`，默认 `skip`，让路由器返回 404 |

策略按方法区分，同一路径的 `GET` 可以匿名、`DELETE` 可以要求认证。`mode_for` 仅在需要
按完整请求动态决定时覆盖上述规则。

可选认证下，**没有**凭据按匿名继续；**携带**的凭据格式错误、验签失败或回调返回 401 时
默认拒绝（401），避免客户端静默丢失身份——例如带过期令牌的写请求被当作匿名写入。
`invalid_optional = continue_anonymous` 恢复按匿名继续。非 401 的失败（如 Redis 故障导致
的 503）始终拒绝。成功认证时正常绑定身份。

需要异步验签或查询当前用户时，设置 `authenticate_async`；它优先于同步 `verify`，
并可在回调中绑定请求作用域。返回 `jwt_auth_failure{status, message, reason}` 拒绝请求；
`on_failure` 可输出应用自己的错误响应格式，按 `reason`（`missing_credentials`、
`malformed_credentials`、`invalid_credentials`、`expired_credentials`、`unavailable`）
映射到应用错误码，不要匹配 `message` 文本。中间件自身产生的缺失/格式错误会带上对应 reason。

```cpp
router.post("/login", login, http::endpoint_metadata{http::allow_anonymous{}});
router.get("/articles/:id", read_article,
    http::endpoint_metadata{http::optional_authentication{}});
router.del("/articles/:id", remove_article);   // 需要认证

srv.use(jwt_auth({
    .verify = [](std::string_view token) {
        return token == "my-secret-key";
    },
}));
```


### 3. authorization — 授权

**签名**: `auto authorize(authorization_options options) -> middleware_fn`

```cpp
struct authorization_principal {
    std::string subject;
    std::string tenant_id;
    std::vector<std::string> permissions;
};

struct authorization_requirement {
    std::vector<std::string> all_of;  // 必须全部匹配
    std::vector<std::string> any_of;  // 至少匹配一个
};

struct authorization_options {
    principal_authenticator authenticate;               // 必填
    authorization_requirement_resolver requirement_for; // 可选覆盖
    authenticated_principal_sink on_authenticated;
    bool authorize_unmatched = false;
    std::function<void(request_context&, int status, std::string_view code)> on_failure;
};
```

权限要求默认读取端点上的 `http::required_permissions{all_of, any_of}`，支持通配符
（如 `iot:device:*`）；`requirement_for` 仅用于动态覆盖。`allow_anonymous` 端点直接放行；
`optional_authentication` 且未声明权限的端点允许匿名；未匹配路由默认放行（404），
`authorize_unmatched = true` 时强制认证。认证失败返回 401，`verifier_failure` 返回 503，
权限不足返回 403。`on_failure` 可输出应用自己的错误响应格式，收到状态码与稳定错误码
（`UNAUTHENTICATED`、`FORBIDDEN`、`AUTHENTICATION_UNAVAILABLE`）。`authenticate` 为空时
`authorize()` 抛出 `std::invalid_argument`。

```cpp
router.get("/devices", list_devices, http::endpoint_metadata{
    http::required_permissions{.all_of = {"iot:device:read"}}});

srv.use(authorize({
    .authenticate = [](request_context& ctx)
        -> std::expected<authorization_principal, authorization_error> {
        auto token = ctx.get_header("Authorization");
        if (token.empty())
            return std::unexpected(authorization_error{
                .code = authorization_error_code::unauthenticated});
        return authorization_principal{.subject = "user1",
            .permissions = {"iot:device:read", "iot:device:write"}};
    },
}));
```

### 4. rate_limiter — 速率限制

**签名**: `auto rate_limiter(rate_limiter_options opts = {}) -> http::middleware_fn`

```cpp
struct rate_limiter_options {
    double rate = 10.0;              // 令牌桶速率（req/s）
    double burst = 20.0;             // 突发容量
    std::function<std::string(http::request_context&)> key_fn;
    std::chrono::seconds entry_ttl{300};
};
```

默认按 IP 限流；自定义 `key_fn` 可按用户/API Key 限流。

每次调用 `rate_limiter()` 创建一个独立的进程内状态；返回的中间件及其所有副本共享该状态。
因此把同一个中间件实例安装到多事件循环 HTTP 服务器时，同一 key 使用的是**全进程统一令牌桶**，
不是每个事件循环各一份配额。内部按 key 分片同步，可从不同事件循环并发调用。若需要分路由独立
配额，应分别创建中间件实例；若需要跨进程全局配额，应使用 Redis 等外部原子计数方案。

```cpp
srv.use(rate_limiter({.rate = 100.0, .burst = 200.0}));
```

### 5. compress — gzip 压缩

**签名**: `auto compress(compress_options opts = {}) -> http::middleware_fn`

```cpp
struct compress_options {
    std::size_t min_size = 1024;  // 小于此值不压缩
    int level = 6;                // 压缩级别 1-9
};
```

```cpp
srv.use(compress({.min_size = 512, .level = 6}));
```

### 6. body_limit — 请求体限制

**签名**: `auto body_limit(std::size_t max_bytes = 1024 * 1024) -> http::middleware_fn`

检查 `Content-Length` 头和实际 body 大小，超限返回 413。

```cpp
srv.use(body_limit(8 * 1024 * 1024)); // 8MB
```

### 7. request_id — 请求 ID

**签名**: `auto request_id(std::string_view header_name = "X-Request-ID") -> http::middleware_fn`

请求已有 `X-Request-ID` 则复用（反向代理传入），否则生成 128 位随机 hex。Handler 通过 `ctx.resp().get_header("X-Request-ID")` 读取。

```cpp
srv.use(request_id());
```

### 8. access_log — 访问日志

**签名**:
```cpp
auto access_log(access_log_options opts, std::source_location loc = ...) -> http::middleware_fn;
auto access_log(logger::level lv = logger::level::info, std::source_location loc = ...) -> http::middleware_fn;
```

```cpp
struct access_log_options {
    logger::level lv = logger::level::info;
    access_log_format format = access_log_format::brief;
    access_log_dump dump = access_log_dump::error_only;
    bool log_request_headers = true;
    bool log_request_body = true;
    bool log_response_headers = true;
    bool log_response_body = true;
    std::size_t max_body_bytes = 2048;
    bool redact_sensitive_headers = true;
};
```

```cpp
srv.use(access_log());  // 简单用法
srv.use(access_log({.format = access_log_format::http, .dump = access_log_dump::always}));
```

### 9. metrics — 指标采集

**签名**:
```cpp
auto metrics_middleware(metrics_collector& collector) -> http::middleware_fn;
auto metrics_handler(metrics_collector& collector) -> handler_fn;
```

`metrics_collector` 采集请求总数、状态码分布、延迟直方图、响应字节数。输出 Prometheus 格式。

同时提供 `cnetmod::metrics::registry` 自定义指标：
```cpp
auto openmetrics_middleware(registry& r = global_registry(),
    std::vector<double> buckets = {...}) -> http::middleware_fn;
auto openmetrics_handler(registry& r = global_registry()) -> handler_fn;
```

```cpp
metrics_collector mc;
srv.use(metrics_middleware(mc));
// ...
r.get("/metrics", metrics_handler(mc));
```

### 10. timeout — 超时控制

**签名**: `auto request_timeout(std::chrono::steady_clock::duration max_time) -> http::middleware_fn`

软超时：handler 执行完成后检测耗时，超限则覆盖响应为 504。应放在 `recover` 之后。

```cpp
srv.use(request_timeout(std::chrono::seconds{5}));
```

### 11. graceful_shutdown — 优雅关闭

```cpp
class shutdown_handler {
    void install() noexcept;
    [[nodiscard]] auto is_signaled() const noexcept -> bool;
    [[nodiscard]] auto in_flight() const noexcept -> std::int64_t;
    template <typename SleepFn> auto wait_for_signal(SleepFn sleep_fn) -> task<void>;
    template <typename SleepFn> auto drain(SleepFn sleep_fn, std::chrono::steady_clock::duration timeout) -> task<bool>;
    auto track_middleware() -> http::middleware_fn;
};
```

注册 SIGINT/SIGTERM 信号处理器，等待 in-flight 请求完成。shutdown 后新请求返回 503。

```cpp
shutdown_handler sh;
sh.install();
srv.use(sh.track_middleware());

// 主协程
co_await sh.wait_for_signal([&](auto d) { return async_sleep(*ctx, d); });
co_await sh.drain([&](auto d) { return async_sleep(*ctx, d); }, std::chrono::seconds{5});
srv.stop();
ctx->stop();
```

### 12. ip_firewall — IP 防火墙

**签名**:
```cpp
class ip_firewall {
    explicit ip_firewall(cache::cache_store& store, ip_firewall_options opts = {});
    auto check_middleware() -> http::middleware_fn;    // 链头：封禁检查
    auto track_middleware() -> http::middleware_fn;    // 链尾：违规追踪
    auto report_violation(std::string_view ip, int weight = 1) -> task<void>;
    auto ban(std::string_view ip) -> task<void>;
    auto unban(std::string_view ip) -> task<void>;
    auto is_banned(std::string_view ip) -> task<bool>;
};
```

```cpp
struct ip_firewall_options {
    int max_violations = 10;
    std::chrono::seconds violation_window{300};
    std::chrono::seconds ban_duration{3600};
    bool track_4xx = true;
    bool track_5xx = false;
    bool track_rate_limit = true;
};
```

需要 `cache_store` 后端（`memory_cache` 或 `redis_cache`）。

```cpp
cache::memory_cache store({.max_entries = 50000});
ip_firewall fw(store, {.max_violations = 10});
srv.use(fw.check_middleware());  // 链头
// ... 其他中间件和路由 ...
srv.use(fw.track_middleware());  // 链尾
```

管理 API handler：`firewall_status_handler(fw)`, `firewall_ban_handler(fw)`, `firewall_unban_handler(fw)`。

### 13. ip_filter — IP 过滤

**签名**: `auto ip_filter(ip_filter_options opts = {}) -> http::middleware_fn`

```cpp
struct ip_filter_options {
    std::vector<std::string> allow_list;
    std::vector<std::string> deny_list;
    std::vector<std::string> trusted_proxies;
    int denied_status = http::status::forbidden;
};
```

```cpp
srv.use(ip_filter({
    .allow_list = {"127.0.0.1", "10.0.0.0/8"},
    .deny_list = {"192.168.1.100"},
}));
```

### 14. cache_store — 缓存存储

抽象接口 `cache::cache_store`，具体实现：
- `memory_cache` — 内存 LRU 缓存
- `redis_cache` — Redis 后端（需 `CNETMOD_HAS_PROTOCOL_REDIS`）

```cpp
class memory_cache : public cache_store {
    explicit memory_cache(memory_cache_options opts = {});
    auto get/set/del/exists(...) -> task<...>;
};
```

**HTTP 缓存中间件**:
```cpp
auto make_cache_middleware(cache_store& store, global_cache_options opts = {},
    cache_group_registry* registry = nullptr) -> http::middleware_fn;
```

Per-route 缓存：`cacheable()`, `cache_put()`, `cache_evict()`, `cache_evict_group()`。

```cpp
cache::memory_cache store({.max_entries = 10000});
srv.use(cache::make_cache_middleware(store, {.ttl = std::chrono::seconds{60}}));
```

### 15. health_check — 健康检查

**签名**:
```cpp
auto health_check() -> handler_fn;
auto health_check(std::function<health_status()> check_fn) -> handler_fn;
auto readiness_check(std::function<health_status()> check_fn) -> handler_fn;
auto readiness_check(std::vector<std::pair<std::string, std::function<health_status()>>> checks) -> handler_fn;
```

```cpp
r.get("/health", health_check());
r.get("/ready", readiness_check({
    {"database", [&db]() -> health_status {
        return {db.is_connected(), db.connected() ? "ok" : "disconnected"};
    }},
    {"redis", [&redis]() -> health_status {
        return {redis.ping(), "ok"};
    }},
}));
```

### 16. upload — 文件上传

**签名**: `auto upload(upload_config cfg = {}) -> http::middleware_fn`

```cpp
struct upload_config {
    std::size_t max_file_size = 10 * 1024 * 1024;  // 10MB
    std::size_t max_total_size = 0;
    std::size_t max_files = 0;
    std::size_t max_fields = 0;
    std::vector<std::string> allowed_types;
    std::vector<std::string> allowed_exts;
};
```

自动解析 `multipart/form-data` 请求体，结果可通过 `ctx.parse_form()` 访问。

```cpp
srv.use(upload({.max_file_size = 5 * 1024 * 1024, .allowed_exts = {".jpg", ".png"}}));
```

### 17. recover — panic 恢复

**签名**: `auto recover(recover_options opts = {}) -> http::middleware_fn`

```cpp
struct recover_options {
    bool log_body = false;
    std::size_t max_body_bytes = 512;
    bool allow_env_override = true;
};
```

捕获 handler 中的异常，返回 500 而不是崩溃。**应始终放在中间件链最外层**。

```cpp
srv.use(recover());
```

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| `recover()` 放在最外层 | 不放 recover 导致异常崩溃 |
| `timeout` 在 `recover` 之后 | timeout 在 recover 之前（异常无法捕获） |
| `ip_firewall` 分两段注册（check + track） | 只在链头注册 check 忘记链尾 track |
| `body_limit` 在 `upload` 之前 | upload 在 body_limit 之前（大文件先解析后拒绝） |
| 用 `health_check()` 做 K8s 探针 | 在探针 handler 中做重计算 |

## 参考示例
- `examples/http/hight_http.cpp` — 中间件链完整示例（recover + access_log + cors + request_id + body_limit）
- `examples/http/http2_demo.cpp` — HTTP/2 + 中间件组合
## W3C Trace Context / OpenTelemetry bridge

Use the optional tracing middleware when an HTTP service needs standard
`traceparent` propagation. It creates one server span, preserves an incoming
trace ID, generates a fresh span ID, and exposes a completed-span callback for
an application OpenTelemetry exporter. No tracing work is performed unless the
middleware is installed.

```cpp
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.tracing;

namespace trace = cnetmod::http::tracing;

server.use(trace::tracing_middleware({
    .on_end = [](const trace::completed_span& span) {
        // Forward span to the application's OpenTelemetry exporter or logger.
        // Never throw from this callback.
    },
}));

router.get("/orders/:id", [&client](cnetmod::http::request_context& ctx)
    -> cnetmod::task<void> {
    cnetmod::http::request upstream(cnetmod::http::http_method::GET,
        "https://inventory.internal/v1/stock");

    if (auto current = trace::context_from(ctx)) {
        auto child = trace::child_context(*current);
        trace::inject(upstream, child);
    }
    auto response = co_await client.send(upstream, ctx.request_deadline());
    // Handle response...
});
```

`tracestate` is propagated only when `tracing_options::accept_tracestate` is
enabled (the default). Disable it at an untrusted boundary when vendor state
must not cross that boundary. `baggage` is intentionally not propagated by
default because it needs application-specific allowlists and size limits.

### OTLP/HTTP exporter

`cnetmod.observability.otlp` provides a bounded asynchronous OTLP/HTTP JSON
exporter. `submit()` is lock-free on the request path and never waits for the
collector. During orderly shutdown, stop accepting new spans with `close()`
and await `flush()` before stopping the `io_context`; the latter waits until
already accepted spans have either been exported or accounted for as a failed
batch.

```cpp
import cnetmod.observability.otlp;

cnetmod::observability::otlp_http_exporter exporter{
    context,
    {.endpoint = "http://otel-collector:4318/v1/traces",
     .service_name = "orders-api"},
};

srv.use(trace::tracing_middleware({
    .on_end = [&exporter](const trace::completed_span& span) {
        (void)exporter.submit(span); // bounded queue: drops are counted
    },
}));

// Service shutdown coroutine, before context.stop().
exporter.close();
if (auto flushed = co_await exporter.flush(std::chrono::seconds{5}); !flushed) {
    // Use the application's cnetmod logger to record the timeout/failure.
}
```

The exporter deliberately has no retry loop on the request path. A collector
outage increments `failed_batches`; applications can inspect `statistics()` to
alert on drops or failed deliveries without amplifying an outage with retries.

### HTTP → SQL / Redis / gRPC trace chain

Trace context is an explicit value rather than thread-local state, so it is
safe across coroutine worker migration. Pass the handler's context to each
downstream operation; SQL and Redis create local child spans and report them to
the same bounded exporter, while gRPC also injects W3C headers for the remote
service.

```cpp
if (auto parent = trace::context_from(ctx)) {
    auto report = [&exporter](const trace::completed_span& span) {
        (void)exporter.submit(span);
    };

    auto user = co_await session.query(
        "SELECT id, name FROM users WHERE id = ?", *parent, report);
    auto cached = co_await redis.cmd({"GET", "user:42"}, *parent, report);

    grpc::unary_request rpc{/* ... */};
    auto profile = co_await grpc_client.unary(std::move(rpc), *parent);
}
```

SQL and Redis do not define a `traceparent` wire field, so their child spans
remain local telemetry. gRPC transmits the child `traceparent` and
`tracestate`; HTTP uses `trace::inject()` for the same purpose. Exporter
callbacks are isolated from business results: an exception in a callback is
ignored and never changes the database or Redis operation outcome.
