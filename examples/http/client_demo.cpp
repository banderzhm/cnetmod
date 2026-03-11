// HTTP Client Example - Async version with coroutines
// Demonstrates unified HTTP/HTTPS client with HTTP/1.1 and HTTP/2 support

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

using namespace cnetmod;
using namespace cnetmod::http;

auto run_examples(client& http_client) -> task<void> {
    // Example 1: Simple HTTP GET request
    std::println("=== Example 1: HTTP GET Request ===");
    {
        auto result = co_await http_client.get("http://httpbin.org/get");
        if (result) {
            std::println("Status: {}", result->status_code());
            std::println("Body: {}", result->body());
        } else {
            std::println("Error: {}", result.error().message());
        }
    }

#ifdef CNETMOD_HAS_SSL
    // Example 2: HTTPS GET request
    std::println("\n=== Example 2: HTTPS GET Request ===");
    {
        auto result = co_await http_client.get("https://httpbin.org/get");
        if (result) {
            std::println("Status: {}", result->status_code());
            std::println("Body length: {} bytes", result->body().size());
        } else {
            std::println("Error: {}", result.error().message());
        }
    }

#ifdef CNETMOD_HAS_NGHTTP2
    // Example 3: HTTP/2 request (automatic negotiation via ALPN)
    std::println("\n=== Example 3: HTTP/2 Request (ALPN) ===");
    {
        auto result = co_await http_client.get("https://http2.golang.org/");
        if (result) {
            std::println("Status: {}", result->status_code());
            std::println("Protocol: HTTP/2 (negotiated via ALPN)");
            std::println("Body length: {} bytes", result->body().size());
        } else {
            std::println("Error: {}", result.error().message());
        }
    }
#endif
#endif

    // Example 4: POST request with JSON body
    std::println("\n=== Example 4: POST Request ===");
    {
        std::string json_body = R"({"name": "test", "value": 123})";
        
        request req(http_method::POST, "http://httpbin.org/post");
        req.set_header("Content-Type", "application/json");
        req.set_body(json_body);

        auto result = co_await http_client.send(req);
        if (result) {
            std::println("Status: {}", result->status_code());
        } else {
            std::println("Error: {}", result.error().message());
        }
    }

    std::println("\n=== Supported Features ===");
    std::println("✓ HTTP/1.1");
#ifdef CNETMOD_HAS_SSL
    std::println("✓ HTTPS (SSL/TLS)");
#ifdef CNETMOD_HAS_NGHTTP2
    std::println("✓ HTTP/2 (via ALPN)");
    std::println("✓ Automatic protocol negotiation");
#else
    std::println("✗ HTTP/2 (nghttp2 not enabled)");
#endif
#else
    std::println("✗ HTTPS (OpenSSL not enabled)");
    std::println("✗ HTTP/2 (requires SSL for ALPN)");
#endif
    std::println("✓ Connection reuse (Keep-Alive)");
    std::println("✓ Custom headers");
    std::println("✓ All HTTP methods");
    std::println("✓ Automatic redirect following");
    std::println("✓ Fully asynchronous with coroutines");
}

auto main() -> int {
    auto ctx = make_io_context();

    client_options opts;
    opts.connect_timeout = std::chrono::seconds(5);
    opts.request_timeout = std::chrono::seconds(30);
    opts.follow_redirects = true;
    opts.keep_alive = true;
    opts.user_agent = "cnetmod-example/1.0";
    opts.verify_peer = true;
    opts.version_pref = http_version_preference::http2_preferred;

    client http_client(*ctx, opts);

    // Run examples as coroutine
    spawn(*ctx, run_examples(http_client));
    
    // Run event loop
    ctx->run();

    return 0;
}
