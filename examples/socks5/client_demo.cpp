import std;
import cnetmod.io.io_context;
import cnetmod.io.platform;
import cnetmod.coro.task;
import cnetmod.coro.sync_wait;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

// Example 1: Simple HTTP request through SOCKS5
auto example_http_through_socks5(io_context& ctx) -> task<void> {
    std::println("\n=== Example 1: HTTP Request through SOCKS5 ===\n");
    
    // Create SOCKS5 client
    client socks_client(ctx);
    
    // Connect to SOCKS5 proxy
    std::println("1. Connecting to SOCKS5 proxy at 127.0.0.1:1080...");
    auto conn_r = co_await socks_client.connect("127.0.0.1", 1080);
    if (!conn_r) {
        std::println("   Failed: {}", conn_r.error().message());
        co_return;
    }
    std::println("   Connected to proxy");
    
    // Authenticate (try with credentials)
    std::println("2. Authenticating with username/password...");
    auto auth_r = co_await socks_client.authenticate("user", "pass");
    if (!auth_r) {
        std::println("   Authentication failed: {}", auth_r.error().message());
        co_return;
    }
    std::println("   Authenticated successfully");
    
    // Connect to target through proxy
    std::println("3. Connecting to httpbin.org:80 through proxy...");
    auto target_r = co_await socks_client.connect_target("httpbin.org", 80);
    if (!target_r) {
        std::println("   Failed: {}", target_r.error().message());
        co_return;
    }
    std::println("   Connected to target");
    
    // Send HTTP request
    std::println("4. Sending HTTP GET request...");
    std::string http_request = 
        "GET /ip HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "User-Agent: cnetmod-socks5-client/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    
    auto& sock = socks_client.socket();
    auto write_r = co_await async_write(ctx, sock,
        const_buffer{http_request.data(), http_request.size()});
    if (!write_r) {
        std::println("   Write failed: {}", write_r.error().message());
        co_return;
    }
    std::println("   Request sent");
    
    // Receive HTTP response
    std::println("5. Receiving HTTP response...\n");
    std::array<std::byte, 4096> buf;
    auto read_r = co_await async_read(ctx, sock,
        mutable_buffer{buf.data(), buf.size()});
    if (!read_r || *read_r == 0) {
        std::println("   Read failed");
        co_return;
    }
    
    std::string response(reinterpret_cast<const char*>(buf.data()), *read_r);
    std::println("Response:\n{}", response);
    
    socks_client.close();
}

// Example 2: Connect with no authentication
auto example_no_auth(io_context& ctx) -> task<void> {
    std::println("\n=== Example 2: No Authentication ===\n");
    
    client socks_client(ctx);
    
    std::println("Connecting to proxy (no auth)...");
    auto conn_r = co_await socks_client.connect("127.0.0.1", 1080);
    if (!conn_r) {
        std::println("Failed: {}", conn_r.error().message());
        co_return;
    }
    
    // No authentication needed
    auto auth_r = co_await socks_client.authenticate("", "");
    if (!auth_r) {
        std::println("Failed: {}", auth_r.error().message());
        co_return;
    }
    
    std::println("Connecting to example.com:80...");
    auto target_r = co_await socks_client.connect_target("example.com", 80);
    if (!target_r) {
        std::println("Failed: {}", target_r.error().message());
        co_return;
    }
    
    std::println("Success! Connected to example.com through SOCKS5 proxy");
    socks_client.close();
}

// Example 3: Connect using IP address
auto example_ip_address(io_context& ctx) -> task<void> {
    std::println("\n=== Example 3: Connect using IP Address ===\n");
    
    client socks_client(ctx);
    
    std::println("Connecting to proxy...");
    auto conn_r = co_await socks_client.connect("127.0.0.1", 1080);
    if (!conn_r) {
        std::println("Failed: {}", conn_r.error().message());
        co_return;
    }
    
    auto auth_r = co_await socks_client.authenticate("admin", "secret");
    if (!auth_r) {
        std::println("Auth failed: {}", auth_r.error().message());
        co_return;
    }
    
    // Connect using IP address instead of domain name
    std::println("Connecting to 8.8.8.8:53 (Google DNS)...");
    auto target_r = co_await socks_client.connect_target("8.8.8.8", 53);
    if (!target_r) {
        std::println("Failed: {}", target_r.error().message());
        co_return;
    }
    
    std::println("Success! Connected to 8.8.8.8:53 through SOCKS5 proxy");
    socks_client.close();
}

// Example 4: Error handling
auto example_error_handling(io_context& ctx) -> task<void> {
    std::println("\n=== Example 4: Error Handling ===\n");
    
    client socks_client(ctx);
    
    // Try to connect to non-existent proxy
    std::println("Attempting to connect to non-existent proxy...");
    auto conn_r = co_await socks_client.connect("192.0.2.1", 1080);
    if (!conn_r) {
        std::println("Expected failure: {}", conn_r.error().message());
    }
    
    // Connect to real proxy
    socks_client = client(ctx);
    conn_r = co_await socks_client.connect("127.0.0.1", 1080);
    if (!conn_r) {
        std::println("Proxy connection failed: {}", conn_r.error().message());
        co_return;
    }
    
    // Try wrong credentials
    std::println("\nAttempting authentication with wrong credentials...");
    auto auth_r = co_await socks_client.authenticate("wrong", "credentials");
    if (!auth_r) {
        std::println("Expected failure: {}", auth_r.error().message());
    }
    
    // Try to connect to unreachable host
    socks_client = client(ctx);
    conn_r = co_await socks_client.connect("127.0.0.1", 1080);
    if (conn_r) {
        co_await socks_client.authenticate("user", "pass");
        
        std::println("\nAttempting to connect to unreachable host...");
        auto target_r = co_await socks_client.connect_target("192.0.2.1", 80);
        if (!target_r) {
            std::println("Expected failure: {}", target_r.error().message());
        }
    }
}

auto main(int argc, char* argv[]) -> int {
    try {
        io_context ctx;
        
        std::println("=== SOCKS5 Client Demo ===");
        std::println("Make sure SOCKS5 server is running on 127.0.0.1:1080\n");
        
        if (argc > 1) {
            std::string example = argv[1];
            
            if (example == "1" || example == "http") {
                sync_wait(example_http_through_socks5(ctx));
            } else if (example == "2" || example == "noauth") {
                sync_wait(example_no_auth(ctx));
            } else if (example == "3" || example == "ip") {
                sync_wait(example_ip_address(ctx));
            } else if (example == "4" || example == "error") {
                sync_wait(example_error_handling(ctx));
            } else {
                std::println("Unknown example: {}", example);
                std::println("Usage: {} [1|2|3|4|http|noauth|ip|error]", argv[0]);
                return 1;
            }
        } else {
            // Run all examples
            sync_wait(example_http_through_socks5(ctx));
            sync_wait(example_no_auth(ctx));
            sync_wait(example_ip_address(ctx));
            sync_wait(example_error_handling(ctx));
        }
        
        std::println("\n=== All examples completed ===");
        
    } catch (const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        return 1;
    }
    return 0;
}

