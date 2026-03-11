// HTTP Client Advanced Features Demo
// Demonstrates Cookie management

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

// =============================================================================
// Cookie 管理示例
// =============================================================================

auto demo_cookies(client& http_client) -> task<void> {
    std::println("\n=== Cookie Management Demo ===");
    
    // 第一次请求 - 服务器设置 cookie
    std::println("1. First request - server sets cookies");
    auto result1 = co_await http_client.get("http://httpbin.org/cookies/set?name=value");
    if (result1) {
        std::println("   Status: {}", result1->status_code());
        
        // 查看存储的 cookies
        const auto& jar = http_client.cookies();
        std::println("   Stored cookies: {}", jar.cookies().size());
        for (const auto& cookie : jar.cookies()) {
            std::println("   - {}: {}", cookie.name, cookie.value);
        }
    }
    
    // 第二次请求 - 自动发送 cookies
    std::println("\n2. Second request - cookies sent automatically");
    auto result2 = co_await http_client.get("http://httpbin.org/cookies");
    if (result2) {
        std::println("   Status: {}", result2->status_code());
        std::println("   Response: {}", result2->body().substr(0, 200));
    }
    
    // 手动管理 cookies
    std::println("\n3. Manual cookie management");
    cookie custom_cookie;
    custom_cookie.name = "custom";
    custom_cookie.value = "test123";
    custom_cookie.domain = "httpbin.org";
    custom_cookie.path = "/";
    custom_cookie.http_only = true;
    
    http_client.cookies().add(custom_cookie);
    std::println("   Added custom cookie: {}={}", custom_cookie.name, custom_cookie.value);
    
    // 清除 cookies
    std::println("\n4. Clear cookies");
    http_client.cookies().clear();
    std::println("   All cookies cleared");
}

// =============================================================================
// 高级特性组合示例
// =============================================================================

auto demo_advanced_features(client& http_client) -> task<void> {
    std::println("\n=== Advanced Features Combined ===");
    
    // 1. 带 Cookie 的认证流程
    std::println("1. Authentication flow with cookies");
    
    // 模拟登录（设置 session cookie）
    request login_req(http_method::POST, "http://httpbin.org/cookies/set?session=abc123");
    login_req.set_header("Content-Type", "application/json");
    
    auto login_result = co_await http_client.send(login_req);
    if (login_result) {
        std::println("   ✓ Login successful, session cookie stored");
    }
    
    // 使用 session cookie 访问受保护资源
    auto protected_result = co_await http_client.get("http://httpbin.org/cookies");
    if (protected_result) {
        std::println("   ✓ Accessed protected resource with session");
    }
    
    // 2. 自定义请求头 + Cookies
    std::println("\n2. Custom headers with automatic cookies");
    request custom_req(http_method::GET, "http://httpbin.org/headers");
    custom_req.set_header("X-Custom-Header", "MyValue");
    custom_req.set_header("Authorization", "Bearer token123");
    
    auto custom_result = co_await http_client.send(custom_req);
    if (custom_result) {
        std::println("   ✓ Request sent with custom headers and cookies");
        std::println("   Response preview: {}", 
            custom_result->body().substr(0, 150));
    }
}

// =============================================================================
// 主函数
// =============================================================================

auto run_demos(client& http_client) -> task<void> {
    std::println("=== HTTP Client Advanced Features Demo ===");
    std::println("Demonstrating: Cookie Management\n");
    
    // Cookie 管理
    co_await demo_cookies(http_client);
    
    // 高级特性组合
    co_await demo_advanced_features(http_client);
    
    std::println("\n=== Demo Complete ===");
    std::println("\nImplemented Features:");
    std::println("✓ Cookie Management");
    std::println("  - Automatic cookie storage");
    std::println("  - Automatic cookie sending");
    std::println("  - Manual cookie manipulation");
    std::println("  - Cookie expiration handling");
    std::println("  - Domain and path matching");
    std::println("\nNote: For WebSocket support, use cnetmod.protocol.websocket module");
    std::println("      WebSocket connection can be established from HTTP client connection");
}

auto main() -> int {
    auto ctx = make_io_context();
    
    client_options opts;
    opts.connect_timeout = std::chrono::seconds(5);
    opts.request_timeout = std::chrono::seconds(30);
    opts.follow_redirects = true;
    opts.keep_alive = true;
    opts.enable_cookies = true;  // 启用 Cookie 管理
    opts.user_agent = "cnetmod-advanced-demo/1.0";
    opts.verify_peer = true;
    
    client http_client(*ctx, opts);
    
    spawn(*ctx, run_demos(http_client));
    ctx->run();
    
    return 0;
}
