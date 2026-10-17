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
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;

namespace {

struct static_asset
{
    std::shared_ptr<const std::string> identity;
    std::shared_ptr<const std::string> brotli;
    std::string content_type;
};

using asset_cache = std::unordered_map<std::string, static_asset>;

auto load_asset_file(const std::filesystem::path& path)
    -> std::shared_ptr<const std::string>
{
    std::ifstream input{path, std::ios::binary};
    if (!input)
        throw std::system_error{std::make_error_code(std::errc::io_error),
            path.string()};
    auto content = std::make_shared<std::string>(
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    if (input.bad() || content->size() != std::filesystem::file_size(path))
        throw std::system_error{std::make_error_code(std::errc::io_error),
            path.string()};
    return content;
}

// The official fixture is immutable for a benchmark process. Cache it before
// starting the event loops so concurrent requests do not amplify the 64 x 64
// static profile into sustained file-I/O and temporary read buffers.
auto load_assets(const std::filesystem::path& root) -> asset_cache
{
    asset_cache assets;
    for (const auto& entry : std::filesystem::directory_iterator{root})
    {
        if (!entry.is_regular_file() || entry.path().extension() == ".br" ||
            entry.path().extension() == ".gz")
            continue;
        static_asset asset;
        asset.identity = load_asset_file(entry.path());
        asset.content_type = cnetmod::http::guess_mime_type(entry.path().extension().string());
        auto compressed = entry.path();
        compressed += ".br";
        if (std::filesystem::exists(compressed))
            asset.brotli = load_asset_file(compressed);
        assets.emplace("/static/" + entry.path().filename().string(), std::move(asset));
    }
    return assets;
}

auto serve_asset(const asset_cache& assets, cnetmod::http::v3::http3_request& request,
    cnetmod::http::v3::http3_response& response) -> std::error_code
{
    const auto found = assets.find(request.path.substr(0, request.path.find('?')));
    if (found == assets.end())
    {
        response.status = 404;
        return {};
    }
    // This adapter accepts the official profile's exact negotiation or an
    // identity request for independent byte validation. Other negotiations
    // are rejected explicitly rather than silently misinterpreting q-values.
    const auto encoding = request.headers.find("accept-encoding");
    const bool brotli = encoding != request.headers.end() &&
        (encoding->second == "br;q=1, gzip;q=0.8" || encoding->second == "br");
    if (encoding != request.headers.end() && !encoding->second.empty() &&
        encoding->second != "identity" && !brotli)
    {
        response.status = 406;
        return {};
    }
    const auto& asset = found->second;
    const auto use_brotli = brotli && static_cast<bool>(asset.brotli);
    const auto& content = use_brotli ? asset.brotli : asset.identity;
    response.headers.reserve(3U);
    response.headers.emplace("content-type", asset.content_type);
    response.headers.emplace("vary", "Accept-Encoding");
    if (use_brotli)
        response.headers.emplace("content-encoding", "br");
    response.shared_body = content;
    return {};
}

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
    auto tls = cnetmod::ssl_context::quic_server();
    if (!tls)
        return 1;
    tls->configure_alpn_server({"h3"});
    if (!tls->load_cert_file(argv[1]) || !tls->load_key_file(argv[2]))
        return 1;
    asset_cache assets;
    if (argc == 6)
    {
        try
        {
            assets = load_assets(argv[5]);
        }
        catch (const std::exception& error)
        {
            logger::error{"{}", error.what()};
            return 1;
        }
    }
    cnetmod::http::v3::server_request_handler handler = [&assets](
                                                            cnetmod::http::v3::http3_request& request,
                                                            cnetmod::http::v3::http3_response& response) -> std::error_code
    {
        if (request.path.starts_with("/static/") &&
            request.method == cnetmod::http::http_method::GET)
            return serve_asset(assets, request, response);
        return handle_request(request, response);
    };
    cnetmod::server_context context{workers, workers};
    const cnetmod::endpoint endpoint{cnetmod::ipv4_address::any(), static_cast<std::uint16_t>(port)};
    auto server = cnetmod::http::v3::make_http3_server(
        context, *tls, endpoint, std::move(handler));
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
    if (const auto configured = server->set_path_mtu_discovery(1472,
            std::chrono::milliseconds{1});
        !configured)
    {
        logger::error{"HTTP/3 path-MTU setup failed: {}",
            configured.error().message()};
        return 1;
    }
    if (const auto configured = server->set_max_udp_payload_size(1472);
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
