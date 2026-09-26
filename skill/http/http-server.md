# HTTP Server

> 高性能异步 HTTP/HTTPS 服务器栈，支持 HTTP/1.1、HTTP/2、HTTP/3、路由、中间件、SSE、Swagger 与文件上传。

**import**: `import cnetmod.protocol.http;`
**CMake**: `-DCNETMOD_ENABLE_HTTP=ON`
**源码**: `src/protocol/http/`

## 场景导航
- 我要启动一个 HTTP 服务器 → [看这里](#创建并启动服务器)
- 我要注册路由 → [看这里](#路由注册)
- 我要处理请求参数 → [看这里](#request_context--请求访问)
- 我要返回 JSON/HTML/文本 → [看这里](#response--响应构建)
- 我要先返回 202、再运行后台任务 → [参见 Application 托管任务](../infra/application.md#http-先响应后台继续执行)
- 我要推送实时事件 (SSE) → [看这里](#sse-server-sent-events)
- 我要生成 API 文档 → [看这里](#swaggeropenapi-文档)
- 我要处理文件上传 → [看这里](#multipartform-data--文件上传)
- 我要设置 Cookie → [看这里](#cookie-处理)
- 我要启用 HTTP/2 → [看这里](#http2-支持)
- 我要启用 HTTP/3 / QUIC → [看这里](#http3--quic-支持)
- 我要升级为 WebSocket → [参见 websocket.md](websocket.md)

## API 参考

### 创建并启动服务器

#### `server::server`
**签名**: `explicit server(io_context& ctx)` / `explicit server(server_context& sctx)`
**参数**:
- `ctx` — 单线程 I/O 上下文
- `sctx` — 多核服务器上下文（多线程模式）

#### `server::listen`
**签名**: `auto listen(std::string_view host, std::uint16_t port, socket_options opts = {.reuse_address = true}) -> std::expected<void, std::error_code>`
**参数**:
- `host` — 监听地址（如 `"0.0.0.0"`）
- `port` — 监听端口
- `opts` — 套接字选项

#### `server::set_router`
**签名**: `void set_router(router r)`

#### `server::use`
**签名**: `void use(middleware_fn mw)`
**说明**: 添加中间件，按调用顺序构成中间件管道。

#### `server::run`
**签名**: `auto run() -> task<void>`
**说明**: 启动接受循环，开始处理连接。

#### `server::stop`
**签名**: `void stop()`
**生命周期**: 在 accept 事件循环上调用，取消挂起的 accept。调用后必须继续运行
事件循环，直到持有的 `run()` task 完成，才能销毁 server/事件循环。
`stop()` 不等于连接排空；已经接受的连接仍须独立等待完成，不能调用后立即停止 I/O。
连接超限时，接收循环发送 429 后半关闭发送方向，并读取丢弃对端剩余数据直到 EOF。
发送和收尾共用 1 秒取消预算，`stop()` 可提前取消。等待 I/O 与定时器结束后才关闭
socket、复用接收取消令牌。这避免立即关闭未读请求使 Windows 客户端丢失 429。
收尾当前占用接收循环，超限对端不关闭时最多消耗上述预算；不改变正常获准连接的路径。

获准连接使用 `spawn_guarded` 派发：未被中间件处理的连接异常会结束该连接，记录错误
类别和数值，不记录异常文本，也不再经 detached promise 终止进程。包装协程创建前的
分配失败仍可向接收循环传播。此隔离机制与 OTEL 开关无关，不替代连接任务的显式等待
或 handler 的取消契约。

#### `server::abort_connections`
**签名**: `void abort_connections() noexcept`
**说明**: 停止接收后，可中止现有及已投递连接的 socket 读写。此操作对该 server
实例不可撤销，可重复调用；采用 socket shutdown，不销毁挂起的协程。
必须继续运行 worker 事件循环直到连接完成。它不能取消 handler 内任意非 socket
等待，也不能替代应用层取消协议。`active_connections()` 包含已投递但尚未执行的连接。

#### `server::set_max_connections`
**签名**: `void set_max_connections(std::size_t n)`

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

    router r;
    r.get("/", [](request_context& ctx) -> task<void> {
        ctx.text(status::ok, "Hello cnetmod!");
        co_return;
    });

    server srv(*ctx);
    auto result = srv.listen("0.0.0.0", 8080);
    if (!result) {
        std::println("Listen failed: {}", result.error().message());
        return 1;
    }
    srv.set_router(std::move(r));

    spawn(*ctx, srv.run());
    ctx->run();
}
```

---

### 路由注册

#### `router::get / post / put / del / patch / any`
**签名**:
```cpp
auto get(std::string_view pattern, handler_fn fn,
    endpoint_metadata metadata = {}) -> router&;
// post / put / del / patch / any 同形；stream_* 与 sse_* 的元数据参数位于选项之后
auto endpoints() const -> std::vector<std::shared_ptr<const endpoint>>;
```
**参数**:
- `pattern` — 路由模式，支持 `:name` 命名参数和 `*filepath` 通配符
- `fn` — 处理函数 `std::function<task<void>(request_context&)>`
- `metadata` — 端点策略，按类型存取（同类型后加覆盖先加）。标准类型：
  `allow_anonymous`、`optional_authentication`、`required_permissions{all_of, any_of}`、
  `endpoint_name{"orders.list"}`；应用可添加任意自定义类型

路由在中间件之前完成匹配；中间件与 handler 通过 `request_context::endpoint()` 读取匹配的
`endpoint{method, pattern, name, metadata}`，未匹配时为空指针。

服务器会区分“路径不存在”和“方法不匹配”：没有任何模式匹配时返回 `404`；路径模式存在但
当前方法未注册时自动返回 `405 Method Not Allowed`，并生成 `Allow` 响应头。业务控制器不应
为同一路径补一个手写的兜底路由。`router::allowed_methods(path)` 可用于测试或自定义调度。
默认的 404/405 响应体是纯文本；应用用 `router::on_unmatched(handler)` 输出自己的错误响应格式，
handler 收到 `unmatched_request{status, allowed}`，405 的 `Allow` 头已由服务器设置：

```cpp
routes.on_unmatched([](request_context& request, const unmatched_request& unmatched)
    -> task<void> {
    respond_error(request, unmatched.status == status::not_found
        ? error_code::route_not_found : error_code::method_not_allowed);
    co_return;
});
```

```cpp
routes.get("/orders/:id", read_order, endpoint_metadata{
    required_permissions{.all_of = {"orders:read"}}, endpoint_name{"orders.read"}});
routes.post("/login", login, endpoint_metadata{allow_anonymous{}});
```

**路由模式说明**:
| 模式 | 示例路径 | 说明 |
|------|---------|------|
| `/api/users` | `/api/users` | 精确匹配 |
| `/api/users/:id` | `/api/users/42` | 命名参数 |
| `/api/users/:id/posts/:pid` | `/api/users/7/posts/99` | 多命名参数 |
| `/static/*filepath` | `/static/css/main.css` | 通配符 |

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

router r;

r.get("/api/users/:id", [](request_context& ctx) -> task<void> {
    auto id = ctx.param("id");
    ctx.json(status::ok,
        std::format(R"({{"id":{},"name":"User_{}"}})", id, id));
    co_return;
});

r.post("/api/echo", [](request_context& ctx) -> task<void> {
    ctx.text(status::ok, std::format("Echo: {}", ctx.body()));
    co_return;
});

r.del("/api/users/:id", [](request_context& ctx) -> task<void> {
    auto id = ctx.param("id");
    ctx.json(status::ok, std::format(R"({{"deleted":{}}})", id));
    co_return;
});

// 通配符路由
r.get("/static/*filepath", [](request_context& ctx) -> task<void> {
    auto path = ctx.wildcard();
    ctx.text(status::ok, std::format("File: {}", path));
    co_return;
});
```

---

### `request_context` — 请求访问

#### `request_context::method`
**签名**: `[[nodiscard]] auto method() const noexcept -> std::string_view`
**返回**: HTTP 方法字符串（如 `"GET"`, `"POST"`）

#### `request_context::path`
**签名**: `[[nodiscard]] auto path() const noexcept -> std::string_view`
**返回**: 请求路径（不含查询字符串）

#### `request_context::query_string`
**签名**: `[[nodiscard]] auto query_string() const noexcept -> std::string_view`

#### `request_context::uri`
**签名**: `[[nodiscard]] auto uri() const noexcept -> std::string_view`
**返回**: 完整 URI（含路径和查询字符串）

#### `request_context::param`
**签名**: `[[nodiscard]] auto param(std::string_view name) const noexcept -> std::string_view`
**参数**: `name` — 路由中 `:name` 定义的参数名

#### `request_context::wildcard`
**签名**: `[[nodiscard]] auto wildcard() const noexcept -> std::string_view`

#### `request_context::get_header`
**签名**: `[[nodiscard]] auto get_header(std::string_view key) const -> std::string_view`

#### `request_context::headers`
**签名**: `[[nodiscard]] auto headers() const noexcept -> const header_map&`

#### `request_context::body`
**签名**: `[[nodiscard]] auto body() const -> std::string_view`

#### `request_context::parse_form`
**签名**: `[[nodiscard]] auto parse_form() -> std::expected<const form_data*, std::error_code>`
**说明**: 解析 `multipart/form-data` 或 `application/x-www-form-urlencoded` 请求体。

#### `request_context::resp`
**签名**: `[[nodiscard]] auto resp() noexcept -> response&`
**说明**: 获取底层 response 对象，用于高级操作（如设置 trailer、自定义 header）。

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto handler = [](request_context& ctx) -> task<void> {
    auto method = ctx.method();           // "POST"
    auto path   = ctx.path();             // "/api/data"
    auto query  = ctx.query_string();     // "page=1&size=10"
    auto id     = ctx.param("id");        // 路由参数
    auto token  = ctx.get_header("Authorization");
    auto body   = ctx.body();

    ctx.json(status::ok, R"({"ok":true})");
    co_return;
};
```

---

### `response` — 响应构建

#### `request_context::text`
**签名**: `void text(int status_code, std::string_view text_body)`

#### `request_context::json`
**签名**: `void json(int status_code, std::string_view json_body)`

#### `request_context::html`
**签名**: `void html(int status_code, std::string_view html_body)`

#### `request_context::redirect`
**签名**: `void redirect(std::string_view location, int code = 302)`

#### `request_context::not_found`
**签名**: `void not_found()`

#### `response::set_cookie`
**签名**:
```cpp
auto set_cookie(std::string_view name, std::string_view value,
    std::string_view domain = {}, std::string_view path = "/",
    std::optional<std::chrono::seconds> max_age = std::nullopt,
    bool secure = false, bool http_only = false) -> response&;

auto set_cookie(const cookie& c) -> response&;
```

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

auto handler = [](request_context& ctx) -> task<void> {
    // 设置 Cookie
    ctx.resp().set_cookie("session", "abc123", {}, "/",
        std::chrono::hours(24), false, true);

    ctx.json(status::ok, R"({"logged_in":true})");
    co_return;
};
```

---

### SSE (Server-Sent Events)

**标准入口先选清楚：** 注册时就确定是 SSE 的独立端点，用
`router::sse_get()` / `router::sse_post()`；同一端点在请求期间根据参数选择 JSON
或 SSE，用 `request_context::with_sse()` 包住整段流式处理。两种入口都会启动
`max_duration` 总时长看门狗。不要在普通 `get/post` handler 中仅调用
`sse_begin()` / `sse_send()`，也不要只自行构造 `sse_stream`：这些底层写法仍有
**默认 5 秒的单次写超时**，但**不会启动总时长看门狗**。普通
`request_timeout` 只是 handler 返回后的软检测，不能代替 SSE 的总时限。

#### `request_context::sse_begin`
**签名**: `auto sse_begin(int status_code = status::ok) -> task<bool>`

#### `request_context::sse_started` / `sse_state`
**签名**:
```cpp
[[nodiscard]] auto sse_started() const noexcept -> bool;
[[nodiscard]] auto sse_state() const noexcept -> sse_stream_state;
```
**说明**: `sse_started()` 在开始尝试写出 SSE 响应头时即返回 true，包括部分写入后失败的情况；此后不得回退为普通 JSON 响应。`sse_state()` 区分 `not_started`、`committing`、`open`、`failed` 与 `closed`。

#### `request_context::sse_send`
**签名**: `auto sse_send(std::string_view data, std::string_view event = {}) -> task<bool>`

#### `request_context::sse_json`
**签名**: `auto sse_json(std::string_view json_payload, std::string_view event = {}) -> task<bool>`

#### `request_context::sse_comment` / `sse_heartbeat`
**签名**:
```cpp
auto sse_comment(std::string_view comment) -> task<bool>;
auto sse_heartbeat() -> task<bool>;
```
**说明**: 发送标准 SSE 注释帧。`sse_heartbeat()` 发送 `: keepalive\n\n`，不会被编码成 `data:` 事件。

#### `request_context::sse_done`
**签名**: `auto sse_done() -> task<bool>`

`sse_done()` 不只是发送应用层 `{"done":true}` 事件；在 HTTP/1.1 下还会写出分块正文的
终止标记。客户端收到完整响应后即可结束本次流，不需要等待 keep-alive 连接关闭；同一连接
仍可用于后续 HTTP 请求。

#### `request_context::with_sse`

**签名**:
```cpp
auto with_sse(sse_handler_fn handler,
    sse_stream_options options = {}) -> task<void>;
```

当是否启用 SSE 必须在请求期间决定时，先完成鉴权、参数解析和资源存在性检查；只有确认
进入流式响应后才调用 `with_sse()`。在调用前仍可返回普通 HTTP 4xx/5xx；调用后由框架以
结构化并发同时运行 stream handler 和总时限看门狗，并在 handler 结束时取消、等待看门狗，
不会留下失管协程。

```cpp
routes.post("/chat", [](http::request_context& request) -> task<void> {
    auto session = co_await find_session(request.param("id"));
    if (!session) {
        request.not_found();
        co_return;
    }
    if (request.query_string() != "stream=true") {
        request.json(http::status::ok, render_response(*session));
        co_return;
    }

    co_await request.with_sse(
        [session = std::move(*session)](http::request_context&,
            http::sse_stream& stream) mutable -> task<void> {
            co_await stream.send(render_delta(session), "delta");
            co_await stream.finish();
        },
        {.max_duration = std::chrono::seconds{60},
            .write_timeout = std::chrono::seconds{3}});
});
```

#### `sse_stream`

`sse_stream` 是绑定 `request_context` 的高层流对象，统一管理保守的开始状态、惰性
响应头写出、具名事件、注释、心跳和终止帧。应用负责序列化 payload，框架负责 SSE
帧编码和 socket 写出。

```cpp
routes.sse_post("/chat", [](http::request_context& request,
                            http::sse_stream& stream) -> task<void> {
    auto deltas = stream.callback("delta");
    if (!co_await stream.begin())
        co_return;
    if (!co_await deltas(R"({"text":"first"})"))
        co_return;
    co_await stream.send(R"({"result":"complete"})", "done");
    co_await stream.finish();
}, http::sse_stream_options{
    .max_duration = std::chrono::seconds{60},
    .write_timeout = std::chrono::seconds{3},
});
```

`started()` 在响应头开始提交时即返回 true，包括提交失败；一旦为 true，不得回退普通
HTTP 响应。`callback(event)` 借用流对象，不能超过 route handler、`sse_stream` 或请求
上下文的生命周期。`router::sse_get()` 和 `router::sse_post()` 会为每个请求创建独立流对象，
并在内部复用 `request_context::with_sse()`；它们适用于注册时即可确定为 SSE 的独立端点。
运行时才决定是否流式的同路径接口必须使用 `with_sse()`，不要只构造 `sse_stream`，否则
没有结构化的总时限看门狗。业务封装（例如仅把 `delta/done/error` 映射到
`request_context::sse_send()` 的 writer）也必须在 `with_sse()` 的 handler 内使用；
封装帧格式不等于接入流生命周期管理。Application 的 recover 中间件发现 SSE 已
提交后不会再尝试普通 JSON 响应，而是尽力写出具名 `error` 帧和终止帧；业务可在异常前
自行写出更具体的错误契约。

`finish()` 成功后会同时完成 SSE 语义和 HTTP 响应 framing。前端仍应以 `done` 事件作为
业务完成信号，但不得依赖服务端关闭 TCP/TLS 连接来判断本次响应结束。

`sse_stream_options::max_duration` 限制整条流的绝对生命周期，默认 120 秒；
`write_timeout` 限制每次响应头或事件帧的写出时间，默认 5 秒。二者必须为正数。超时会取消
当前 socket 操作、关闭连接并将状态置为 `failed`。普通 `request_timeout` 中间件检测到 SSE
已提交后不会写入 504；SSE 使用自己的总时限。生产者访问下游时应使用
`request_context::with_deadline()`，让下游操作同样受总时限和请求取消约束。

#### `sse::event`
```cpp
struct event {
    std::string event, data, id;
    std::optional<std::chrono::milliseconds> retry;
    std::string comment;
};
```

#### `sse::prepare`
**签名**: `void prepare(response&, response_options opts = {})`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

r.get("/events", [](request_context& ctx) -> task<void> {
    co_await ctx.with_sse([](request_context&, sse_stream& stream) -> task<void> {
        if (!co_await stream.heartbeat()) co_return;
        for (int i = 0; i < 5; ++i) {
            if (!co_await stream.send(std::format("message {}", i), "update"))
                co_return;
        }
        (void)co_await stream.finish();
    });
});
```

---

### Swagger/OpenAPI 文档

#### `openapi_document`
```cpp
struct openapi_document {
    std::string title = "cnetmod API";
    std::string version = "1.0.0";
    std::string description;
    std::vector<openapi_server> servers;
    std::map<std::string, std::map<std::string, openapi_operation>> paths;
};
```

#### `openapi_json_handler`
**签名**: `[[nodiscard]] auto openapi_json_handler(openapi_document doc) -> handler_fn`
**说明**: 生成返回 OpenAPI JSON 的路由处理器。

#### `swagger_ui_handler`
**签名**: `[[nodiscard]] auto swagger_ui_handler(std::string openapi_url = "/openapi.json", std::string title = "API Docs") -> handler_fn`
**说明**: 生成 Swagger UI HTML 页面的路由处理器。

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

openapi_document doc;
doc.title = "My API";
doc.version = "1.0.0";
doc.servers.push_back({.url = "http://localhost:8080", .description = "dev"});

add_operation(doc, http_method::GET, "/api/users", {
    .tags = {"users"},
    .summary = "List all users",
});

r.get("/openapi.json", openapi_json_handler(std::move(doc)));
r.get("/docs", swagger_ui_handler());
```

---

### HTTP/2 支持

服务器自动检测 TLS 连接上的 ALPN 协商，透明支持 HTTP/2（RFC 9113）。客户端通过 `h2` 前缀连接时，服务器使用 `v2::session` 处理帧和多路复用。

**配置**: 启用 SSL 后，HTTP/2 自动通过 ALPN 协商。无需额外配置。

```cpp
import std;
import cnetmod.protocol.http;

// 服务器同时支持 HTTP/1.1 和 HTTP/2
// curl --http2 -k https://localhost:8443/
// curl --http1.1 -k https://localhost:8443/
```

---

### HTTP/3 / QUIC 支持

HTTP/3（RFC 9114）已经包含完整客户端和服务端，但它不是 TCP `http::server` 上的
另一个 ALPN 分支。HTTP/1.1 与 HTTP/2 共用 TCP/TLS listener；HTTP/3 使用独立的 UDP
listener、QUIC、TLS 1.3 和 ALPN `h3`，因此需要创建 `http::v3::http3_server`。两者可以
绑定同一个数字端口，因为传输协议分别是 TCP 和 UDP。

启用条件：

```text
-DCNETMOD_ENABLE_HTTP=ON
-DCNETMOD_ENABLE_SSL=ON
-DCNETMOD_ENABLE_QUIC=ON
-DCNETMOD_ENABLE_BORINGSSL_QUIC=ON
```

```cpp
import cnetmod.protocol.http.v3.server;

namespace h3 = cnetmod::http::v3;

auto server = h3::make_http3_server(io, tls,
    cnetmod::endpoint{cnetmod::ipv4_address::any(), 8443},
    [](h3::http3_request& request,
        h3::http3_response& response) -> std::error_code {
        response.status = cnetmod::http::status::ok;
        response.body = R"({"protocol":"h3"})";
        return {};
    });
auto started = server->start();
```

HTTP/3 还支持异步 handler、请求/响应流式正文、QPACK 动态表、0-RTT、连接迁移、
Datagram、WebTransport、实验性 Multipath QUIC 和 Path MTU Discovery。完整配置、取消、
背压和生命周期规则参见 [HTTP/3 / QUIC](http3-quic.md)。

---

### multipart/form-data — 文件上传

#### `form_data`
```cpp
class form_data {
    auto field(std::string_view name) const -> std::optional<std::string_view>;
    auto file(std::string_view name) const -> const form_file*;
    auto all_files() const noexcept -> const std::vector<form_file>&;
};
```

#### `multipart_builder`
```cpp
class multipart_builder {
    auto add_field(std::string_view name, std::string_view value) -> multipart_builder&;
    auto add_file(std::string_view field_name, std::string_view filename,
        std::string_view content_type, std::string_view data) -> multipart_builder&;
    [[nodiscard]] auto content_type() const -> std::string;
    [[nodiscard]] auto build() const -> std::string;
};
```

#### `save_upload`
**签名**: `auto save_upload(upload_options opts) -> handler_fn`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

r.post("/upload", [](request_context& ctx) -> task<void> {
    auto form = ctx.parse_form();
    if (!form) {
        ctx.json(status::bad_request, R"({"error":"invalid form"})");
        co_return;
    }
    for (auto& f : (*form)->all_files()) {
        std::println("file: {} ({} bytes)", f.filename, f.size());
    }
    ctx.json(status::ok, R"({"uploaded":true})");
});

// 或使用内置保存处理器
r.post("/save", save_upload({
    .save_dir = "uploads",
    .default_filename = "upload.bin",
    .max_size = 32 * 1024 * 1024,
}));
```

---

### 静态文件服务

#### `serve_dir`
**签名**: `auto serve_dir(static_file_options opts) -> handler_fn`

**示例**:
```cpp
import std;
import cnetmod.protocol.http;

using namespace cnetmod::http;

router r;
r.get("/static/*filepath", serve_dir({
    .root = "./public",
    .index_file = "index.html",
}));
```

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 先 `use()` 注册中间件，再 `set_router()` | 在 `run()` 之后注册路由 |
| 用 `ctx.json()` 返回 JSON | 手动拼接 `Content-Type` header |
| 使用 `save_upload()` 处理大文件上传 | 在 handler 中手动读取整个 body 到内存 |
| SSE 时使用 `sse_begin()`、`sse_send()` 与 `sse_heartbeat()` | `sse_started()` 后回退使用 `ctx.json()` |
| 使用 `co_return` 结束 handler | 忘记 `co_return` 导致未定义行为 |

## 多核服务器部署（生产级用法）

### `server_context` — 多核上下文

**签名**（`cnetmod.executor.pool`）：
```cpp
class server_context {
    explicit server_context(
        unsigned workers = std::thread::hardware_concurrency(),
        unsigned pool_threads = std::thread::hardware_concurrency());

    [[nodiscard]] auto accept_io() noexcept -> io_context&;
    [[nodiscard]] auto next_worker_io() noexcept -> io_context&;
    [[nodiscard]] auto worker_count() const noexcept -> unsigned;
    [[nodiscard]] auto worker_ios() -> std::vector<io_context*>;
    [[nodiscard]] auto pool() noexcept -> thread_pool&;

    template <typename F>
    auto offload(io_context& return_to, F&& fn);

    void spawn_next(task<void> t);
    void run();   // 阻塞：启动 worker 线程，当前线程运行 accept_io
    void stop();
};
```

**架构**：
| 线程 | 角色 | 说明 |
|------|------|------|
| Thread 0（main） | `accept_io()` | 专用 accept 循环，不参与请求处理 |
| Thread 1..N | `next_worker_io()` | 每个 worker 独立 `io_context`，round-robin 分配连接 |
| Thread Pool | `pool()` | cnetmod `thread_pool`，用于 CPU 密集型任务卸载 |

**IOCP 特性**：accept 后的新 socket 尚未关联 IOCP，首次 `async_read/write` 时自动绑定到 worker 的 IOCP。

---

### 多核 HTTP 服务器完整示例

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.tcp;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.access_log;
import cnetmod.protocol.http.middleware.recover;
import cnetmod.protocol.http.middleware.cors;
import cnetmod.protocol.http.middleware.request_id;
import cnetmod.protocol.http.middleware.body_limit;

namespace cn = cnetmod;
namespace http = cnetmod::http;

auto main() -> int {
    cn::net_init net;

    // 创建多核上下文：4 worker 线程 + 4 个 CPU 线程
    constexpr unsigned WORKERS = 4;
    cn::server_context sctx(WORKERS, WORKERS);

    // 构建路由
    http::router router;

    router.get("/", [](http::request_context& ctx) -> cn::task<void> {
        ctx.json(http::status::ok, std::format(
            R"({{"message":"Hello from multi-core!","thread":"{}"}})",
            std::this_thread::get_id()));
        co_return;
    });

    router.get("/api/users/:id", [](http::request_context& ctx) -> cn::task<void> {
        auto id = ctx.param("id");
        ctx.json(http::status::ok, std::format(
            R"({{"id":{},"name":"User_{}"  }})", id, id));
        co_return;
    });

    // CPU 密集型路由：卸载到 cnetmod 线程池
    router.get("/compute/:n", [&sctx](http::request_context& ctx) -> cn::task<void> {
        int n = 30;
        auto n_str = ctx.param("n");
        if (!n_str.empty())
            std::from_chars(n_str.data(), n_str.data() + n_str.size(), n);

        auto io_tid = std::this_thread::get_id();

        // 切换到 cnetmod 线程池执行 CPU 密集计算
        co_await cn::pool_post_awaitable{sctx.pool()};
        auto pool_tid = std::this_thread::get_id();

        // 模拟计算
        std::uint64_t fib = 0, a = 0, b = 1;
        for (int i = 2; i <= n; ++i) { auto c = a + b; a = b; b = c; }
        fib = (n <= 1) ? static_cast<std::uint64_t>(n) : b;

        // 切回 worker io_context 线程响应
        co_await cn::post_awaitable{ctx.io_ctx()};

        ctx.json(http::status::ok, std::format(
            R"({{"n":{},"fibonacci":{},"io_thread":"{}","pool_thread":"{}"}})",
            n, fib, io_tid, pool_tid));
        co_return;
    });

    // 创建多核 HTTP 服务器
    http::server srv(sctx);
    auto listen_r = srv.listen("0.0.0.0", 8080);
    if (!listen_r) {
        std::println("Listen failed: {}", listen_r.error().message());
        return 1;
    }

    // 注册中间件（顺序：recover → access_log → cors → request_id → body_limit）
    srv.use(cn::recover());
    srv.use(cn::access_log());
    srv.use(cn::cors());
    srv.use(cn::request_id());
    srv.use(cn::body_limit(2 * 1024 * 1024));
    srv.set_router(std::move(router));

    // 在 accept_io 上启动 accept 循环
    cn::spawn(sctx.accept_io(), srv.run());

    std::println("Multi-core server on 0.0.0.0:8080 ({} workers)", WORKERS);

    // 阻塞：当前线程运行 accept_io，后台启动 N 个 worker 线程
    sctx.run();
    return 0;
}
```

### Do's & Don'ts（多核模式）
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 使用 `server_context` 构造 `server` 启用多核 | 在多核场景手动创建多个 `server` 实例 |
| CPU 密集任务通过 `pool_post_awaitable` 卸载到 `pool()` | 在 handler 中直接执行耗时计算阻塞 worker |
| 卸载后用 `post_awaitable{ctx.io_ctx()}` 切回 worker | 在 pool 线程上直接调用 `ctx.json()` |
| `spawn(sctx.accept_io(), srv.run())` 启动 accept | 在 worker 线程上运行 accept 循环 |

---

## 参考示例
- `examples/http/hight_http.cpp` — 路由、中间件、文件上传完整示例
- `examples/http/http_demo.cpp` — 底层 HTTP 请求/响应解析
- `examples/http/hight_plus_http.cpp` — 高级功能（Cookie、SSE 等）
- `examples/http/http2_demo.cpp` — HTTP/2 TLS + ALPN 示例
- `skill/http/http3-quic.md` — HTTP/3 / QUIC 客户端与服务端完整指南
- `examples/http/websocket_upgrade_demo.cpp` — HTTP 升级至 WebSocket
- `examples/http/multicore_http.cpp` — 多核 server_context + pool 卸载完整示例
- `examples/http/tfb_benchmark.cpp` — TechEmpower 基准测试（多核 + 数据库连接池）
