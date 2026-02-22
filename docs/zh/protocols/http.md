# HTTP 服务器

使用 cnetmod 的 HTTP/1.1 服务器构建 REST API 和 Web 服务。

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

## 性能提示

1. **使用连接池**用于数据库/Redis 连接
2. **启用压缩**用于大响应
3. **缓存响应**使用缓存中间件
4. **使用多核**通过 `server_context`
5. **最小化分配**在热路径中
6. **使用 `string_view`**用于只读字符串
7. **批量数据库查询**在可能的情况下

## 下一步

- **[WebSocket](websocket.md)** - 实时通信
- **[中间件指南](../middleware/http-middleware.md)** - 构建自定义中间件
- **[MySQL](mysql.md)** - 数据库集成
- **[Redis](redis.md)** - 缓存层
