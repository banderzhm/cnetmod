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
3. access_log      ← 记录请求/响应日志
4. cors            ← 处理 OPTIONS 预检
5. request_id      ← 注入 X-Request-ID
6. ip_firewall     ← IP 封禁检查（check_middleware）
7. ip_filter       ← IP 黑白名单
8. rate_limiter    ← 频率限制
9. body_limit      ← 请求体大小
10. compress       ← gzip 压缩
11. metrics        ← 指标采集
12. jwt_auth       ← 认证
13. authorization  ← 授权
14. upload         ← 文件上传解析
15. handler        ← 业务逻辑
16. ip_firewall    ← 违规追踪（track_middleware，最内层）
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
srv.use(jwt_auth({.verify = my_verify, .skip_paths = {"/", "/login"}}));
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
struct jwt_auth_options {
    std::function<bool(std::string_view token)> verify;
    std::vector<std::string> skip_paths;
    std::string header_name = "Authorization";
    std::string token_prefix = "Bearer ";
};
```

**行为**: 检查 `skip_paths` → 提取 `Authorization` 头 → 去除 `Bearer ` 前缀 → 调用 `verify(token)` → 失败返回 401。

```cpp
srv.use(jwt_auth({
    .verify = [](std::string_view token) {
        return token == "my-secret-key";
    },
    .skip_paths = {"/", "/login", "/register"},
}));
```

辅助函数：`generate_secure_token(std::size_t bytes = 32) -> std::string` 生成 CSPRNG 安全令牌。

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
    principal_authenticator authenticate;
    authorization_requirement_resolver requirement_for;
    authenticated_principal_sink on_authenticated;
    std::function<bool(const request_context&)> skip;
};
```

支持通配符权限匹配（如 `iot:device:*`）。

```cpp
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
    .requirement_for = [](const request_context& ctx)
        -> std::optional<authorization_requirement> {
        return authorization_requirement{.all_of = {"iot:device:read"}};
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
- `examples/http/account_server_demo.cpp` — 认证、授权、防火墙综合示例
