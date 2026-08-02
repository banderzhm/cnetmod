# HTTP Client

> 统一异步 HTTP/HTTPS 客户端，内置 HTTP/1.1 与 HTTP/2 双栈、Cookie 管理、自动重定向与连接池。

**import**: `import cnetmod.protocol.http;`
**CMake**: `-DCNETMOD_ENABLE_HTTP=ON`
**源码**: `src/protocol/http/client/`

## 场景导航
- 我要发送 GET/POST 请求 → [看这里](#快捷请求方法)
- 我要自定义请求头和请求体 → [看这里](#request--请求构建)
- 我要配置超时和 SSL → [看这里](#client_options--配置)
- 我要管理 Cookie → [看这里](#cookie--cookie-管理)
- 我要复用连接（连接池）→ [看这里](#client_pool--连接池)
- 我要发送并发 HTTP/2 请求 → [看这里](#send_batch--http2-并发)
- 我要升级为 WebSocket → [参见 websocket.md](websocket.md)

## API 参考

### `client_options` — 配置

```cpp
struct client_options {
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    bool follow_redirects = true;
    std::size_t max_redirects = 10;
    bool keep_alive = true;
    std::string user_agent = "cnetmod-http-client/1.0";

    // SSL/TLS
    bool verify_peer = true;
    std::string ca_file;
    std::string cert_file;
    std::string key_file;

    // HTTP/2
    http_version_preference version_pref = http_version_preference::http2_preferred;
    std::uint32_t h2_max_concurrent_streams = 100;
    std::uint32_t h2_initial_window_size = 1 * 1024 * 1024;

    // Cookie
    bool enable_cookies = true;
};
```

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `connect_timeout` | 5s | TCP 连接超时 |
| `request_timeout` | 30s | 请求总超时 |
| `follow_redirects` | `true` | 自动跟踪 3xx 重定向 |
| `max_redirects` | 10 | 最大重定向次数 |
| `keep_alive` | `true` | 保持 TCP 连接（HTTP/1.1 Keep-Alive） |
| `verify_peer` | `true` | 验证服务器证书 |
| `version_pref` | `http2_preferred` | HTTP 版本偏好 |

**`http_version_preference` 枚举**:
| 值 | 说明 |
|---|------|
| `http1_only` | 仅使用 HTTP/1.1 |
| `http2_only` | 仅使用 HTTP/2（需 ALPN） |
| `http2_preferred` | 优先 HTTP/2，回退 HTTP/1.1 |
| `http1_preferred` | 优先 HTTP/1.1，接受 HTTP/2 |

---

### 创建客户端

#### `client::client`
**签名**: `explicit client(io_context& ctx, client_options opts = {})`
**说明**: 创建异步 HTTP 客户端，不可拷贝，可移动。

**示例**:
```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

auto main() -> int {
    auto ctx = make_io_context();

    client_options opts;
    opts.connect_timeout = std::chrono::seconds(5);
    opts.request_timeout = std::chrono::seconds(30);
    opts.follow_redirects = true;
    opts.verify_peer = true;
    opts.version_pref = http_version_preference::http2_preferred;

    client http_client(*ctx, opts);

    spawn(*ctx, [&](client& c) -> task<void> {
        auto result = co_await c.get("http://httpbin.org/get");
        if (result) {
            std::println("Status: {}", result->status_code());
            std::println("Body: {}", result->body());
        } else {
            std::println("Error: {}", result.error().message());
        }
    }(http_client));

    ctx->run();
}
```

---

### 快捷请求方法

#### `client::get`
**签名**: `[[nodiscard]] auto get(std::string_view url) -> task<std::expected<response, std::error_code>>`

#### `client::post`
**签名**: `[[nodiscard]] auto post(std::string_view url, std::string_view body) -> task<std::expected<response, std::error_code>>`

#### `client::put`
**签名**: `[[nodiscard]] auto put(std::string_view url, std::string_view body) -> task<std::expected<response, std::error_code>>`

#### `client::delete_`
**签名**: `[[nodiscard]] auto delete_(std::string_view url) -> task<std::expected<response, std::error_code>>`

#### `client::patch`
**签名**: `[[nodiscard]] auto patch(std::string_view url, std::string_view body) -> task<std::expected<response, std::error_code>>`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto fetch_data(client& c) -> task<void> {
    // GET
    auto r1 = co_await c.get("https://api.example.com/users");
    if (r1) std::println("Users: {}", r1->body());

    // POST JSON
    auto r2 = co_await c.post("https://api.example.com/users",
        R"({"name":"Alice","age":30})");
    if (r2) std::println("Created: {}", r2->status_code());

    // DELETE
    auto r3 = co_await c.delete_("https://api.example.com/users/42");
    if (r3) std::println("Deleted: {}", r3->status_code());
}
```

---

### `request` — 请求构建

#### `request::request`
**签名**: `explicit request(http_method method, std::string_view uri, http_version version = http_version::http_1_1)`

#### `request::set_header`
**签名**: `auto& set_header(std::string_view key, std::string_view value)`

#### `request::append_header`
**签名**: `auto& append_header(std::string_view key, std::string_view value)`

#### `request::set_body`
**签名**: `auto& set_body(std::string_view body)` / `auto& set_body(std::string body)`
**说明**: 自动设置 `Content-Length` 头。

#### `client::send`
**签名**:
```cpp
[[nodiscard]] auto send(const request& req) -> task<std::expected<response, std::error_code>>;
[[nodiscard]] auto send(http_method method, std::string_view url, std::string_view body = {})
    -> task<std::expected<response, std::error_code>>;
```

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto custom_request(client& c) -> task<void> {
    request req(http_method::POST, "https://api.example.com/upload");
    req.set_header("Content-Type", "application/json");
    req.set_header("Authorization", "Bearer token123");
    req.set_body(R"({"file":"data.bin"})");

    auto result = co_await c.send(req);
    if (result) {
        std::println("Status: {}", result->status_code());
        std::println("Header: {}", result->get_header("Content-Type"));
    }
}
```

---

### `response` — 响应访问

#### `response::status_code`
**签名**: `[[nodiscard]] auto status_code() const noexcept -> int`

#### `response::body`
**签名**: `[[nodiscard]] auto body() const noexcept -> std::string_view`

#### `response::get_header`
**签名**: `[[nodiscard]] auto get_header(std::string_view key) const -> std::string_view`

#### `response::headers`
**签名**: `[[nodiscard]] auto headers() const noexcept -> const header_map&`

---

### `cookie` — Cookie 管理

#### `cookie` 结构体
```cpp
struct cookie {
    std::string name, value;
    std::string domain, path = "/";
    std::optional<std::chrono::seconds> max_age;
    bool secure = false, http_only = false;
    enum class same_site_policy { none, lax, strict };
    std::optional<same_site_policy> same_site;
};
```

#### `client::set_cookie`
**签名**: `auto& set_cookie(std::string_view name, std::string_view value, std::string_view domain = {}, std::string_view path = "/")`

#### `client::cookies`
**签名**: `auto cookies() -> cookie_jar&`

#### `client::clear_cookies`
**签名**: `auto& clear_cookies()`

#### `cookie_jar::add`
**签名**: `void add(const cookie& c)`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto cookie_demo(client& c) -> task<void> {
    // 快捷设置
    c.set_cookie("session", "abc123", "example.com", "/");

    // 完整控制
    cookie ck;
    ck.name = "token"; ck.value = "xyz";
    ck.domain = "api.example.com";
    ck.secure = true; ck.http_only = true;
    ck.same_site = cookie::same_site_policy::strict;
    c.cookies().add(ck);

    // 请求自动携带 Cookie
    auto r = co_await c.get("https://api.example.com/data");

    // 查看已存储的 Cookie
    for (auto& ck : c.cookies().cookies()) {
        std::println("{}={} (domain: {})", ck.name, ck.value, ck.domain);
    }

    c.clear_cookies();
}
```

---

### `send_batch` — HTTP/2 并发

**签名**: `[[nodiscard]] auto send_batch(std::span<const request> requests) -> task<std::vector<std::expected<response, std::error_code>>>`
**说明**: 对同一来源的请求使用 HTTP/2 多路复用在同一连接上并发发送。HTTP/1.1 回退为顺序发送。

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto batch_demo(client& c) -> task<void> {
    std::vector<request> reqs;
    for (int i = 1; i <= 5; ++i) {
        reqs.emplace_back(http_method::GET,
            std::format("https://api.example.com/users/{}", i));
    }

    auto results = co_await c.send_batch(reqs);
    for (auto& r : results) {
        if (r) std::println("Status: {}", r->status_code());
    }
}
```

---

### `client_pool` — 连接池

**签名**:
```cpp
class client_pool {
    client_pool(io_context& context, client_options options = {}, std::size_t max_idle = 64);
    [[nodiscard]] auto acquire() -> std::unique_ptr<client>;
    void release(std::unique_ptr<client> value);
    void clear() noexcept;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
};
```

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto pool_demo(client_pool& pool) -> task<void> {
    auto c = pool.acquire();
    auto r = co_await c->get("https://api.example.com/health");
    if (r) std::println("Status: {}", r->status_code());
    pool.release(std::move(c));
}
```

---

### WebSocket 升级

#### `client::release_connection`
**签名**: `[[nodiscard]] auto release_connection() -> std::optional<socket>`
**说明**: 释放底层 socket 用于 WebSocket 升级，之后 client 不可再使用。

详见 [websocket.md](websocket.md)。

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 复用同一个 `client` 实例发送多个请求 | 每次请求创建新 `client` |
| 用 `send_batch` 并发请求同域 API | 逐个 `await` 同域请求 |
| 通过 `client_options` 设置超时 | 不设超时导致请求永久挂起 |
| 检查 `std::expected` 的 `error()` | 直接 `*result` 不检查错误 |
| HTTPS 请求启用 `verify_peer` | 生产环境关闭证书验证 |

## 连接池（生产级用法）

### `client_pool` — 完整 API

**签名**（源码 `client_pool.cppm`）：
```cpp
struct client_pool_key {
    std::string host;
    std::uint16_t port{};
    bool tls{};
};

class client_pool {
    client_pool(io_context& context, client_options options = {}, std::size_t max_idle = 64);

    // 通用池：acquire/release 不区分端点
    [[nodiscard]] auto acquire() -> std::unique_ptr<client>;
    void release(std::unique_ptr<client> value);

    // 端点感知池：按 host:port:tls 复用连接
    [[nodiscard]] auto acquire(client_pool_key key) -> std::unique_ptr<client>;
    void release(client_pool_key key, std::unique_ptr<client> value);

    void clear() noexcept;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
};
```

**连接复用策略**：
| 模式 | 说明 |
|------|------|
| 通用 `acquire()` / `release()` | 池内任意空闲 client，适合单端点场景 |
| 端点感知 `acquire(key)` / `release(key, ...)` | 按 `(host, port, tls)` 精确匹配，适合多端点代理网关 |
| HTTP/2 多路复用 | 同一 client 实例自动通过 `send_batch` 在同一连接上并发多个 stream |
| HTTP/1.1 Keep-Alive | `client_options::keep_alive = true`（默认），同一 client 串行复用连接 |

---

### 端点感知连接池示例

```cpp
import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

auto gateway_handler(client_pool& pool) -> task<void> {
    // 请求 user-service
    {
        auto c = pool.acquire({"user-service.internal", 8080, false});
        auto r = co_await c->get("http://user-service.internal:8080/api/users/42");
        if (r) std::println("User: {}", r->body());
        pool.release({"user-service.internal", 8080, false}, std::move(c));
    }

    // 请求 order-service（不同端点，独立连接池）
    {
        auto c = pool.acquire({"order-service.internal", 8081, false});
        auto r = co_await c->get("http://order-service.internal:8081/api/orders");
        if (r) std::println("Orders: {}", r->body());
        pool.release({"order-service.internal", 8081, false}, std::move(c));
    }

    std::println("Idle clients in pool: {}", pool.idle_count());
}

auto main() -> int {
    auto ctx = make_io_context();

    client_options opts;
    opts.connect_timeout = std::chrono::seconds(3);
    opts.request_timeout = std::chrono::seconds(10);
    opts.keep_alive = true;
    opts.version_pref = http_version_preference::http2_preferred;

    // 最多缓存 128 个空闲 client
    client_pool pool(*ctx, opts, 128);

    spawn(*ctx, gateway_handler(pool));
    ctx->run();
    return 0;
}
```

### HTTP/2 多路复用 + 连接池

```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto h2_batch_with_pool(client_pool& pool) -> task<void> {
    auto c = pool.acquire();

    // send_batch 在同一 HTTP/2 连接上并发发送多个请求
    std::vector<request> reqs;
    for (int i = 1; i <= 10; ++i) {
        reqs.emplace_back(http_method::GET,
            std::format("https://api.example.com/items/{}", i));
    }

    auto results = co_await c->send_batch(reqs);
    for (auto& r : results) {
        if (r) std::println("Status: {}, Body: {}", r->status_code(), r->body());
    }

    pool.release(std::move(c));
}
```

### Do's & Don'ts（连接池）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用完毕立即 `release` 归还 client | 长期持有 client 不归还导致池耗尽 |
| 多端点场景使用 `client_pool_key` 精确路由 | 混用不同端点却使用通用 `acquire()` |
| 配合 `send_batch` 对同域请求做 HTTP/2 多路复用 | 对同域请求逐个 `await` 浪费连接 |
| 合理设置 `max_idle` 避免内存浪费 | 设置过大导致大量空闲连接占内存 |
| 生产环境保持 `keep_alive = true` | 每次请求后关闭连接 |

---

## 参考示例
- `examples/http/client_demo.cpp` — GET/POST、HTTP/2、SSL 基础示例
- `examples/http/cookie_demo.cpp` — Cookie 自动管理示例
- `examples/http/cookie_and_chunked_demo.cpp` — Cookie 简化 API 与 chunked 传输

