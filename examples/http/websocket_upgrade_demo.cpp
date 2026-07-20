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
// WebSocket
// =============================================================================

auto demo_websocket_upgrade() -> task<void> {
    std::println("=== HTTP to WebSocket Upgrade Demo ===\n");
    
    auto ctx = make_io_context();
    
    // 1: WebSocket ()
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
            
            // Send messages
            co_await ws_conn.async_send_text("Hello WebSocket!");
            std::println("  Sent: Hello WebSocket!");
            
            // Implementation note.
            auto msg = co_await ws_conn.async_recv();
            if (msg) {
                std::println("  Received: {}", msg->as_string());
            }
            
            // Implementation note.
            co_await ws_conn.async_close();
            std::println("✓ WebSocket closed\n");
        } else {
            std::println("✗ Connection failed: {}\n", result.error().message());
        }
    }
    
    // 2: HTTP Client ( HTTP request)
    std::println("Method 2: Upgrade from HTTP Client");
    {
        // Note: HTTP authentication
        // 1 simple
        
        client_options http_opts;
        http_opts.keep_alive = true;
        
        client http_client(*ctx, http_opts);
        
        // 1. HTTP authentication()
        std::println("  Step 1: HTTP authentication");
        auto auth_result = co_await http_client.get("https://example.com/auth");
        if (auth_result) {
            std::println("  ✓ Authenticated");
        }
        
        // 2. WebSocket request
        std::println("  Step 2: Send WebSocket upgrade request");
        
        auto sec_key = generate_sec_key();
        auto expected_accept = compute_accept_key(sec_key);
        
        auto upgrade_req = build_upgrade_request(
            "echo.websocket.org", "/", sec_key, "chat");
        
        auto upgrade_result = co_await http_client.send(upgrade_req);
        
        if (upgrade_result && upgrade_result->status_code() == 101) {
            std::println("  ✓ Upgrade response received (101)");
            
            // 3. verifyresponse
            http::response_parser resp_parser;
            // Response response_parser verify
            // (real, send )
            
            // 4. WebSocket
            if (auto sock = http_client.release_connection()) {
                std::println("  ✓ Connection released from HTTP client");
                
                connection ws_conn(*ctx);
                ws_conn.attach(std::move(*sock), false);  // false = client mode
                
                std::println("  ✓ Connection attached to WebSocket");
                
                // WebSocket
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
// Implementation note.
// =============================================================================

auto practical_example() -> task<void> {
    std::println("\n=== Practical Example: Authenticated WebSocket ===\n");
    
    auto ctx = make_io_context();
    
    // HTTP authentication token, WebSocket
    
    // 1. HTTP authentication token
    std::println("Step 1: Get authentication token via HTTP");
    {
        client http_client(*ctx);
        
        request auth_req(http_method::POST, "https://api.example.com/auth");
        auth_req.set_header("Content-Type", "application/json");
        auth_req.set_body(R"({"username":"user","password":"pass"})");
        
        auto result = co_await http_client.send(auth_req);
        if (result) {
            std::println("✓ Got auth token (simulated)");
            // Real, response token
            // std::string token = extract_token(result->body());
        }
    }
    
    // 2. token WebSocket
    std::println("\nStep 2: Connect to WebSocket with token");
    {
        connection ws_conn(*ctx);
        
        // Note: real token authenticationusually WebSocket
        // Implementation note.
        
        connect_options opts;
        opts.origin = "https://api.example.com";
        
        auto result = co_await ws_conn.async_connect(
            "wss://api.example.com/ws", opts);
        
        if (result) {
            std::println("✓ WebSocket connected");
            
            // Authentication
            co_await ws_conn.async_send_text(R"({"type":"auth","token":"xxx"})");
            std::println("✓ Auth message sent");
            
            // Authentication
            auto msg = co_await ws_conn.async_recv();
            if (msg) {
                std::println("✓ Auth confirmed: {}", msg->as_string());
            }
            
            // Implementation note.
            co_await ws_conn.async_send_text(R"({"type":"message","data":"Hello"})");
            
            co_await ws_conn.async_close();
            std::println("✓ Connection closed");
        }
    }
    
    std::println("\n=== Practical Example Complete ===");
}

// =============================================================================
// Main function
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
