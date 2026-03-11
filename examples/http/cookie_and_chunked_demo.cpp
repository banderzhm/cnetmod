// Cookie and Chunked Transfer Demo
// Demonstrates simplified cookie API and chunked transfer handling

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

// =============================================================================
// 简化的 Cookie 接口示例
// =============================================================================

auto demo_simple_cookie_api(client& http_client) -> task<void> {
    std::println("\n=== Simplified Cookie API Demo ===");
    
    // 方法 1: 使用简化接口设置 Cookie
    std::println("\n1. Set cookies using simplified API");
    http_client
        .set_cookie("session", "abc123", "example.com", "/")
        .set_cookie("user_id", "12345", "example.com", "/")
        .set_cookie("theme", "dark", "example.com", "/api");
    
    std::println("   ✓ Set 3 cookies using chained calls");
    
    // 查看设置的 cookies
    const auto& jar = http_client.cookies();
    std::println("   Stored cookies: {}", jar.cookies().size());
    for (const auto& c : jar.cookies()) {
        std::println("   - {}={} (domain: {}, path: {})", 
            c.name, c.value, c.domain, c.path);
    }
    
    // 方法 2: 使用完整接口设置 Cookie（更多控制）
    std::println("\n2. Set cookie with full control");
    cookie advanced_cookie;
    advanced_cookie.name = "secure_token";
    advanced_cookie.value = "xyz789";
    advanced_cookie.domain = "api.example.com";
    advanced_cookie.path = "/v1";
    advanced_cookie.secure = true;
    advanced_cookie.http_only = true;
    advanced_cookie.same_site = cookie::same_site_policy::strict;
    advanced_cookie.max_age = std::chrono::hours(24);
    
    http_client.cookies().add(advanced_cookie);
    std::println("   ✓ Set secure cookie with full options");
    
    // 清除所有 cookies
    std::println("\n3. Clear all cookies");
    http_client.clear_cookies();
    std::println("   ✓ All cookies cleared");
    std::println("   Remaining cookies: {}", http_client.cookies().cookies().size());
}

// =============================================================================
// Chunked Transfer Encoding 示例
// =============================================================================

auto demo_chunked_transfer(client& http_client) -> task<void> {
    std::println("\n=== Chunked Transfer Encoding Demo ===");
    
    // 示例 1: 接收 chunked 响应
    std::println("\n1. Receive chunked response");
    auto result1 = co_await http_client.get("http://httpbin.org/stream/5");
    
    if (result1) {
        std::println("   ✓ Received chunked response");
        std::println("   Status: {}", result1->status_code());
        std::println("   Transfer-Encoding: {}", 
            result1->get_header("Transfer-Encoding"));
        std::println("   Body size: {} bytes", result1->body().size());
        std::println("   Body preview: {}", 
            result1->body().substr(0, std::min<size_t>(100, result1->body().size())));
    } else {
        std::println("   ✗ Error: {}", result1.error().message());
    }
    
    // 示例 2: 大文件下载（通常使用 chunked）
    std::println("\n2. Download large file (chunked)");
    auto result2 = co_await http_client.get("http://httpbin.org/bytes/10240");
    
    if (result2) {
        std::println("   ✓ Downloaded {} bytes", result2->body().size());
        
        // 检查是否使用了 chunked 编码
        auto transfer_encoding = result2->get_header("Transfer-Encoding");
        if (transfer_encoding.find("chunked") != std::string_view::npos) {
            std::println("   ✓ Used chunked transfer encoding");
        } else {
            std::println("   ℹ Used Content-Length: {}", 
                result2->get_header("Content-Length"));
        }
    } else {
        std::println("   ✗ Error: {}", result2.error().message());
    }
}

// =============================================================================
// 组合示例：带 Cookie 的 Chunked 请求
// =============================================================================

auto demo_combined(client& http_client) -> task<void> {
    std::println("\n=== Combined: Cookies + Chunked Transfer ===");
    
    // 设置认证 cookie
    http_client.set_cookie("auth_token", "secret123", "httpbin.org", "/");
    
    std::println("1. Set authentication cookie");
    std::println("   ✓ Cookie: auth_token=secret123");
    
    // 发送请求（自动包含 cookie）
    std::println("\n2. Send request with cookie (chunked response)");
    auto result = co_await http_client.get("http://httpbin.org/stream/3");
    
    if (result) {
        std::println("   ✓ Request successful");
        std::println("   Status: {}", result->status_code());
        
        // 验证 cookie 被发送
        // （httpbin.org/stream 不会回显 cookies，但实际应用中会）
        std::println("   Cookie was automatically sent in request");
        
        // 处理 chunked 响应
        std::println("   Received {} bytes of data", result->body().size());
    }
    
    // 清理
    http_client.clear_cookies();
}

// =============================================================================
// Chunked 编码详解
// =============================================================================

auto explain_chunked_encoding() -> task<void> {
    std::println("\n=== Chunked Transfer Encoding Explained ===\n");
    
    std::println("What is Chunked Transfer Encoding?");
    std::println("- HTTP/1.1 feature for streaming data without knowing total size");
    std::println("- Server sends data in chunks, each with its size");
    std::println("- Useful for: streaming, dynamic content, large files\n");
    
    std::println("Format:");
    std::println("  [chunk-size in hex]\\r\\n");
    std::println("  [chunk-data]\\r\\n");
    std::println("  [chunk-size in hex]\\r\\n");
    std::println("  [chunk-data]\\r\\n");
    std::println("  0\\r\\n");
    std::println("  \\r\\n\n");
    
    std::println("Example:");
    std::println("  5\\r\\n");
    std::println("  Hello\\r\\n");
    std::println("  6\\r\\n");
    std::println("  World!\\r\\n");
    std::println("  0\\r\\n");
    std::println("  \\r\\n\n");
    
    std::println("Client Implementation:");
    std::println("✓ Automatic detection (Transfer-Encoding: chunked)");
    std::println("✓ Automatic decoding of chunks");
    std::println("✓ Hex size parsing");
    std::println("✓ Trailer headers support");
    std::println("✓ Error handling for malformed chunks\n");
    
    std::println("Advantages:");
    std::println("+ No need to know content length in advance");
    std::println("+ Can start sending immediately");
    std::println("+ Efficient for streaming");
    std::println("+ Supports trailer headers\n");
    
    std::println("When to use:");
    std::println("- Server-sent events");
    std::println("- Dynamic content generation");
    std::println("- Large file transfers");
    std::println("- Real-time data streaming");
}

// =============================================================================
// 主函数
// =============================================================================

auto run_demos(client& http_client) -> task<void> {
    std::println("=== Cookie and Chunked Transfer Demo ===");
    
    // Cookie 简化接口
    co_await demo_simple_cookie_api(http_client);
    
    // Chunked 传输
    co_await demo_chunked_transfer(http_client);
    
    // 组合使用
    co_await demo_combined(http_client);
    
    // 说明
    co_await explain_chunked_encoding();
    
    std::println("\n=== Demo Complete ===");
}

auto main() -> int {
    auto ctx = make_io_context();
    
    client_options opts;
    opts.enable_cookies = true;
    opts.keep_alive = true;
    opts.user_agent = "cnetmod-demo/1.0";
    
    client http_client(*ctx, opts);
    
    spawn(*ctx, run_demos(http_client));
    ctx->run();
    
    return 0;
}
