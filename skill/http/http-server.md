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
**签名**: `auto listen(std::string_view host, std::uint16_t port, socket_options opts = {.reuse_address = true}, int backlog = 4096) -> std::expected<void, std::error_code>`
**参数**:
- `host` — 监听地址（如 `"0.0.0.0"`）
- `port` — 监听端口
- `opts` — 套接字选项
- `backlog` — 等待 `accept` 的内核连接队列长度；必须为正，最终值可被操作系统上限截断

Application 项目用 `http.backlog` 配置业务端口、用 `management.backlog` 配置独立管理端口；
默认均为 4096，也可通过 `CNETMOD_HTTP_BACKLOG` 覆盖业务端口。`max_connections`
限制的是已经接纳的并发连接，与 backlog 不是同一个概念。

HTTP accept 循环收到首个异步完成后，会限量排空最多 64 个已经排队的连接，再回到
事件循环公平点。Linux 使用 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)`，Windows 使用
非阻塞监听 socket 的 `accept`；没有排队连接时不会自旋。不要在业务层再创建并行
accept 循环或重复实现这一批处理。

在支持 provided buffer ring 与 multishot recv 的 io_uring 内核上，框架会为每个
事件循环创建一份跨协议共享的接收服务：注册缓冲区归事件循环所有，连接只持有轻量 receiver，
不会逐连接注册 buffer ring。能力不可用、ring 注册失败或某条 multishot 接收终止时，
框架自动退回普通 `async_read`；应用层不需要环境变量，也不应自行创建接收适配器。
HTTP/1 明文连接的首次探测缓冲在协议确定后立即释放；普通读取的 8 KiB 回退缓冲只在
共享 multishot 接收不可用时按需分配。不要在 handler 或中间件中再为每个 keep-alive
连接常驻同尺寸接收缓冲，否则高连接数下会把固定内存成本重新放大。

#### `server::set_router`
**签名**: `void set_router(router r)`

#### `server::set_h2_sync_adapter`
**签名**:
```cpp
void set_h2_sync_adapter(v2::sync_server_handler handler,
    v2::buffered_request_filter filter);
```
在 `run()` 前配置完整 HTTP/2 请求的同步协议适配器。回调返回 `std::nullopt`
时继续普通 HTTP 路由；filter 决定小请求体的缓冲资格，回调仍需检查请求是否适用。
存在 middleware 或流式请求体时使用普通分发链。handler 和 filter 必须同时提供或同时清空。

`v2::server_response` 可用 `static_headers` / `static_trailers` 复用固定协议元数据：
`v2::static_header_fields::from<fields>()` 只接受静态存储期的 const 字段数组，
防止把请求栈上的头部视图交给异步发送器。业务响应体和动态 metadata 仍由响应拥有。
发送端分别扣减连接和流窗口；额度不足的响应等待 WINDOW_UPDATE，SETTINGS 缩窗可产生负额度。
`v2::server_response::automatic_content_length` 默认为 true；设为 false 时只关闭自动生成
Content-Length，显式提供的长度头仍然发送。DATA 和 END_STREAM 仍按完整响应编码。

每个 HPACK 解码方向最多复用一个不改变动态表的头块（编码不超过 512 字节、16 个字段、
解码内容不超过 4096 字节）。插入或调整动态表会使该复用失效；敏感字段、Authorization、
Proxy-Authorization、Cookie 和 Set-Cookie 不参与复用。每次请求仍执行 HTTP/2 头部语义校验。

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
    auto result = srv.listen("0.0.0.0", 8080,
        {.reuse_address = true}, 8192);
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

不需要等待异步操作的端点使用 `get_sync / post_sync / put_sync / del_sync /
patch_sync / any_sync`，其 handler 为 `std::function<void(request_context&)>`。在没有
中间件时服务器直接调用它，不创建 handler 或中间件链协程帧；有中间件时框架才按需
适配成 `task<void>`。该语义对 HTTP/1.1 和 HTTP/2 一致；HTTP/2 不会忽略
`sync_handler`。不要为了使用 sync API 在 handler 内执行阻塞 I/O 或 CPU 重任务。

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

**说明**: 首选接口。名称匹配不区分大小写；常用 header 使用固定槽位，其他 header 使用
大小写无关哈希索引，返回值直接引用本次请求的 header backing buffer，不会为名称和值
逐项创建 `std::string`。返回的视图只在当前请求生命周期内有效，不要跨请求保存。

#### `request_context::headers`
**签名**: `[[nodiscard]] auto headers() const -> const header_map&`

**说明**: 兼容需要遍历全部 header 的场景，首次调用时才物化 `header_map`，因此会产生
字符串分配。中间件和 handler 只读取少量已知 header 时必须使用 `get_header()`。

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

#### `response::set_body_source`
**签名**:
```cpp
auto set_body_source(std::shared_ptr<response_body_source> source)
    -> response&;
```

用于文件、对象存储或动态生成的大响应体。传输层会按背压逐块拉取，HTTP/1
明文、HTTP/1 TLS 与 HTTP/2 都走各自的协议写入路径；handler 不得通过
`request_context::raw_socket()` 自行发送 HTTP 字节。`set_body()` 与
`set_body_source()` 互斥，后调用者替换前一种响应体。提供
`content_length` 时框架自动生成 `Content-Length`，并在发送结束时核对实际字节数。

#### `response::generate_body` / `response::transform_body`

**签名**:
```cpp
template <typename Generator>
auto generate_body(Generator&& generator)
    -> std::expected<void, std::error_code>;

template <typename Transformer>
auto transform_body(Transformer&& transformer)
    -> std::expected<bool, std::error_code>;
```

`generate_body()` 让 JSON/DTO 序列化器直接写入 response 持有的可复用字符串，避免先生成
临时字符串再复制到响应体。generator 接收 `std::string&`，失败时框架清空不完整正文并
恢复正确的 `Content-Length`。

`transform_body()` 用于压缩、编码等“读取当前正文并生成替代正文”的场景。transformer
接收 `std::string_view input, std::string& output`，返回 `true` 才提交 output；返回
`false` 保留原正文，错误则原样返回。response 会在持久连接上分别保留原文和变换结果的
缓冲容量。业务 handler 通常只需 `generate_body()`；压缩中间件已经自动使用
`transform_body()`，不要在 handler 中重复压缩。

内容可重复的动态响应还可同时设置 `compress_options::cache_capacity` 与
`compress_options::cache_key`。缓存默认关闭；键必须包含所有会改变响应体的请求维度，
不得只按路由缓存个性化响应。中间件命中后仍核对完整未压缩正文，数据变化会重新压缩，
并以共享不可变正文发送缓存结果。

#### `response::set_shared_body`
**签名**:
```cpp
auto set_shared_body(std::shared_ptr<const std::string> body) -> response&;
auto set_shared_body_preserve_headers(
    std::shared_ptr<const std::string> body) -> response&;
```

`set_shared_body()` 用于跨请求复用不可变响应体，并自动维护 `Content-Length`。
`set_shared_body_preserve_headers()` 只替换响应体、不改动已有头，供框架在已经安装完整的
协议预编码头块时使用；普通业务 handler 不应调用后者，否则容易漏写长度或制造
`Content-Length`/chunked 冲突。`serve_dir()` 会自行选择正确入口。

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

流式请求只在应用实际消费 body chunk 后回补接收窗口；普通和借用式 handler 遵循相同规则。
同一轮消费产生的连接窗口回补会合并为一个 WINDOW_UPDATE，并共享一次后台任务和写入。
不要依赖“一块数据对应一个连接 WINDOW_UPDATE”的帧数量；测试应核对回补字节总量，
并确认尚未消费的数据没有提前获得信用。已结束的流只需要回补连接窗口。

已经携带 `END_STREAM` 的完整请求由连接协程批量分发并合并刷出响应；只有请求正文仍在
到达的流才启动后台流式任务。应用无需为了吞吐手工创建 task 或绕过 router，正文流式
端点继续使用 `stream_*` 路由即可。单个 `HEADERS + END_STREAM` 的常见 GET/RPC 请求会
直接进入就绪队列，不创建 `stream` 映射节点；连接会复用 HPACK 解码后的请求头容量，且在
归还容量前主动清零 `Authorization`、`Proxy-Authorization`、`Cookie` 等凭证值。

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
    .serve_precompressed = true,
    .cache_max_file_size = 1024 * 1024,
    .revalidate_interval = std::chrono::milliseconds{500},
}));
```

`serve_dir()` 使用协议无关的 pull-based response body：文件分块异步读取，
HTTP/1 明文、TLS 和 HTTP/2 由服务器传输层分别编码。不要为了零拷贝或性能在
静态文件 handler 中直接调用 `raw_socket()`；那会绕过 TLS 加密与 HTTP/2
帧编码。默认对不超过 1 MiB 的热点文件保留不可变共享响应体，HTTP/1 与 HTTP/2
不会按请求重复打开、读取或复制文件；超过上限的文件仍按背压异步分块读取。
HTTP/2 缓存命中还会复用不依赖动态表的 HPACK 固定响应头块，避免每个请求重复构造
header map 和编码固定字段；这是静态资源元数据复用，不是缓存动态业务响应。
缓存每 500 ms 重新核对文件大小和修改时间，因此替换或删除静态资源后无需重启。
将 `cache_max_file_size` 设为 `0` 可关闭 body 缓存，将
`revalidate_interval` 设为 `0ms` 可要求每次请求检查磁盘元数据。

`serve_precompressed` 默认开启。客户端的 `Accept-Encoding` 允许时优先选择同目录的
`.br`，其次选择 `.gz`；`q=0` 会明确禁用对应编码，并自动设置
`Content-Encoding` 与 `Vary: Accept-Encoding`。Range 请求固定读取原始文件，避免
把原文件的字节范围错误地应用到压缩表示。

HTTP/3 使用同一磁盘一致性语义，但请求类型不同，因此显式导入
`cnetmod.protocol.http.v3.static_files`：

```cpp
import std;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.static_files;

auto static_files = cnetmod::http::v3::serve_dir({
    .root = "/srv/public",
    .url_prefix = "/static/",
    .serve_precompressed = true,
});

cnetmod::http::v3::server_request_handler handler =
    [static_files = std::move(static_files)](auto& request, auto& response)
        -> std::error_code
{
    auto served = static_files(request, response);
    if (!served)
        return served.error();
    if (*served)
        return {};
    // 继续处理其他业务路由。
    response.status = cnetmod::http::status::not_found;
    return {};
};
```

该入口属于框架正式能力，不会在应用入口预读整个目录，也不会持有不校验磁盘的
永久缓存。HTTP/3 对不超过 `cache_max_file_size`（默认 1 MiB）的文件保留共享内容，
但每次请求都会核对大小与修改时间；替换或删除文件后立即更新或失效。它同样支持
`.br`/`.gz` 预压缩副本、保留原文件的 `Content-Type`，并拒绝编码后的目录穿越。
不要为了 benchmark 在应用层恢复启动时预加载目录。

---

## Do's & Don'ts
| ✅ 正确 | ❌ 错误 |
|---------|---------|
| 先 `use()` 注册中间件，再 `set_router()` | 在 `run()` 之后注册路由 |
| 用 `ctx.json()` 返回 JSON | 手动拼接 `Content-Type` header |
| 用 `ctx.get_header()` 读取已知 header | 为读取单个值调用 `ctx.headers()` 物化整张表 |
| 使用 `save_upload()` 处理大文件上传 | 在 handler 中手动读取整个 body 到内存 |
| 大响应使用 `response_body_source` 或 `serve_dir()` | handler 直接写 `raw_socket()` 绕过 TLS/H2 |
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
| Thread 0（main） | `accept_io()` | 控制循环；Linux 多 worker HTTP 不在这里接纳连接 |
| Thread 1..N | `worker_ios()` | 每个 worker 独立 `io_context`，连接全生命周期归属该循环 |
| Thread Pool | `pool()` | cnetmod `thread_pool`，用于 CPU 密集型任务卸载 |

Linux 多 worker HTTP 为每个 worker 创建 `SO_REUSEPORT` listener，内核把连接直接分配给
最终所属的循环，不再经过控制循环 round-robin 转交；每个循环同时持有自己的 multishot
recv 注册缓冲区。单循环、Windows 和不支持该能力的平台保持原有 listener/dispatch 模型。

worker 数量不是固定等于逻辑核数。HTTP/2/HTTP/3 在一条连接上已经并发复用大量流，过多
worker 会增加 listener、内核唤醒和缓存迁移，却不能把同一连接拆给多个循环。生产环境应按
实际连接数、每连接并发流数和 handler 开销压测选择；CPU 密集工作继续交给 `pool()`，不要
靠增加 I/O worker 掩盖阻塞。对于少量高复用连接，通常从 2–4 个 I/O worker 起测；大量独立
连接再逐步增加。框架不擅自覆盖应用显式传入的 worker 数量。

HTTP/1 解析器会保留同一次 receive 中第一个消息之后的未消费字节，并连续处理完整的
pipelined 请求。普通响应在该 receive 批次内顺序序列化到连接级缓冲区，再一次提交；遇到
SSE、手工流式输出或 chunked 响应时，框架先刷出此前普通响应，保证线上顺序不变。
常见请求头使用固定槽位，只有出现扩展头时才建立回退哈希索引；普通同步路由不会安装未使用
的流式写回调。响应对象只清理本次实际写入的固定头，TLS 小响应也先尝试单次写入，只有平台
返回部分写入时才进入完整写循环。这些均为传输层内部优化，应用不要为它们复制一套 handler。

HTTP/2 的普通同步请求同样走内部借用式路由匹配，不复制已注册的 handler；请求上下文中的
大块动态 `std::string` 响应会把所有权移动到连接写队列，并通过 vectored transport 直接发送
DATA payload；无需为了零拷贝把每个 JSON 响应包装成 `shared_ptr`。不可变静态资源仍使用
`set_shared_body()`，便于跨请求共享同一份内容。小响应继续复制进批量输出缓冲，避免为很小的
payload 增加独立所有者和 gather segment 的管理成本。
SSE、表单、追踪、取消与 request-scope 扩展状态仅在实际调用对应能力时创建。应用仍只使用
公开的 `router::match()`、`request_context` 和 `get_sync()` 等接口，不得依赖这些内部视图，
也不要为了追求吞吐绕过 endpoint metadata 或自行管理请求上下文生命周期。

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
| `spawn(sctx.accept_io(), srv.run())` 启动服务器编排 | 业务代码跨 worker 写另一个循环的 socket |

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
