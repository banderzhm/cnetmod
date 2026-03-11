// HTTP Client Unit Tests

import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;

using namespace cnetmod;
using namespace cnetmod::http;

// Test URL parsing
auto test_url_parsing() -> bool {
    std::println("Testing URL parsing...");
    
    // Test basic HTTP URL
    auto result1 = url::parse("http://example.com/path");
    if (!result1) {
        std::println("  FAIL: Basic URL parsing failed");
        return false;
    }
    if (result1->scheme != "http" || result1->host != "example.com" || 
        result1->port != 80 || result1->path != "/path") {
        std::println("  FAIL: Basic URL components incorrect");
        return false;
    }
    
    // Test URL with port
    auto result2 = url::parse("http://example.com:8080/api/data");
    if (!result2 || result2->port != 8080) {
        std::println("  FAIL: URL with port parsing failed");
        return false;
    }
    
    // Test URL with query
    auto result3 = url::parse("http://example.com/search?q=test&page=1");
    if (!result3 || result3->query != "q=test&page=1") {
        std::println("  FAIL: URL with query parsing failed");
        return false;
    }
    
    std::println("  PASS: URL parsing");
    return true;
}

// Test request building
auto test_request_building() -> bool {
    std::println("Testing request building...");
    
    request req(http_method::GET, "/api/data");
    req.set_header("Host", "example.com");
    req.set_header("User-Agent", "test-client");
    
    auto serialized = req.serialize();
    
    if (serialized.find("GET /api/data HTTP/1.1") == std::string::npos) {
        std::println("  FAIL: Request line incorrect");
        return false;
    }
    
    if (serialized.find("Host: example.com") == std::string::npos) {
        std::println("  FAIL: Host header missing");
        return false;
    }
    
    std::println("  PASS: Request building");
    return true;
}

// Test request with body
auto test_request_with_body() -> bool {
    std::println("Testing request with body...");
    
    request req(http_method::POST, "/api/data");
    req.set_header("Content-Type", "application/json");
    req.set_body(R"({"key":"value"})");
    
    auto serialized = req.serialize();
    
    if (serialized.find("Content-Length: 15") == std::string::npos) {
        std::println("  FAIL: Content-Length not set correctly");
        return false;
    }
    
    if (serialized.find(R"({"key":"value"})") == std::string::npos) {
        std::println("  FAIL: Body not included");
        return false;
    }
    
    std::println("  PASS: Request with body");
    return true;
}

// Test response building
auto test_response_building() -> bool {
    std::println("Testing response building...");
    
    response resp(200);
    resp.set_header("Content-Type", "text/plain");
    resp.set_body("Hello, World!");
    
    auto serialized = resp.serialize();
    
    if (serialized.find("HTTP/1.1 200 OK") == std::string::npos) {
        std::println("  FAIL: Status line incorrect");
        return false;
    }
    
    if (serialized.find("Content-Length: 13") == std::string::npos) {
        std::println("  FAIL: Content-Length not set correctly");
        return false;
    }
    
    if (serialized.find("Hello, World!") == std::string::npos) {
        std::println("  FAIL: Body not included");
        return false;
    }
    
    std::println("  PASS: Response building");
    return true;
}

// Test client options
auto test_client_options() -> bool {
    std::println("Testing client options...");
    
    io_context ctx;
    
    client_options opts;
    opts.connect_timeout = std::chrono::seconds(10);
    opts.request_timeout = std::chrono::seconds(60);
    opts.follow_redirects = false;
    opts.keep_alive = false;
    opts.user_agent = "custom-agent/1.0";
    
    client http_client(ctx, opts);
    
    const auto& retrieved_opts = http_client.options();
    if (retrieved_opts.connect_timeout != std::chrono::seconds(10)) {
        std::println("  FAIL: Connect timeout not set correctly");
        return false;
    }
    
    if (retrieved_opts.user_agent != "custom-agent/1.0") {
        std::println("  FAIL: User agent not set correctly");
        return false;
    }
    
    std::println("  PASS: Client options");
    return true;
}

auto main() -> int {
    std::println("=== HTTP Client Tests ===\n");
    
    int passed = 0;
    int total = 0;
    
    auto run_test = [&](auto test_func) {
        total++;
        if (test_func()) {
            passed++;
        }
    };
    
    run_test(test_url_parsing);
    run_test(test_request_building);
    run_test(test_request_with_body);
    run_test(test_response_building);
    run_test(test_client_options);
    
    std::println("\n=== Results ===");
    std::println("Passed: {}/{}", passed, total);
    
    return (passed == total) ? 0 : 1;
}
