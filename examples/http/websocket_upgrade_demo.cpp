// HTTP to WebSocket Upgrade Demo
// Demonstrates how to upgrade from HTTP client to WebSocket connection

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;
using namespace cnetmod::ws;

// =============================================================================
// WebSocket 升级示例
// =============================================================================

auto demo_websocket_upgrade() -> task<void> {
    std::println("=== HTTP to WebSocket Upgrade Demo ===\n");
    
    auto ctx = make_io_context();
    
    // 方法 1: 直接使用 WebSocket 连接（推荐）
    std::println("Method 1: Direct WebSocket Connection");
    {
        connection ws_conn(*ctx);
        
        connect_options opts;
        opts.subprotocol = "chat";
        opts.origin = "http://example.com";
        
        auto result = co_await ws_conn.async_connect(
            "wss://echo.websocket.org/", opts);
        
        if (result) {
            std::println("✓ WebSocket connected directly");
            
            // 发送消息
            co_await ws_conn.async_send_text("Hello WebSocket!");
            std::println("  Sent: Hello WebSocket!");
            
            // 接收消息
            auto msg = co_await ws_conn.async_recv();
            if (msg) {
                std::println("  Received: {}", msg->as_string());
            }
            
            // 关闭连接
            co_await ws_conn.async_close();
            std::println("✓ WebSocket closed\n");
        } else {
            std::println("✗ Connection failed: {}\n", result.error().message());
        }
    }
    
    // 方法 2: 从 HTTP Client 升级（适用于需要先发送 HTTP 请求的场景）
    std::println("Method 2: Upgrade from HTTP Client");
    {
        // 注意：这个方法适用于需要先进行 HTTP 认证等操作的场景
        // 大多数情况下，方法 1 更简单直接
        
        client_options http_opts;
        http_opts.keep_alive = true;
        
        client http_client(*ctx, http_opts);
        
        // 1. 先进行 HTTP 认证（示例）
        std::println("  Step 1: HTTP authentication");
        auto auth_result = co_await http_client.get("https://example.com/auth");
        if (auth_result) {
            std::println("  ✓ Authenticated");
        }
        
        // 2. 发送 WebSocket 升级请求
        std::println("  Step 2: Send WebSocket upgrade request");
        
        auto sec_key = generate_sec_key();
        auto expected_accept = compute_accept_key(sec_key);
        
        auto upgrade_req = build_upgrade_request(
            "echo.websocket.org", "/", sec_key, "chat");
        
        auto upgrade_result = co_await http_client.send(upgrade_req);
        
        if (upgrade_result && upgrade_result->status_code() == 101) {
            std::println("  ✓ Upgrade response received (101)");
            
            // 3. 验证升级响应
            http::response_parser resp_parser;
            // 将 response 转换为 response_parser 进行验证
            // （实际使用中，可以直接在 send 中处理）
            
            // 4. 释放底层连接并转交给 WebSocket
            if (auto sock = http_client.release_connection()) {
                std::println("  ✓ Connection released from HTTP client");
                
                connection ws_conn(*ctx);
                ws_conn.attach(std::move(*sock), false);  // false = client mode
                
                std::println("  ✓ Connection attached to WebSocket");
                
                // 现在可以使用 WebSocket 连接
                co_await ws_conn.async_send_text("Hello from upgraded connection!");
                std::println("  Sent: Hello from upgraded connection!");
                
                auto msg = co_await ws_conn.async_recv();
                if (msg) {
                    std::println("  Received: {}", msg->as_string());
                }
                
                co_await ws_conn.async_close();
                std::println("  ✓ WebSocket closed\n");
            } else {
                std::println("  ✗ Failed to release connection\n");
            }
        } else {
            std::println("  ✗ Upgrade failed\n");
        }
    }
    
    std::println("=== Demo Complete ===\n");
    std::println("Summary:");
    std::println("- Method 1 (Direct): Simpler, recommended for most cases");
    std::println("- Method 2 (Upgrade): Useful when HTTP operations needed first");
    std::println("\nWebSocket features (from websocket module):");
    std::println("✓ Full RFC 6455 implementation");
    std::println("✓ Text and binary messages");
    std::println("✓ Ping/Pong automatic handling");
    std::println("✓ Fragmentation support");
    std::println("✓ TLS/SSL support (wss://)");
    std::println("✓ Subprotocol negotiation");
}

// =============================================================================
// 实际使用场景示例
// =============================================================================

auto practical_example() -> task<void> {
    std::println("\n=== Practical Example: Authenticated WebSocket ===\n");
    
    auto ctx = make_io_context();
    
    // 场景：需要先通过 HTTP 获取认证 token，然后建立 WebSocket 连接
    
    // 1. HTTP 认证获取 token
    std::println("Step 1: Get authentication token via HTTP");
    {
        client http_client(*ctx);
        
        request auth_req(http_method::POST, "https://api.example.com/auth");
        auth_req.set_header("Content-Type", "application/json");
        auth_req.set_body(R"({"username":"user","password":"pass"})");
        
        auto result = co_await http_client.send(auth_req);
        if (result) {
            std::println("✓ Got auth token (simulated)");
            // 实际应用中，从响应中提取 token
            // std::string token = extract_token(result->body());
        }
    }
    
    // 2. 使用 token 建立 WebSocket 连接
    std::println("\nStep 2: Connect to WebSocket with token");
    {
        connection ws_conn(*ctx);
        
        // 注意：实际的 token 认证通常在 WebSocket 握手的自定义头中
        // 或者在连接后的第一条消息中发送
        
        connect_options opts;
        opts.origin = "https://api.example.com";
        
        auto result = co_await ws_conn.async_connect(
            "wss://api.example.com/ws", opts);
        
        if (result) {
            std::println("✓ WebSocket connected");
            
            // 发送认证消息
            co_await ws_conn.async_send_text(R"({"type":"auth","token":"xxx"})");
            std::println("✓ Auth message sent");
            
            // 接收认证确认
            auto msg = co_await ws_conn.async_recv();
            if (msg) {
                std::println("✓ Auth confirmed: {}", msg->as_string());
            }
            
            // 正常通信
            co_await ws_conn.async_send_text(R"({"type":"message","data":"Hello"})");
            
            co_await ws_conn.async_close();
            std::println("✓ Connection closed");
        }
    }
    
    std::println("\n=== Practical Example Complete ===");
}

// =============================================================================
// 主函数
// =============================================================================

auto main() -> int {
    auto ctx = make_io_context();
    
    spawn(*ctx, []() -> task<void> {
        co_await demo_websocket_upgrade();
        co_await practical_example();
    }());
    
    ctx->run();
    
    return 0;
}
