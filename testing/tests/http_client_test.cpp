// HTTP Client Unit Tests

import std;
import cnetmod.core.log;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.http;

using namespace cnetmod;
using namespace cnetmod::http;

// Test URL parsing
auto test_url_parsing() -> bool
{
    logger::info{"Testing URL parsing"};

    // Test basic HTTP URL
    auto result1 = url::parse("http://example.com/path");
    if (!result1)
    {
        logger::error{"Basic URL parsing failed"};
        return false;
    }
    if (result1->scheme != "http" || result1->host != "example.com" ||
        result1->port != 80 || result1->path != "/path")
    {
        logger::error{"Basic URL components incorrect"};
        return false;
    }

    // Test URL with port
    auto result2 = url::parse("http://example.com:8080/api/data");
    if (!result2 || result2->port != 8080)
    {
        logger::error{"URL with port parsing failed"};
        return false;
    }

    // Test URL with query
    auto result3 = url::parse("http://example.com/search?q=test&page=1");
    if (!result3 || result3->query != "q=test&page=1")
    {
        logger::error{"URL with query parsing failed"};
        return false;
    }

    logger::info{"URL parsing passed"};
    return true;
}

// Test request building
auto test_request_building() -> bool
{
    logger::info{"Testing request building"};

    request req(http_method::GET, "/api/data");
    req.set_header("Host", "example.com");
    req.set_header("User-Agent", "test-client");

    auto serialized = req.serialize();

    if (serialized.find("GET /api/data HTTP/1.1") == std::string::npos)
    {
        logger::error{"Request line incorrect"};
        return false;
    }

    if (serialized.find("Host: example.com") == std::string::npos)
    {
        logger::error{"Host header missing"};
        return false;
    }

    logger::info{"Request building passed"};
    return true;
}

// Test request with body
auto test_request_with_body() -> bool
{
    logger::info{"Testing request with body"};

    request req(http_method::POST, "/api/data");
    req.set_header("Content-Type", "application/json");
    req.set_body(std::string_view{R"({"key":"value"})"});

    auto serialized = req.serialize();

    if (serialized.find("Content-Length: 15") == std::string::npos)
    {
        logger::error{"Content-Length not set correctly"};
        return false;
    }

    if (serialized.find(R"({"key":"value"})") == std::string::npos)
    {
        logger::error{"Body not included"};
        return false;
    }

    logger::info{"Request body serialization passed"};
    return true;
}

// A streaming request body is pull-driven: the producer is not asked for the
// next chunk until the caller has consumed the previous one.
auto test_request_body_stream_source() -> bool
{
    logger::info{"Testing streaming request body source"};

    auto index = std::make_shared<std::size_t>(0);
    auto source = std::make_shared<request_body_source>(
        [index](cancel_token& token)
            -> task<std::optional<request_body_chunk>>
        {
            if (token.is_cancelled() || *index >= 2)
                co_return std::nullopt;
            const auto text = *index == 0 ? std::string_view{"part-a"}
                                          : std::string_view{"part-b"};
            ++*index;
            request_body_chunk chunk;
            chunk.insert(chunk.end(),
                reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data()) + text.size());
            co_return chunk;
        });

    request req(http_method::POST, "/upload");
    req.set_body_stream(source);
    if (!req.has_streaming_body() || req.body_source() != source ||
        !req.body().empty() || !req.get_header("Content-Length").empty())
    {
        logger::error{"Streaming source was not attached correctly"};
        return false;
    }

    cancel_token token;
    auto first = sync_wait(source->next(token));
    auto second = sync_wait(source->next(token));
    auto eof = sync_wait(source->next(token));
    if (!first || !second || eof ||
        std::string_view(reinterpret_cast<const char*>(first->data()), first->size()) != "part-a" ||
        std::string_view(reinterpret_cast<const char*>(second->data()), second->size()) != "part-b")
    {
        logger::error{"Streaming source pull sequence is incorrect"};
        return false;
    }
    logger::info{"Streaming request body source passed"};
    return true;
}

// Test response building
auto test_response_building() -> bool
{
    logger::info{"Testing response building"};

    response resp(200);
    resp.set_header("Content-Type", "text/plain");
    resp.set_body(std::string_view{"Hello, World!"});

    auto serialized = resp.serialize();

    if (serialized.find("HTTP/1.1 200 OK") == std::string::npos)
    {
        logger::error{"Status line incorrect"};
        return false;
    }

    if (serialized.find("Content-Length: 13") == std::string::npos)
    {
        logger::error{"Response Content-Length not set correctly"};
        return false;
    }

    if (serialized.find("Hello, World!") == std::string::npos)
    {
        logger::error{"Response body not included"};
        return false;
    }

    logger::info{"Response building passed"};
    return true;
}

// Test client options
auto test_client_options() -> bool
{
    logger::info{"Testing client options"};

    auto ctx = make_io_context();

    client_options opts;
    opts.connect_timeout = std::chrono::seconds(10);
    opts.request_timeout = std::chrono::seconds(60);
    opts.follow_redirects = false;
    opts.keep_alive = false;
    opts.user_agent = "custom-agent/1.0";
    opts.version_pref = http_version_preference::http3_preferred;
    opts.h3_qpack_max_table_capacity = 32 * 1024;
    opts.h3_qpack_blocked_streams = 32;
    opts.h3_max_concurrent_streams = 16;
    opts.http3_fallback_to_tcp = true;
    opts.enable_alt_svc_http3 = false;
    opts.http3_resumption_ticket_file = "ticket-cache.bin";
    opts.enable_http3_early_data = true;

    client http_client(*ctx, opts);

    const auto& retrieved_opts = http_client.options();
    if (retrieved_opts.connect_timeout != std::chrono::seconds(10))
    {
        logger::error{"Connect timeout not set correctly"};
        return false;
    }

    if (retrieved_opts.user_agent != "custom-agent/1.0")
    {
        logger::error{"User agent not set correctly"};
        return false;
    }

    if (retrieved_opts.version_pref != http_version_preference::http3_preferred ||
        retrieved_opts.h3_qpack_max_table_capacity != 32 * 1024 ||
        retrieved_opts.h3_qpack_blocked_streams != 32 ||
        retrieved_opts.h3_max_concurrent_streams != 16 ||
        !retrieved_opts.http3_fallback_to_tcp ||
        retrieved_opts.enable_alt_svc_http3 ||
        retrieved_opts.http3_resumption_ticket_file != "ticket-cache.bin" ||
        !retrieved_opts.enable_http3_early_data)
    {
        logger::error{"HTTP/3 options not set correctly"};
        return false;
    }

    logger::info{"Client options passed"};
    return true;
}

auto test_alt_svc_cache_load() -> bool
{
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    logger::info{"Testing persisted Alt-Svc cache"};
    const auto path = std::filesystem::temp_directory_path() /
        ("cnetmod-alt-svc-" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())) + ".cache");
    const auto expiry = std::chrono::duration_cast<std::chrono::milliseconds>(
        (std::chrono::system_clock::now() + std::chrono::hours{1}).time_since_epoch())
                            .count();
    {
        std::ofstream output(path, std::ios::trunc);
        output << "example.com:443\t8443\t" << expiry << '\n';
    }
    client_options opts;
    opts.alt_svc_cache_file = path.string();
    auto context = make_io_context();
    client http_client(*context, opts);
    const bool loaded = http_client.has_http3_alt_svc("example.com", 443);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (!loaded)
    {
        logger::error{"Persisted Alt-Svc entry was not loaded"};
        return false;
    }
    logger::info{"Persisted Alt-Svc cache passed"};
    return true;
#else
    return true;
#endif
}

auto main() -> int
{
    logger::info{"HTTP Client Tests"};

    int passed = 0;
    int total = 0;

    auto run_test = [&](auto test_func)
    {
        total++;
        if (test_func())
        {
            passed++;
        }
    };

    run_test(test_url_parsing);
    run_test(test_request_building);
    run_test(test_request_with_body);
    run_test(test_request_body_stream_source);
    run_test(test_response_building);
    run_test(test_client_options);
    run_test(test_alt_svc_cache_load);

    logger::info{"HTTP Client Tests: passed={}/{}", passed, total};

    return (passed == total) ? 0 : 1;
}
