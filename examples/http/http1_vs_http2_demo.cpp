// HTTP/1.1 vs HTTP/2 Streaming Demo
// Demonstrates the difference in how data is transferred

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

// =============================================================================
// HTTP/1.1 Chunked Transfer Demo
// =============================================================================

auto demo_http1_chunked(client& http_client) -> task<void> {
    std::println("\n=== HTTP/1.1 Chunked Transfer ===");
    
    // 强制使用 HTTP/1.1
    client_options opts;
    opts.version_pref = http_version_preference::http1_only;
    client http1_client(*http_client.options().ctx_, opts);
    
    std::println("Requesting streaming endpoint...");
    auto result = co_await http1_client.get("http://httpbin.org/stream/5");
    
    if (result) {
        std::println("\n✓ Response received");
        std::println("  Protocol: HTTP/1.1");
        std::println("  Status: {}", result->status_code());
        
        // 检查 Transfer-Encoding 头
        auto transfer_encoding = result->get_header("Transfer-Encoding");
        if (!transfer_encoding.empty()) {
            std::println("  Transfer-Encoding: {}", transfer_encoding);
            
            if (transfer_encoding.find("chunked") != std::string_view::npos) {
                std::println("  ✓ Used chunked transfer encoding");
                std::println("\n  How it works:");
                std::println("  1. Server sends: [size in hex]\\r\\n[data]\\r\\n");
                std::println("  2. Client parses hex size");
                std::println("  3. Client reads exact amount of data");
                std::println("  4. Repeat until size = 0");
            }
        } else {
            auto content_length = result->get_header("Content-Length");
            std::println("  Content-Length: {}", content_length);
            std::println("  ℹ Used fixed content length (not chunked)");
        }
        
        std::println("\n  Body size: {} bytes", result->body().size());
        std::println("  Body preview: {}", 
            result->body().substr(0, std::min<size_t>(100, result->body().size())));
    } else {
        std::println("✗ Error: {}", result.error().message());
    }
}

// =============================================================================
// HTTP/2 DATA Frames Demo
// =============================================================================

auto demo_http2_frames() -> task<void> {
    std::println("\n=== HTTP/2 DATA Frames ===");
    
#ifdef CNETMOD_HAS_NGHTTP2
    auto ctx = make_io_context();
    
    // 强制使用 HTTP/2
    client_options opts;
    opts.version_pref = http_version_preference::http2_only;
    client http2_client(*ctx, opts);
    
    std::println("Requesting from HTTP/2 server...");
    auto result = co_await http2_client.get("https://http2.golang.org/");
    
    if (result) {
        std::println("\n✓ Response received");
        std::println("  Protocol: HTTP/2");
        std::println("  Status: {}", result->status_code());
        
        // HTTP/2 不会有 Transfer-Encoding 头
        auto transfer_encoding = result->get_header("Transfer-Encoding");
        if (transfer_encoding.empty()) {
            std::println("  Transfer-Encoding: (none)");
            std::println("  ✓ HTTP/2 doesn't use chunked encoding");
            
            std::println("\n  How it works:");
            std::println("  1. Server sends binary DATA frames");
            std::println("  2. Each frame has 9-byte header + payload");
            std::println("  3. nghttp2 library parses frames automatically");
            std::println("  4. Callback receives decoded data");
            std::println("  5. Multiple streams can be multiplexed");
        }
        
        std::println("\n  Body size: {} bytes", result->body().size());
        std::println("  Body preview: {}", 
            result->body().substr(0, std::min<size_t>(100, result->body().size())));
    } else {
        std::println("✗ Error: {}", result.error().message());
    }
#else
    std::println("HTTP/2 support not enabled (need nghttp2)");
#endif
}

// =============================================================================
// 协议自动协商 Demo
// =============================================================================

auto demo_auto_negotiation() -> task<void> {
    std::println("\n=== Automatic Protocol Negotiation ===");
    
    auto ctx = make_io_context();
    
    // 使用默认设置（优先 HTTP/2）
    client_options opts;
    opts.version_pref = http_version_preference::http2_preferred;
    client http_client(*ctx, opts);
    
    std::println("\nTest 1: HTTPS server with HTTP/2 support");
    {
        auto result = co_await http_client.get("https://http2.golang.org/");
        if (result) {
            std::println("  ✓ Connected");
            std::println("  Negotiated: HTTP/2 (via ALPN)");
            std::println("  Transfer method: DATA frames");
        }
    }
    
    std::println("\nTest 2: HTTP server (no TLS, no ALPN)");
    {
        auto result = co_await http_client.get("http://httpbin.org/get");
        if (result) {
            std::println("  ✓ Connected");
            std::println("  Protocol: HTTP/1.1");
            
            auto transfer_encoding = result->get_header("Transfer-Encoding");
            if (!transfer_encoding.empty()) {
                std::println("  Transfer method: Chunked encoding");
            } else {
                std::println("  Transfer method: Content-Length");
            }
        }
    }
}

// =============================================================================
// 性能对比
// =============================================================================

auto demo_performance_comparison() -> task<void> {
    std::println("\n=== Performance Comparison ===");
    
    std::println("\nHTTP/1.1 Chunked:");
    std::println("  Format: Text (hex size + CRLF)");
    std::println("  Parsing: Manual hex parsing");
    std::println("  Overhead: ~10 bytes per chunk");
    std::println("  Multiplexing: No (one request at a time)");
    std::println("  Head-of-line blocking: Yes");
    
    std::println("\nHTTP/2 DATA Frames:");
    std::println("  Format: Binary (fixed 9-byte header)");
    std::println("  Parsing: nghttp2 library (optimized)");
    std::println("  Overhead: 9 bytes per frame");
    std::println("  Multiplexing: Yes (multiple streams)");
    std::println("  Head-of-line blocking: No");
    std::println("  Additional: Header compression (HPACK)");
    
    std::println("\nConclusion:");
    std::println("  HTTP/2 is more efficient for:");
    std::println("  - Multiple concurrent requests");
    std::println("  - Streaming data");
    std::println("  - Low latency applications");
    std::println("  - Modern web applications");
}

// =============================================================================
// Nginx 行为说明
// =============================================================================

auto explain_nginx_behavior() -> task<void> {
    std::println("\n=== Nginx HTTP/2 Behavior ===");
    
    std::println("\nScenario: Nginx as reverse proxy");
    std::println("\n  Backend → Nginx:");
    std::println("    HTTP/1.1 with Transfer-Encoding: chunked");
    std::println("    5\\r\\n");
    std::println("    Hello\\r\\n");
    std::println("    0\\r\\n");
    std::println("    \\r\\n");
    
    std::println("\n  Nginx → Client:");
    std::println("    HTTP/2 with DATA frames");
    std::println("    [HEADERS frame]");
    std::println("    [DATA frame: Hello]");
    std::println("    [DATA frame with END_STREAM flag]");
    
    std::println("\n  Nginx automatically:");
    std::println("  1. Receives chunked data from backend");
    std::println("  2. Decodes chunks");
    std::println("  3. Re-encodes as HTTP/2 DATA frames");
    std::println("  4. Sends to client");
    std::println("  5. No 'Transfer-Encoding' header in HTTP/2");
    
    std::println("\n  Configuration:");
    std::println("    server {");
    std::println("        listen 443 ssl http2;");
    std::println("        location / {");
    std::println("            proxy_pass http://backend;");
    std::println("            # Nginx handles protocol conversion");
    std::println("        }");
    std::println("    }");
}

// =============================================================================
// 主函数
// =============================================================================

auto run_demos() -> task<void> {
    std::println("=== HTTP/1.1 vs HTTP/2 Streaming Demo ===");
    
    auto ctx = make_io_context();
    client http_client(*ctx);
    
    // HTTP/1.1 chunked
    co_await demo_http1_chunked(http_client);
    
    // HTTP/2 frames
    co_await demo_http2_frames();
    
    // 自动协商
    co_await demo_auto_negotiation();
    
    // 性能对比
    co_await demo_performance_comparison();
    
    // Nginx 行为
    co_await explain_nginx_behavior();
    
    std::println("\n=== Demo Complete ===");
}

auto main() -> int {
    auto ctx = make_io_context();
    spawn(*ctx, run_demos());
    ctx->run();
    return 0;
}
