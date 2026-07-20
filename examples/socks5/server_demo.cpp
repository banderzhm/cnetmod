import std;
import cnetmod.io.io_context;
import cnetmod.io.platform;
import cnetmod.coro.task;
import cnetmod.coro.sync_wait;
import cnetmod.executor.pool;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

auto demo_socks5_server(server_context& sctx) -> task<void> {
    std::println("=== SOCKS5 Multi-Core Server Demo ===\n");
    
    // Configure server
    server_config config;
    config.allow_no_auth = true;
    config.allow_username_password = true;
    config.authenticator = [](std::string_view username, std::string_view password) {
        std::println("[Auth] Attempt: user='{}', pass='{}'", username, password);
        
        // Simple authentication: admin/secret or user/pass
        if (username == "admin" && password == "secret") {
            std::println("[Auth] Success: admin user");
            return true;
        }
        if (username == "user" && password == "pass") {
            std::println("[Auth] Success: regular user");
            return true;
        }
        
        std::println("[Auth] Failed: invalid credentials");
        return false;
    };
    config.max_connections = 1000;
    
    // Create multi-core server
    server socks_server(sctx, config);
    
    // Listen
    auto listen_r = socks_server.listen("0.0.0.0", 1080);
    if (!listen_r) {
        std::println("Failed to listen: {}", listen_r.error().message());
        co_return;
    }
    
    std::println("SOCKS5 server listening on 0.0.0.0:1080");
    std::println("Worker threads: {}", sctx.worker_count());
    std::println("\nConfiguration:");
    std::println("  - No authentication: enabled");
    std::println("  - Username/Password: enabled");
    std::println("    * admin/secret (admin user)");
    std::println("    * user/pass (regular user)");
    std::println("  - Max connections: {}", config.max_connections);
    std::println("\nPress Ctrl+C to stop\n");
    
    // Statistics reporting task
    spawn(sctx.accept_io(), [&socks_server]() -> task<void> {
        while (true) {
            co_await async_sleep(std::chrono::seconds(10));
            std::println("[Stats] Active connections: {}", 
                socks_server.active_connections());
        }
    }());
    
    // Run server
    co_await socks_server.run();
}

auto main(int argc, char* argv[]) -> int {
    try {
        // Parse command line arguments
        std::size_t worker_count = std::thread::hardware_concurrency();
        
        if (argc > 1) {
            try {
                worker_count = std::stoull(argv[1]);
                if (worker_count == 0) {
                    worker_count = std::thread::hardware_concurrency();
                }
            } catch (...) {
                std::println(stderr, "Invalid worker count, using default");
            }
        }
        
        std::println("Starting SOCKS5 server with {} worker threads...\n", worker_count);
        
        // Create server context with worker pool
        server_context sctx(worker_count);
        
        // Run server
        sync_wait(demo_socks5_server(sctx));
        
    } catch (const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        return 1;
    }
    return 0;
}

