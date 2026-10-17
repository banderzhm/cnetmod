// HttpArena baseline-h3 adapter. Keep the application workload identical when
// comparing transport revisions; do not replace query parsing with a constant.
import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;

namespace {

auto handle_request(cnetmod::http::v3::http3_request& request,
    cnetmod::http::v3::http3_response& response) -> std::error_code
{
    namespace http = cnetmod::http;
    const auto target = std::string_view{request.path};
    const auto separator = target.find('?');
    if (target.substr(0, separator) != "/baseline2" || request.method != http::http_method::GET)
    {
        response.status = 404;
        return {};
    }
    std::int64_t sum{};
    auto query = separator == std::string_view::npos ? std::string_view{} : target.substr(separator + 1);
    while (!query.empty())
    {
        const auto next = query.find('&');
        const auto field = query.substr(0, next);
        const auto equals = field.find('=');
        if (equals != std::string_view::npos)
        {
            const auto decoded = http::url_decode(field.substr(equals + 1));
            auto value = std::string_view{decoded};
            if (value.starts_with('+'))
            {
                value.remove_prefix(1);
                if (value.starts_with('-'))
                    value = {};
            }
            std::int64_t number{};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
            if (error == std::errc{} && end == value.data() + value.size() &&
                !(number > 0 && sum > std::numeric_limits<std::int64_t>::max() - number) &&
                !(number < 0 && sum < std::numeric_limits<std::int64_t>::min() - number))
                sum += number;
        }
        if (next == std::string_view::npos)
            break;
        query.remove_prefix(next + 1);
    }
    response.headers.emplace("content-type", "text/plain");
    response.body = std::to_string(sum);
    return {};
}

} // namespace

auto main(int argc, char** argv) -> int
{
    cnetmod::net_init network;
    logger::init("arena-h3", logger::level::warn);
    if (argc != 5)
    {
        logger::error{"usage: arena_h3_server CERT KEY PORT WORKERS"};
        return 2;
    }
    unsigned port{}, workers{};
    const auto parse = [](std::string_view text, unsigned& value)
    {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() && value > 0;
    };
    if (!parse(argv[3], port) || port > 65535 || !parse(argv[4], workers))
        return 2;
    auto tls = cnetmod::ssl_context::quic_server();
    if (!tls)
        return 1;
    tls->configure_alpn_server({"h3"});
    if (!tls->load_cert_file(argv[1]) || !tls->load_key_file(argv[2]))
        return 1;
    cnetmod::server_context context{workers, workers};
    auto server = cnetmod::http::v3::make_http3_server(context, *tls,
        cnetmod::endpoint{cnetmod::ipv4_address::any(), static_cast<std::uint16_t>(port)}, handle_request);
    if (const auto result = server->start(); !result)
    {
        logger::error{"HTTP/3 start failed: {}", result.error().message()};
        return 1;
    }
    context.run();
    logger::shutdown();
    return 0;
}
