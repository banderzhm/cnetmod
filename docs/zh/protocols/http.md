# HTTP 服务器

使用 cnetmod 的 HTTP/1.1 和 HTTP/2 服务器构建 REST API 和 Web 服务。

## 快速开始

```cpp
import cnetmod;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;
    
    http::server srv(ctx);
    
    // 简单路由
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello, World!");
        co_return;
    });
    
    // JSON 响应
    srv.get("/api/status", [](http::request_context& ctx) -> task<void> {
        ctx.resp().json({
            {"status", "ok"},
            {"timestamp", std::time(nullptr)}
        });
        co_return;
    });
    
    srv.listen(8080);
    std::println("Server running on http://localhost:8080");
    ctx.run();
}
```

## 路由

### 路径参数

```cpp
// /users/:id
srv.get("/users/:id", [](http::request_context& ctx) -> task<void> {
    auto id = ctx.param("id");
    ctx.resp().json({{"user_id", id}});
    co_return;
});

// /posts/:post_id/comments/:comment_id
srv.get("/posts/:post_id/comments/:comment_id",
    [](http::request_context& ctx) -> task<void> {
        auto post_id = ctx.param("post_id");
        auto comment_id = ctx.param("comment_id");
        // ...
        co_return;
    }
);
```

### 通配符路由

```cpp
// 匹配 /files/*（例如 /files/docs/readme.txt）
srv.get("/files/*filepath", [](http::request_context& ctx) -> task<void> {
    auto filepath = ctx.param("filepath");
    // 提供文件...
    co_return;
});
```

### HTTP 方法

```cpp
srv.get("/resource", get_handler);
srv.post("/resource", create_handler);
srv.put("/resource/:id", update_handler);
srv.patch("/resource/:id", patch_handler);
srv.del("/resource/:id", delete_handler);
srv.options("/resource", options_handler);
srv.head("/resource", head_handler);
```

### 路由组

```cpp
auto api = srv.group("/api");
api.get("/users", list_users);
api.post("/users", create_user);
api.get("/users/:id", get_user);
api.put("/users/:id", update_user);
api.del("/users/:id", delete_user);
```

## 请求处理

### 查询参数

```cpp
srv.get("/search", [](http::request_context& ctx) -> task<void> {
    auto query = ctx.query("q");
    auto page = ctx.query("page", "1");  // 默认值
    auto limit = std::stoi(ctx.query("limit", "10"));
    
    // 执行搜索...
    co_return;
});
```

### 头部

```cpp
srv.get("/", [](http::request_context& ctx) -> task<void> {
    auto user_agent = ctx.header("User-Agent");
    auto auth = ctx.header("Authorization");
    
    if (auth.empty()) {
        ctx.resp().set_status(http::status::unauthorized);
        co_return;
    }
    
    // 处理请求...
    co_return;
});
```

### 请求体

```cpp
// JSON 体
srv.post("/api/users", [](http::request_context& ctx) -> task<void> {
    auto json = ctx.json();
    auto name = json["name"].get<std::string>();
    auto email = json["email"].get<std::string>();
    
    // 创建用户...
    ctx.resp().json({{"id", 123}, {"name", name}});
    co_return;
});

// 原始体
srv.post("/upload", [](http::request_context& ctx) -> task<void> {
    auto body = ctx.body();
    // 处理体...
    co_return;
});
```

### 多部分表单数据

```cpp
srv.post("/upload", [](http::request_context& ctx) -> task<void> {
    auto form = ctx.multipart();
    
    // 文本字段
    auto title = form.field("title");
    
    // 文件上传
    auto file = form.file("document");
    if (file) {
        std::println("Uploaded: {} ({} bytes)",
            file->filename, file->content.size());
        
        // 保存文件...
        co_await save_file(file->filename, file->content);
    }
    
    ctx.resp().json({{"status", "uploaded"}});
    co_return;
});
```

## 响应处理

### 状态码

```cpp
ctx.resp().set_status(http::status::ok);  // 200
ctx.resp().set_status(http::status::created);  // 201
ctx.resp().set_status(http::status::no_content);  // 204
ctx.resp().set_status(http::status::bad_request);  // 400
ctx.resp().set_status(http::status::unauthorized);  // 401
ctx.resp().set_status(http::status::not_found);  // 404
ctx.resp().set_status(http::status::internal_server_error);  // 500
```

### 响应体

```cpp
// 纯文本
ctx.resp().set_body("Hello, World!");

// JSON
ctx.resp().json({{"key", "value"}});

// HTML
ctx.resp().html("<h1>Hello</h1>");

// 自定义内容类型
ctx.resp().set_header("Content-Type", "application/xml");
ctx.resp().set_body("<root><item>value</item></root>");
```

### 头部

```cpp
ctx.resp().set_header("Content-Type", "application/json");
ctx.resp().set_header("Cache-Control", "no-cache");
ctx.resp().set_header("X-Custom-Header", "value");
```

### 重定向

```cpp
ctx.resp().redirect("/new-location");  // 302
ctx.resp().redirect("/new-location", http::status::moved_permanently);  // 301
```

### 流式响应

```cpp
srv.get("/stream", [](http::request_context& ctx) -> task<void> {
    ctx.resp().set_header("Content-Type", "text/event-stream");
    ctx.resp().set_header("Cache-Control", "no-cache");
    
    for (int i = 0; i < 10; ++i) {
        ctx.resp().write(std::format("data: {}\n\n", i));
        co_await ctx.resp().flush();
        co_await async_sleep(ctx.io_ctx(), 1s);
    }
    
    co_return;
});
```

## 中间件

### 使用中间件

```cpp
http::server srv(ctx);

// 全局中间件
srv.use(cors_middleware());
srv.use(jwt_auth_middleware());
srv.use(request_id_middleware());
srv.use(access_log_middleware());

// 路由特定中间件
srv.get("/admin", admin_only_middleware(), admin_handler);
```

### 内置中间件

```cpp
// CORS
srv.use(cors({
    .allowed_origins = {"https://example.com"},
    .allowed_methods = {"GET", "POST", "PUT", "DELETE"},
    .allowed_headers = {"Content-Type", "Authorization"},
    .max_age = 3600
}));

// JWT 认证
srv.use(jwt_auth({
    .secret = "your-secret-key",
    .skip_paths = {"/login", "/register"}
}));

// 速率限制
srv.use(rate_limiter({
    .requests_per_second = 100,
    .burst = 200
}));

// 压缩
srv.use(compress({
    .min_size = 1024,
    .level = 6
}));

// 体大小限制
srv.use(body_limit({
    .max_size = 10 * 1024 * 1024  // 10 MB
}));

// 请求 ID
srv.use(request_id());

// 访问日志
srv.use(access_log());

// 指标
srv.use(metrics());

// 超时
srv.use(timeout(30s));

// 恢复（panic 处理器）
srv.use(recover());
```

### 自定义中间件

```cpp
auto my_middleware() -> http::middleware_fn {
    return [](http::request_context& ctx, http::next_fn next) -> task<void> {
        // 处理器之前
        std::println("Request: {} {}", ctx.method(), ctx.path());
        
        // 调用下一个中间件/处理器
        co_await next();
        
        // 处理器之后
        std::println("Response: {}", ctx.resp().status_code());
    };
}

srv.use(my_middleware());
```

## 错误处理

```cpp
srv.get("/api/users/:id", [](http::request_context& ctx) -> task<void> {
    try {
        auto id = std::stoi(ctx.param("id"));
        auto user = co_await db.find_user(id);
        
        if (!user) {
            ctx.resp().set_status(http::status::not_found);
            ctx.resp().json({{"error", "User not found"}});
            co_return;
        }
        
        ctx.resp().json(user->to_json());
    } catch (const std::exception& e) {
        ctx.resp().set_status(http::status::internal_server_error);
        ctx.resp().json({{"error", e.what()}});
    }
});
```

## 静态文件

```cpp
// 从目录提供文件
srv.static_files("/static", "./public");

// 自定义文件处理器
srv.get("/files/*filepath", [](http::request_context& ctx) -> task<void> {
    auto filepath = ctx.param("filepath");
    auto full_path = std::format("./files/{}", filepath);
    
    if (!std::filesystem::exists(full_path)) {
        ctx.resp().set_status(http::status::not_found);
        co_return;
    }
    
    co_await ctx.resp().send_file(full_path);
});
```

## WebSocket 升级

```cpp
srv.get("/ws", [](http::request_context& ctx) -> task<void> {
    if (!ctx.is_websocket_upgrade()) {
        ctx.resp().set_status(http::status::bad_request);
        co_return;
    }
    
    auto ws = co_await ctx.upgrade_to_websocket();
    
    // 处理 WebSocket 连接
    while (true) {
        auto msg = co_await ws.recv();
        if (!msg) break;
        
        co_await ws.send(*msg);  // 回显
    }
});
```

## 多核服务器

```cpp
int main() {
    net_init guard;
    
    // 4 个工作线程
    server_context srv_ctx(4);
    
    http::server srv(srv_ctx.get_io_context());
    
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello from multi-core server!");
        co_return;
    });
    
    srv.listen(8080);
    std::println("Multi-core server running on http://localhost:8080");
    
    srv_ctx.run();  // 阻塞直到停止
}
```

## HTTP 协议版本选择

cnetmod 同时支持 HTTP/1.1 和 HTTP/2。路由器、中间件和 `request_context` 在两个版本之间共享 —— handler 代码与协议版本无关。

协议版本的选择取决于两个因素：**是否配置了 TLS** 和 **ALPN 协商列表**。

### 构建要求

| 功能 | 构建选项 |
|------|----------|
| 仅 HTTP/1.1 | 无额外要求 |
| HTTP/2 (TLS) | `-DCNETMOD_ENABLE_SSL=ON` + nghttp2 子模块 |
| HTTP/2 (明文 h2c) | nghttp2 子模块 |

### 协议检测逻辑

服务器在每个连接上自动检测协议版本，流程如下：

1. **有 TLS**（调用了 `set_ssl_context()`）：TLS 握手完成后，检查 ALPN 协商结果
   - ALPN 选择了 `h2` → 使用 **HTTP/2**
   - 否则 → 使用 **HTTP/1.1**
2. **无 TLS**（明文连接）：读取连接的前几个字节
   - 以 HTTP/2 客户端连接前言（`PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n`）开头 → 使用 **HTTP/2 明文 (h2c)**
   - 否则 → 使用 **HTTP/1.1**

### 场景一：仅 HTTP/1.1（明文）

不配置 TLS，服务器默认以 HTTP/1.1 模式运行。这是最简单的配置。

```cpp
import cnetmod;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;

    http::server srv(ctx);
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("HTTP/1.1 only");
        co_return;
    });

    srv.listen("0.0.0.0", 8080);  // 明文，无 TLS
    spawn(ctx, srv.run());
    ctx.run();
}
```

```bash
curl http://localhost:8080/
```

### 场景二：仅 HTTP/1.1（TLS / HTTPS）

配置 TLS 但 ALPN 只列出 `http/1.1`，这样即使客户端支持 HTTP/2，也只会使用 HTTP/1.1。

```cpp
auto ssl_ctx = ssl_context::server().value();
ssl_ctx.load_cert_file("cert.pem");
ssl_ctx.load_key_file("key.pem");
ssl_ctx.configure_alpn_server({"http/1.1"});  // 只广告 HTTP/1.1

http::server srv(ctx);
srv.listen("0.0.0.0", 8443);
srv.set_ssl_context(ssl_ctx);  // HTTPS，但只有 HTTP/1.1
```

```bash
curl -k https://localhost:8443/     # HTTPS + HTTP/1.1
curl --http2 -k https://localhost:8443/  # 客户端请求 h2，但服务端只支持 h1.1，回退到 HTTP/1.1
```

### 场景三：同时支持 HTTP/2 和 HTTP/1.1（推荐）

ALPN 列表中同时包含 `h2` 和 `http/1.1`。列表顺序决定服务端偏好 —— 排在前面的协议优先级更高。

```cpp
auto ssl_ctx = ssl_context::server().value();
ssl_ctx.load_cert_file("cert.pem");
ssl_ctx.load_key_file("key.pem");

// 优先 h2，不支持 h2 的客户端回退到 http/1.1
ssl_ctx.configure_alpn_server({"h2", "http/1.1"});

http::server srv(*ctx);
srv.listen("0.0.0.0", 8443);
srv.set_ssl_context(ssl_ctx);
```

客户端在 TLS 握手时通过 ALPN 与服务端协商协议。如果双方都支持 `h2`，则使用 HTTP/2；否则回退到 `http/1.1`。

```bash
curl --http2 -k https://localhost:8443/    # 协商到 HTTP/2
curl --http1.1 -k https://localhost:8443/  # 强制 HTTP/1.1
nghttp -v https://localhost:8443/           # 查看 HTTP/2 帧细节
```

### 场景四：仅 HTTP/2（TLS）

ALPN 只列出 `h2`，不支持 HTTP/2 的客户端将无法连接。

```cpp
ssl_ctx.configure_alpn_server({"h2"});  // 只广告 h2
```

```bash
curl --http2 -k https://localhost:8443/    # 正常，协商到 HTTP/2
curl --http1.1 -k https://localhost:8443/  # 失败：ALPN 协商不匹配
```

### 场景五：HTTP/2 明文（h2c）

不配置 TLS 时，如果客户端发送 HTTP/2 客户端连接前言，服务器会自动切换到 HTTP/2 明文模式（h2c）。需要编译时启用 nghttp2。

```cpp
// 无需特殊配置 —— 不设置 ssl_context 即可
http::server srv(ctx);
srv.listen("0.0.0.0", 8080);  // 明文，支持 h2c 和 HTTP/1.1
spawn(ctx, srv.run());
```

```bash
# HTTP/2 明文（h2c，需要 curl 支持 --http2-prior-knowledge）
curl --http2-prior-knowledge http://localhost:8080/

# 普通 HTTP/1.1
curl http://localhost:8080/
```

> **注意**: h2c 通常只用于内部服务或测试环境。生产环境建议使用 TLS + ALPN。

### 完整示例

以下是支持 HTTP/2 + HTTP/1.1 的完整服务器示例：

```cpp
import cnetmod;
import cnetmod.core.ssl;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    auto ctx = make_io_context();

    // TLS 上下文 + ALPN
    auto ssl_ctx = ssl_context::server().value();
    ssl_ctx.load_cert_file("cert.pem");
    ssl_ctx.load_key_file("key.pem");
    ssl_ctx.configure_alpn_server({"h2", "http/1.1"});

    // 路由 —— 与 HTTP/1.1 完全相同
    http::router router;
    router.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello over HTTP/2 or HTTP/1.1!");
        co_return;
    });

    http::server srv(*ctx);
    srv.listen("0.0.0.0", 8443);
    srv.set_router(std::move(router));
    srv.set_ssl_context(ssl_ctx);  // 启用 HTTPS + HTTP/2

    spawn(*ctx, srv.run());
    ctx->run();
}
```

### 版本选择速查表

| 目标 | TLS | ALPN 配置 | 客户端测试命令 |
|------|-----|-----------|----------------|
| HTTP/1.1 明文 | 不配置 | — | `curl http://...` |
| HTTPS + HTTP/1.1 | `set_ssl_context()` | `{"http/1.1"}` | `curl -k https://...` |
| HTTPS + HTTP/2 优先 | `set_ssl_context()` | `{"h2", "http/1.1"}` | `curl --http2 -k https://...` |
| HTTPS + 仅 HTTP/2 | `set_ssl_context()` | `{"h2"}` | `curl --http2 -k https://...` |
| h2c 明文 | 不配置 | — | `curl --http2-prior-knowledge http://...` |

### 主要特性

- **ALPN 协商**: 服务器通过 `configure_alpn_server()` 广告支持的协议列表；客户端在 TLS 握手时选择协议
- **多路复用流**: HTTP/2 在单个 TLS 连接上并发处理多个请求/响应对
- **共享路由和中间件**: 所有 HTTP/1.1 路由和中间件透明地支持 HTTP/2
- **协议无关 handler**: `request_context` 抽象了底层协议 —— handler 无需知道是 h1 还是 h2
- **性能优化**: 写合并、响应批处理、调优的流控窗口、TCP_NODELAY
- **自动检测**: 明文模式下自动识别 h2c 客户端前言，无需手动配置

完整示例参见 `examples/http2_demo.cpp`。

## 性能提示

1. **使用连接池**用于数据库/Redis 连接
2. **启用压缩**用于大响应
3. **缓存响应**使用缓存中间件
4. **使用多核**通过 `server_context`
5. **最小化分配**在热路径中
6. **使用 `string_view`**用于只读字符串
7. **批量数据库查询**在可能的情况下
8. **使用 HTTP/2** 多路复用连接 —— 减少队头阻塞延迟

## 下一步

- **[WebSocket](websocket.md)** - 实时通信
- **[中间件指南](../middleware/http-middleware.md)** - 构建自定义中间件
- **[MySQL](mysql.md)** - 数据库集成
- **[Redis](redis.md)** - 缓存层
