# HTTP Server

Build REST APIs and web services with cnetmod's HTTP/1.1 and HTTP/2 server.

## Quick Start

```cpp
import cnetmod;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;
    
    http::server srv(ctx);
    
    // Simple route
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello, World!");
        co_return;
    });
    
    // JSON response
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

## Routing

### Path Parameters

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

### Wildcard Routes

```cpp
// Match /files/* (e.g., /files/docs/readme.txt)
srv.get("/files/*filepath", [](http::request_context& ctx) -> task<void> {
    auto filepath = ctx.param("filepath");
    // Serve file...
    co_return;
});
```

### HTTP Methods

```cpp
srv.get("/resource", get_handler);
srv.post("/resource", create_handler);
srv.put("/resource/:id", update_handler);
srv.patch("/resource/:id", patch_handler);
srv.del("/resource/:id", delete_handler);
srv.options("/resource", options_handler);
srv.head("/resource", head_handler);
```

### Route Groups

```cpp
auto api = srv.group("/api");
api.get("/users", list_users);
api.post("/users", create_user);
api.get("/users/:id", get_user);
api.put("/users/:id", update_user);
api.del("/users/:id", delete_user);
```

## Request Handling

### Query Parameters

```cpp
srv.get("/search", [](http::request_context& ctx) -> task<void> {
    auto query = ctx.query("q");
    auto page = ctx.query("page", "1");  // Default value
    auto limit = std::stoi(ctx.query("limit", "10"));
    
    // Perform search...
    co_return;
});
```

### Headers

```cpp
srv.get("/", [](http::request_context& ctx) -> task<void> {
    auto user_agent = ctx.header("User-Agent");
    auto auth = ctx.header("Authorization");
    
    if (auth.empty()) {
        ctx.resp().set_status(http::status::unauthorized);
        co_return;
    }
    
    // Process request...
    co_return;
});
```

### Request Body

```cpp
// JSON body
srv.post("/api/users", [](http::request_context& ctx) -> task<void> {
    auto json = ctx.json();
    auto name = json["name"].get<std::string>();
    auto email = json["email"].get<std::string>();
    
    // Create user...
    ctx.resp().json({{"id", 123}, {"name", name}});
    co_return;
});

// Raw body
srv.post("/upload", [](http::request_context& ctx) -> task<void> {
    auto body = ctx.body();
    // Process body...
    co_return;
});
```

### Multipart Form Data

```cpp
srv.post("/upload", [](http::request_context& ctx) -> task<void> {
    auto form = ctx.multipart();
    
    // Text fields
    auto title = form.field("title");
    
    // File uploads
    auto file = form.file("document");
    if (file) {
        std::println("Uploaded: {} ({} bytes)",
            file->filename, file->content.size());
        
        // Save file...
        co_await save_file(file->filename, file->content);
    }
    
    ctx.resp().json({{"status", "uploaded"}});
    co_return;
});
```

## Response Handling

### Status Codes

```cpp
ctx.resp().set_status(http::status::ok);  // 200
ctx.resp().set_status(http::status::created);  // 201
ctx.resp().set_status(http::status::no_content);  // 204
ctx.resp().set_status(http::status::bad_request);  // 400
ctx.resp().set_status(http::status::unauthorized);  // 401
ctx.resp().set_status(http::status::not_found);  // 404
ctx.resp().set_status(http::status::internal_server_error);  // 500
```

### Response Body

```cpp
// Plain text
ctx.resp().set_body("Hello, World!");

// JSON
ctx.resp().json({{"key", "value"}});

// HTML
ctx.resp().html("<h1>Hello</h1>");

// Custom content type
ctx.resp().set_header("Content-Type", "application/xml");
ctx.resp().set_body("<root><item>value</item></root>");
```

### Headers

```cpp
ctx.resp().set_header("Content-Type", "application/json");
ctx.resp().set_header("Cache-Control", "no-cache");
ctx.resp().set_header("X-Custom-Header", "value");
```

### Redirects

```cpp
ctx.resp().redirect("/new-location");  // 302
ctx.resp().redirect("/new-location", http::status::moved_permanently);  // 301
```

### Streaming Response

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

## Middleware

### Using Middleware

```cpp
http::server srv(ctx);

// Global middleware
srv.use(cors_middleware());
srv.use(jwt_auth_middleware());
srv.use(request_id_middleware());
srv.use(access_log_middleware());

// Route-specific middleware
srv.get("/admin", admin_only_middleware(), admin_handler);
```

### Built-in Middleware

```cpp
// CORS
srv.use(cors({
    .allowed_origins = {"https://example.com"},
    .allowed_methods = {"GET", "POST", "PUT", "DELETE"},
    .allowed_headers = {"Content-Type", "Authorization"},
    .max_age = 3600
}));

// JWT Authentication
srv.use(jwt_auth({
    .secret = "your-secret-key",
    .skip_paths = {"/login", "/register"}
}));

// Rate Limiting
srv.use(rate_limiter({
    .requests_per_second = 100,
    .burst = 200
}));

// Compression
srv.use(compress({
    .min_size = 1024,
    .level = 6
}));

// Body Limit
srv.use(body_limit({
    .max_size = 10 * 1024 * 1024  // 10 MB
}));

// Request ID
srv.use(request_id());

// Access Log
srv.use(access_log());

// Metrics
srv.use(metrics());

// Timeout
srv.use(timeout(30s));

// Recovery (panic handler)
srv.use(recover());
```

### Custom Middleware

```cpp
auto my_middleware() -> http::middleware_fn {
    return [](http::request_context& ctx, http::next_fn next) -> task<void> {
        // Before handler
        std::println("Request: {} {}", ctx.method(), ctx.path());
        
        // Call next middleware/handler
        co_await next();
        
        // After handler
        std::println("Response: {}", ctx.resp().status_code());
    };
}

srv.use(my_middleware());
```

## Error Handling

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

## Static Files

```cpp
// Serve files from directory
srv.static_files("/static", "./public");

// Custom file handler
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

## WebSocket Upgrade

```cpp
srv.get("/ws", [](http::request_context& ctx) -> task<void> {
    if (!ctx.is_websocket_upgrade()) {
        ctx.resp().set_status(http::status::bad_request);
        co_return;
    }
    
    auto ws = co_await ctx.upgrade_to_websocket();
    
    // Handle WebSocket connection
    while (true) {
        auto msg = co_await ws.recv();
        if (!msg) break;
        
        co_await ws.send(*msg);  // Echo
    }
});
```

## Multi-Core Server

```cpp
int main() {
    net_init guard;
    
    // 4 worker threads
    server_context srv_ctx(4);
    
    http::server srv(srv_ctx.get_io_context());
    
    srv.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello from multi-core server!");
        co_return;
    });
    
    srv.listen(8080);
    std::println("Multi-core server running on http://localhost:8080");
    
    srv_ctx.run();  // Blocks until stopped
}
```

## HTTP Protocol Version Selection

cnetmod supports both HTTP/1.1 and HTTP/2. The same router, middleware, and `request_context` are shared between both versions — handler code is protocol-agnostic.

Protocol version selection depends on two factors: **whether TLS is configured** and the **ALPN protocol list**.

### Build Requirements

| Feature | Build Option |
|---------|--------------|
| HTTP/1.1 only | No extra requirements |
| HTTP/2 (TLS) | `-DCNETMOD_ENABLE_SSL=ON` + nghttp2 submodule |
| HTTP/2 (cleartext h2c) | nghttp2 submodule |

### Protocol Detection Logic

The server automatically detects the protocol version on each connection:

1. **With TLS** (called `set_ssl_context()`): After TLS handshake, checks ALPN negotiation result
   - ALPN selected `h2` → uses **HTTP/2**
   - Otherwise → uses **HTTP/1.1**
2. **Without TLS** (cleartext): Reads the first bytes of the connection
   - Starts with the HTTP/2 client connection preface (`PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n`) → uses **HTTP/2 cleartext (h2c)**
   - Otherwise → uses **HTTP/1.1**

### Scenario 1: HTTP/1.1 Only (Cleartext)

Without TLS, the server runs in HTTP/1.1 mode by default. This is the simplest configuration.

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

    srv.listen("0.0.0.0", 8080);  // Cleartext, no TLS
    spawn(ctx, srv.run());
    ctx.run();
}
```

```bash
curl http://localhost:8080/
```

### Scenario 2: HTTP/1.1 Only (TLS / HTTPS)

Configure TLS but set ALPN to only advertise `http/1.1`. Even if the client supports HTTP/2, only HTTP/1.1 will be used.

```cpp
auto ssl_ctx = ssl_context::server().value();
ssl_ctx.load_cert_file("cert.pem");
ssl_ctx.load_key_file("key.pem");
ssl_ctx.configure_alpn_server({"http/1.1"});  // Only advertise HTTP/1.1

http::server srv(ctx);
srv.listen("0.0.0.0", 8443);
srv.set_ssl_context(ssl_ctx);  // HTTPS, but HTTP/1.1 only
```

```bash
curl -k https://localhost:8443/     # HTTPS + HTTP/1.1
curl --http2 -k https://localhost:8443/  # Client requests h2, but server only supports h1.1 — falls back to HTTP/1.1
```

### Scenario 3: Both HTTP/2 and HTTP/1.1 (Recommended)

Include both `h2` and `http/1.1` in the ALPN list. The list order determines server preference — protocols listed first have higher priority.

```cpp
auto ssl_ctx = ssl_context::server().value();
ssl_ctx.load_cert_file("cert.pem");
ssl_ctx.load_key_file("key.pem");

// Prefer h2; clients that don't support h2 fall back to http/1.1
ssl_ctx.configure_alpn_server({"h2", "http/1.1"});

http::server srv(*ctx);
srv.listen("0.0.0.0", 8443);
srv.set_ssl_context(ssl_ctx);
```

During TLS handshake, the client and server negotiate a protocol via ALPN. If both support `h2`, HTTP/2 is used; otherwise falls back to `http/1.1`.

```bash
curl --http2 -k https://localhost:8443/    # Negotiates to HTTP/2
curl --http1.1 -k https://localhost:8443/  # Forces HTTP/1.1
nghttp -v https://localhost:8443/           # View HTTP/2 frame details
```

### Scenario 4: HTTP/2 Only (TLS)

Set ALPN to only advertise `h2`. Clients that don't support HTTP/2 will fail to connect.

```cpp
ssl_ctx.configure_alpn_server({"h2"});  // Only advertise h2
```

```bash
curl --http2 -k https://localhost:8443/    # OK, negotiates to HTTP/2
curl --http1.1 -k https://localhost:8443/  # Fails: ALPN negotiation mismatch
```

### Scenario 5: HTTP/2 Cleartext (h2c)

Without TLS, if the client sends an HTTP/2 client connection preface, the server automatically switches to HTTP/2 cleartext mode (h2c). Requires nghttp2 at build time.

```cpp
// No special configuration needed — just don't set ssl_context
http::server srv(ctx);
srv.listen("0.0.0.0", 8080);  // Cleartext, supports both h2c and HTTP/1.1
spawn(ctx, srv.run());
```

```bash
# HTTP/2 cleartext (h2c, requires curl --http2-prior-knowledge)
curl --http2-prior-knowledge http://localhost:8080/

# Plain HTTP/1.1
curl http://localhost:8080/
```

> **Note**: h2c is typically only used for internal services or testing. Use TLS + ALPN for production.

### Complete Example

Full server supporting both HTTP/2 and HTTP/1.1:

```cpp
import cnetmod;
import cnetmod.core.ssl;
import cnetmod.protocol.http;
using namespace cnetmod;

int main() {
    net_init guard;
    auto ctx = make_io_context();

    // TLS context with ALPN
    auto ssl_ctx = ssl_context::server().value();
    ssl_ctx.load_cert_file("cert.pem");
    ssl_ctx.load_key_file("key.pem");
    ssl_ctx.configure_alpn_server({"h2", "http/1.1"});

    // Router — identical to HTTP/1.1
    http::router router;
    router.get("/", [](http::request_context& ctx) -> task<void> {
        ctx.resp().set_body("Hello over HTTP/2 or HTTP/1.1!");
        co_return;
    });

    http::server srv(*ctx);
    srv.listen("0.0.0.0", 8443);
    srv.set_router(std::move(router));
    srv.set_ssl_context(ssl_ctx);  // Enables HTTPS + HTTP/2

    spawn(*ctx, srv.run());
    ctx->run();
}
```

### Quick Reference

| Goal | TLS | ALPN Config | Client Test Command |
|------|-----|-------------|---------------------|
| HTTP/1.1 cleartext | None | — | `curl http://...` |
| HTTPS + HTTP/1.1 | `set_ssl_context()` | `{"http/1.1"}` | `curl -k https://...` |
| HTTPS + HTTP/2 preferred | `set_ssl_context()` | `{"h2", "http/1.1"}` | `curl --http2 -k https://...` |
| HTTPS + HTTP/2 only | `set_ssl_context()` | `{"h2"}` | `curl --http2 -k https://...` |
| h2c cleartext | None | — | `curl --http2-prior-knowledge http://...` |

### Key Features

- **ALPN negotiation**: Server advertises supported protocols via `configure_alpn_server()`; client selects during TLS handshake
- **Multiplexed streams**: HTTP/2 handles multiple concurrent request/response pairs over a single TLS connection
- **Shared router & middleware**: All HTTP/1.1 routes and middleware work transparently with HTTP/2
- **Protocol-agnostic handlers**: `request_context` abstracts the underlying protocol — handlers don't need to know if it's h1 or h2
- **Performance optimizations**: Write coalescing, response batching, tuned flow control windows, TCP_NODELAY
- **Auto-detection**: In cleartext mode, automatically detects h2c client preface with no manual configuration

See `examples/http2_demo.cpp` for a complete working example.

## Performance Tips

1. **Use connection pooling** for database/Redis connections
2. **Enable compression** for large responses
3. **Cache responses** with cache middleware
4. **Use multi-core** with `server_context`
5. **Minimize allocations** in hot paths
6. **Use `string_view`** for read-only strings
7. **Batch database queries** when possible
8. **Use HTTP/2** for multiplexed connections — reduces latency from head-of-line blocking

## Next Steps

- **[WebSocket](websocket.md)** - Real-time communication
- **[Middleware Guide](../middleware/http-middleware.md)** - Build custom middleware
- **[MySQL](mysql.md)** - Database integration
- **[Redis](redis.md)** - Caching layer
