// HTTP Server Cookie and Chunked Demo
// Demonstrates server-side cookie management and chunked transfer encoding

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

// =============================================================================
// Implementation note: Cookie.
// =============================================================================

auto handle_set_cookie(request_context& ctx) -> task<void> {
    // Implementation note: cookies.
    ctx.resp()
        .set_cookie("session_id", "abc123", "", "/", std::chrono::hours(24))
        .set_cookie("user_name", "john", "", "/", std::chrono::hours(24))
        .set_cookie("theme", "dark", "", "/", std::chrono::hours(24 * 7));
    
    ctx.json(status::ok, R"({"message":"Cookies set successfully"})");
    co_return;
}

auto handle_get_cookies(request_context& ctx) -> task<void> {
    // ReadClient cookies
    auto cookie_header = ctx.get_header("Cookie");
    
    if (cookie_header.empty()) {
        ctx.json(status::ok, R"({"cookies":[],"message":"No cookies found"})");
        co_return;
    }
    
    // Simpleparse Cookie (: name1=value1; name2=value2)
    std::string json = R"({"cookies":[)";
    bool first = true;
    
    std::string_view remaining = cookie_header;
    while (!remaining.empty()) {
        // Implementation note.
        while (!remaining.empty() && remaining[0] == ' ') {
            remaining = remaining.substr(1);
        }
        
        // Implementation note.
        auto semicolon = remaining.find(';');
        auto pair = (semicolon != std::string_view::npos) 
            ? remaining.substr(0, semicolon) 
            : remaining;
        
        // Parse name=value
        auto eq = pair.find('=');
        if (eq != std::string_view::npos) {
            auto name = pair.substr(0, eq);
            auto value = pair.substr(eq + 1);
            
            if (!first) json += ",";
            json += std::format(R"({{"name":"{}","value":"{}"}})", name, value);
            first = false;
        }
        
        if (semicolon == std::string_view::npos) break;
        remaining = remaining.substr(semicolon + 1);
    }
    
    json += "]}";
    ctx.json(status::ok, json);
    co_return;
}

auto handle_delete_cookie(request_context& ctx) -> task<void> {
    // Delete cookie(expired)
    cookie c;
    c.name = "session_id";
    c.value = "";
    c.path = "/";
    c.max_age = std::chrono::seconds(0);  // Implementation note.
    
    ctx.resp().set_cookie(c);
    ctx.json(status::ok, R"({"message":"Cookie deleted"})");
    co_return;
}

// =============================================================================
// Chunked Transfer
// =============================================================================

auto handle_chunked_response(request_context& ctx) -> task<void> {
    // Generate( Content-Length, chunked)
    std::string large_data;
    large_data.reserve(100000);
    
    for (int i = 0; i < 1000; ++i) {
        large_data += std::format("Line {}: This is a test line with some data\n", i);
    }
    
    // Content-Length, chunked encoding
    ctx.resp()
        .set_status(status::ok)
        .set_header("Content-Type", "text/plain")
        // Note: Content-Length
        .set_body(large_data);
    
    co_return;
}

auto handle_stream_data(request_context& ctx) -> task<void> {
    // Simulate()
    std::string stream_data;
    
    for (int i = 0; i < 10; ++i) {
        stream_data += std::format("data: Event {}\n\n", i);
    }
    
    ctx.resp()
        .set_status(status::ok)
        .set_header("Content-Type", "text/event-stream")
        .set_header("Cache-Control", "no-cache")
        // Content-Length, chunked
        .set_body(stream_data);
    
    co_return;
}

// =============================================================================
// Cookie + Chunked
// =============================================================================

auto handle_authenticated_stream(request_context& ctx) -> task<void> {
    // Implementation note: cookie.
    auto cookie_header = ctx.get_header("Cookie");
    bool authenticated = cookie_header.find("session_id=") != std::string_view::npos;
    
    if (!authenticated) {
        ctx.json(status::unauthorized, R"({"error":"Not authenticated"})");
        co_return;
    }
    
    // Generate(chunked)
    std::string data;
    data.reserve(50000);
    
    for (int i = 0; i < 500; ++i) {
        data += std::format("Authenticated data line {}\n", i);
    }
    
    ctx.resp()
        .set_status(status::ok)
        .set_header("Content-Type", "text/plain")
        .set_cookie("last_access", std::to_string(std::time(nullptr)), "", "/")
        .set_body(data);
    
    co_return;
}

// =============================================================================
// Main function
// =============================================================================

auto main() -> int {
    auto ctx = make_io_context();
    
    server srv(*ctx);
    
    // Implementation note.
    router r;
    
    // Cookie route
    r.get("/cookie/set", handle_set_cookie);
    r.get("/cookie/get", handle_get_cookies);
    r.get("/cookie/delete", handle_delete_cookie);
    
    // Chunked route
    r.get("/chunked/large", handle_chunked_response);
    r.get("/chunked/stream", handle_stream_data);
    
    // Implementation note.
    r.get("/auth/stream", handle_authenticated_stream);
    
    // Implementation note.
    r.get("/", [](request_context& ctx) -> task<void> {
        ctx.html(status::ok, R"(
<!DOCTYPE html>
<html>
<head>
    <title>HTTP Server Demo</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        h1 { color: #333; }
        .section { margin: 20px 0; padding: 20px; background: #f5f5f5; }
        a { display: block; margin: 10px 0; color: #0066cc; }
        code { background: #eee; padding: 2px 6px; }
    </style>
</head>
<body>
    <h1>HTTP Server Cookie & Chunked Demo</h1>
    
    <div class="section">
        <h2>Cookie Management</h2>
        <a href="/cookie/set">Set Cookies</a>
        <a href="/cookie/get">Get Cookies</a>
        <a href="/cookie/delete">Delete Cookie</a>
    </div>
    
    <div class="section">
        <h2>Chunked Transfer Encoding</h2>
        <a href="/chunked/large">Large Response (Chunked)</a>
        <a href="/chunked/stream">Stream Data (SSE)</a>
    </div>
    
    <div class="section">
        <h2>Combined Features</h2>
        <a href="/auth/stream">Authenticated Stream (Cookie + Chunked)</a>
        <p><small>Note: Set cookies first, then access this endpoint</small></p>
    </div>
    
    <div class="section">
        <h2>Features Demonstrated</h2>
        <ul>
            <li><strong>Cookie Management:</strong>
                <ul>
                    <li>Set-Cookie header generation</li>
                    <li>Cookie parsing from requests</li>
                    <li>Cookie expiration</li>
                    <li>Multiple cookies</li>
                </ul>
            </li>
            <li><strong>Chunked Transfer:</strong>
                <ul>
                    <li>Automatic chunked encoding</li>
                    <li>Large response handling</li>
                    <li>Server-sent events (SSE)</li>
                    <li>No Content-Length required</li>
                </ul>
            </li>
        </ul>
    </div>
    
    <div class="section">
        <h2>How It Works</h2>
        <p><strong>Cookies:</strong></p>
        <code>ctx.resp().set_cookie("name", "value", "", "/", hours(24))</code>
        
        <p><strong>Chunked:</strong></p>
        <code>ctx.resp().set_body(large_data)  // No Content-Length = chunked</code>
    </div>
</body>
</html>
        )");
        co_return;
    });
    
    srv.set_router(std::move(r));
    
    // Implementation note.
    auto listen_result = srv.listen("127.0.0.1", 8080);
    if (!listen_result) {
        std::println("Failed to listen: {}", listen_result.error().message());
        return 1;
    }
    
    std::println("=== HTTP Server Cookie & Chunked Demo ===");
    std::println("Server listening on http://127.0.0.1:8080");
    std::println("\nFeatures:");
    std::println("✓ Cookie management (Set-Cookie, parsing)");
    std::println("✓ Chunked transfer encoding (automatic)");
    std::println("✓ Large response handling");
    std::println("✓ Server-sent events (SSE)");
    std::println("\nEndpoints:");
    std::println("  GET /                    - Demo homepage");
    std::println("  GET /cookie/set          - Set cookies");
    std::println("  GET /cookie/get          - Get cookies");
    std::println("  GET /cookie/delete       - Delete cookie");
    std::println("  GET /chunked/large       - Large chunked response");
    std::println("  GET /chunked/stream      - Stream data (SSE)");
    std::println("  GET /auth/stream         - Authenticated stream");
    std::println("\nPress Ctrl+C to stop\n");
    
    spawn(*ctx, srv.run());
    ctx->run();
    
    return 0;
}
