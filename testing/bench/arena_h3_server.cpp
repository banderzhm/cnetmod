// HttpArena HTTP/3 adapter. Keep the application workload identical when
// comparing transport revisions; do not replace query parsing with a constant.
import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.executor.pool;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.http.v3.qpack;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;
import cnetmod.protocol.http.v3.static_files;

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
    if (argc != 5 && argc != 6)
    {
        logger::error{"usage: arena_h3_server CERT KEY PORT WORKERS [OFFICIAL_STATIC_DIR]"};
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
    // Explicit diagnostic ceilings. Keep receive capacity fixed when comparing
    // send discovery limits so one binary and buffer layout serve every round.
    unsigned max_path_mtu{1472U}, max_udp_payload_size{1472U};
    const auto parse_udp_limit = [&parse](const char* name, unsigned& value)
    {
        const auto* text = std::getenv(name);
        if (!text)
            return true;
        if (!parse(text, value) || value < 1200U || value > 65507U)
        {
            logger::error{"{} must be a complete integer in [1200, 65507]", name};
            return false;
        }
        return true;
    };
    if (!parse_udp_limit("CNETMOD_QUIC_MAX_PATH_MTU", max_path_mtu) ||
        !parse_udp_limit("CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE", max_udp_payload_size))
        return 2;
    auto tls = cnetmod::ssl_context::quic_server();
    if (!tls)
        return 1;
    tls->configure_alpn_server({"h3"});
    if (!tls->load_cert_file(argv[1]) || !tls->load_key_file(argv[2]))
        return 1;
    cnetmod::http::v3::static_file_handler static_files;
    if (argc == 6)
        static_files = cnetmod::http::v3::serve_dir({.root = argv[5]});

    cnetmod::http::v3::server_request_handler handler = [static_files =
                                                                std::move(static_files)](
                                                            cnetmod::http::v3::http3_request& request,
                                                            cnetmod::http::v3::http3_response& response) -> std::error_code
    {
        if (static_files)
        {
            auto served = static_files(request, response);
            if (!served)
                return served.error();
            if (*served)
                return {};
        }
        return handle_request(request, response);
    };
    cnetmod::server_context context{workers, workers};
    const cnetmod::endpoint endpoint{cnetmod::ipv4_address::any(), static_cast<std::uint16_t>(port)};
    auto server = cnetmod::http::v3::make_http3_server(
        context, *tls, endpoint, std::move(handler));
    // Explicit same-binary diagnostic switch; ordinary/official runs keep
    // the established path unless the environment opts in before start.
    if (const auto* gso = std::getenv("CNETMOD_QUIC_GSO");
        gso && std::string_view{gso} == "1")
    {
        if (const auto configured = server->set_udp_segmentation_offload(true);
            !configured)
        {
            logger::error{"HTTP/3 UDP offload setup failed: {}", configured.error().message()};
            return 1;
        }
    }
    // HttpArena creates fresh h2load connections for every short run and the
    // load generator leaves them silent instead of sending CONNECTION_CLOSE.
    // Reclaim those connections within the official two-second round cooldown
    // so later rounds do not retain complete connection and recovery state.
    if (const auto configured = server->set_idle_timeout(std::chrono::milliseconds{1500});
        !configured)
    {
        logger::error{"HTTP/3 idle-timeout setup failed: {}",
            configured.error().message()};
        return 1;
    }
    // The official workload is loopback-only and its peer advertises support
    // for larger datagrams. Probe the controlled local path before using its
    // larger payload ceiling so static responses need fewer encrypted packets.
    if (const auto configured = server->set_path_mtu_discovery(max_path_mtu,
            std::chrono::milliseconds{1});
        !configured)
    {
        logger::error{"HTTP/3 path-MTU setup failed: {}",
            configured.error().message()};
        return 1;
    }
    if (const auto configured = server->set_max_udp_payload_size(max_udp_payload_size);
        !configured)
    {
        logger::error{"HTTP/3 UDP payload setup failed: {}",
            configured.error().message()};
        return 1;
    }
    if (const auto result = server->start(); !result)
    {
        logger::error{"HTTP/3 start failed: {}", result.error().message()};
        return 1;
    }
    context.run();
    logger::shutdown();
    return 0;
}
