// Local HTTP/3 endpoint used by the unified http::client end-to-end test.

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;

namespace {

struct options
{
    std::uint16_t port = 4433;
    std::string certificate = "cert.pem";
    std::string private_key = "key.pem";
    bool enable_early_data = true;
    bool enable_dynamic_qpack{};
    bool enable_multipath{};
    bool enable_path_mtu_discovery{};
    std::chrono::milliseconds path_mtu_initial_probe_delay{};
};

// The priority regression deliberately opens the background request first.
// Requests briefly rendezvous here so response DATA normally enters QUIC's
// scheduler together. The rendezvous is bounded because a dynamically QPACK-
// blocked request may need existing response traffic before its control-stream
// instructions are processed on a loaded event loop.
struct priority_fixture
{
    static constexpr std::size_t request_count = 3U;
    static constexpr std::size_t body_chunks = 24U;
    static constexpr std::size_t chunk_size = 64U;
    std::atomic<std::size_t> arrivals{};
    std::atomic<std::size_t> data_producers{};
    std::atomic<bool> data_released{};
};

struct batch_fixture
{
    static constexpr std::size_t request_count = 3U;
    std::atomic<std::size_t> arrivals{};
};

constexpr std::array ticket_magic{
    std::byte{'c'}, std::byte{'n'}, std::byte{'e'}, std::byte{'t'},
    std::byte{'m'}, std::byte{'o'}, std::byte{'d'}, std::byte{'-'},
    std::byte{'e'}, std::byte{'2'}, std::byte{'e'}, std::byte{'-'},
    std::byte{'t'}, std::byte{'i'}, std::byte{'c'}, std::byte{'k'}};

auto parse_options(int argc, char** argv) -> options
{
    options result;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg{argv[index]};
        if (arg == "--port" && index + 1 < argc)
            result.port = static_cast<std::uint16_t>(std::stoul(argv[++index]));
        else if (arg == "--cert" && index + 1 < argc)
            result.certificate = argv[++index];
        else if (arg == "--key" && index + 1 < argc)
            result.private_key = argv[++index];
        else if (arg == "--disable-early-data")
            result.enable_early_data = false;
        else if (arg == "--dynamic-qpack")
            result.enable_dynamic_qpack = true;
        else if (arg == "--multipath")
            result.enable_multipath = true;
        else if (arg == "--path-mtu-discovery")
            result.enable_path_mtu_discovery = true;
        else if (arg == "--path-mtu-initial-delay-ms" && index + 1 < argc)
            result.path_mtu_initial_probe_delay =
                std::chrono::milliseconds{std::stoll(argv[++index])};
    }
    return result;
}

} // namespace

auto main(int argc, char** argv) -> int
{
    const auto arguments = parse_options(argc, argv);
    cnetmod::net_init network;

    auto tls_result = cnetmod::ssl_context::quic_server();
    if (!tls_result)
    {
        logger::error{"TLS context creation failed: {}", tls_result.error().message()};
        return 1;
    }
    auto tls = std::move(*tls_result);
    tls.configure_alpn_server({"h3"});
    if (auto result = tls.load_cert_file(arguments.certificate); !result)
    {
        logger::error{"certificate load failed: {}", result.error().message()};
        return 1;
    }
    if (auto result = tls.load_key_file(arguments.private_key); !result)
    {
        logger::error{"private-key load failed: {}", result.error().message()};
        return 1;
    }

    const auto address = cnetmod::ip_address::from_string("0.0.0.0");
    if (!address)
    {
        logger::error{"listener address creation failed"};
        return 1;
    }

    auto context = cnetmod::make_io_context();
    auto* context_ptr = context.get();
    auto early_data_cache = std::make_shared<cnetmod::quic::early_data_replay_cache>();
    auto priority = std::make_shared<priority_fixture>();
    auto batch = std::make_shared<batch_fixture>();
    auto cancelled_push_frames = std::make_shared<std::atomic<std::size_t>>();
    cnetmod::http::v3::streaming_server_request_handler handler =
        [context_ptr, early_data_cache, priority, batch,
            cancelled_push_frames](cnetmod::http::v3::http3_request& request,
            cnetmod::http::v3::http3_response& response,
            cnetmod::http::request_body_stream& body_stream,
            cnetmod::cancel_token& token) -> cnetmod::task<std::expected<void, std::error_code>>
    {
        if (request.path.starts_with("/priority/"))
        {
            priority->arrivals.fetch_add(1U, std::memory_order_acq_rel);
            response.status = 200;
            response.headers["content-type"] = "application/octet-stream";
            auto chunk_index = std::make_shared<std::size_t>();
            response.body_source = std::make_shared<
                cnetmod::http::response_body_source>(
                [context_ptr, priority, chunk_index](
                    cnetmod::cancel_token& response_token)
                    -> cnetmod::task<std::optional<
                        cnetmod::http::request_body_chunk>>
                {
                    if (response_token.is_cancelled() ||
                        *chunk_index >= priority_fixture::body_chunks)
                        co_return std::nullopt;
                    if (*chunk_index == 0U)
                    {
                        // Do not block the request handler while waiting for
                        // the other streams. A peer may deliver several
                        // request streams in one UDP datagram, and the session
                        // must be free to dispatch every handler before this
                        // response-body barrier can open.
                        const auto arrival_deadline =
                            std::chrono::steady_clock::now() +
                            std::chrono::milliseconds{100};
                        while (priority->arrivals.load(std::memory_order_acquire) <
                                priority_fixture::request_count &&
                            std::chrono::steady_clock::now() < arrival_deadline)
                        {
                            const auto waited = co_await cnetmod::async_timer_wait(
                                *context_ptr, std::chrono::milliseconds{1},
                                response_token);
                            if (!waited)
                                co_return std::nullopt;
                        }
                        // PRIORITY_UPDATE travels on the control stream after
                        // request headers. Let already-received packets apply
                        // their urgency before any response DATA is generated.
                        const auto priority_settled = co_await cnetmod::async_timer_wait(*context_ptr,
                            std::chrono::milliseconds{20}, response_token);
                        if (!priority_settled)
                            co_return std::nullopt;

                        // `next()` is reached only after this response's
                        // HEADERS have been enqueued.  Hold all producers
                        // here so the first DATA frames become simultaneously
                        // eligible for QUIC's RFC 9218 scheduler.
                        const auto producer = priority->data_producers.fetch_add(
                                                  1U, std::memory_order_acq_rel) +
                            1U;
                        if (producer == priority_fixture::request_count)
                            priority->data_released.store(true,
                                std::memory_order_release);
                        const auto producer_deadline =
                            std::chrono::steady_clock::now() +
                            std::chrono::milliseconds{100};
                        while (!priority->data_released.load(
                                   std::memory_order_acquire) &&
                            std::chrono::steady_clock::now() < producer_deadline)
                        {
                            const auto waited = co_await cnetmod::async_timer_wait(
                                *context_ptr, std::chrono::milliseconds{1},
                                response_token);
                            if (!waited)
                                co_return std::nullopt;
                        }
                        priority->data_released.store(true,
                            std::memory_order_release);
                        // All three coroutines yield after the release. That
                        // gives each producer a chance to enqueue its first
                        // DATA frame before the packet writer chooses one.
                        const auto settled = co_await cnetmod::async_timer_wait(
                            *context_ptr, std::chrono::milliseconds{5},
                            response_token);
                        if (!settled)
                            co_return std::nullopt;
                    }
                    cnetmod::http::request_body_chunk chunk(
                        priority_fixture::chunk_size,
                        static_cast<std::byte>(*chunk_index));
                    ++*chunk_index;
                    co_return chunk;
                },
                priority_fixture::body_chunks* priority_fixture::chunk_size);
            co_return {};
        }

        if (request.path == "/slow")
        {
            const auto waited = co_await cnetmod::async_timer_wait(
                *context_ptr, std::chrono::seconds{30}, token);
            if (!waited)
                co_return std::unexpected(waited.error());
            response.status = 200;
            response.body = "slow-ok";
            co_return {};
        }

        if (request.path == "/get")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.headers["x-early-count"] = std::to_string(early_data_cache->size());
            response.body = "get-ok";
            co_return {};
        }
        if (request.path == "/push")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = "parent-ok";
            auto pushed = std::make_shared<cnetmod::http::v3::http3_response>();
            pushed->status = 200;
            pushed->headers["content-type"] = "text/plain";
            pushed->body = "asset-ok";
            cnetmod::http::v3::http3_push push;
            push.request.method = cnetmod::http::http_method::GET;
            push.request.scheme = "https";
            push.request.host = request.host;
            push.request.port = request.port;
            push.request.path = "/asset";
            push.response = std::move(pushed);
            response.pushes.push_back(std::move(push));
            co_return {};
        }
        if (request.path == "/push-stream")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = "stream-parent-ok";
            auto pushed = std::make_shared<cnetmod::http::v3::http3_response>();
            pushed->status = 200;
            pushed->headers["content-type"] = "text/plain";
            pushed->trailers["x-push-complete"] = "1";
            auto index = std::make_shared<std::size_t>();
            pushed->body_source = std::make_shared<
                cnetmod::http::response_body_source>(
                [index](cnetmod::cancel_token& token)
                    -> cnetmod::task<std::optional<cnetmod::http::request_body_chunk>>
                {
                    if (token.is_cancelled() || *index >= 3U)
                        co_return std::nullopt;
                    constexpr std::array<std::string_view, 3> chunks{
                        "pushed-", "stream-", "body"};
                    const auto part = chunks[(*index)++];
                    cnetmod::http::request_body_chunk chunk;
                    chunk.insert(chunk.end(),
                        reinterpret_cast<const std::byte*>(part.data()),
                        reinterpret_cast<const std::byte*>(part.data()) + part.size());
                    co_return chunk;
                },
                18U);
            cnetmod::http::v3::http3_push push;
            push.request.method = cnetmod::http::http_method::GET;
            push.request.scheme = "https";
            push.request.host = request.host;
            push.request.port = request.port;
            push.request.path = "/asset-stream";
            push.response = std::move(pushed);
            response.pushes.push_back(std::move(push));
            co_return {};
        }
        if (request.path == "/push-cancel")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = "cancel-parent-ok";
            auto pushed = std::make_shared<cnetmod::http::v3::http3_response>();
            pushed->status = 200;
            auto emitted = std::make_shared<bool>();
            pushed->body_source = std::make_shared<
                cnetmod::http::response_body_source>(
                [context_ptr, emitted](cnetmod::cancel_token& response_token)
                    -> cnetmod::task<std::optional<cnetmod::http::request_body_chunk>>
                {
                    if (*emitted)
                        co_return std::nullopt;
                    if (response_token.is_cancelled())
                        co_return std::nullopt;
                    // Give the peer a deterministic window to consume the
                    // PUSH_PROMISE and send CANCEL_PUSH before DATA begins.
                    // This fixture validates remote cancellation, not a
                    // latency target. Dynamic QPACK can defer the promise
                    // callback until encoder instructions arrive. Keep this
                    // synthetic producer pending beyond the observer's
                    // bounded cancellation window so it cannot unregister
                    // its token before the on-wire CANCEL_PUSH is evaluated.
                    const auto waited = co_await cnetmod::async_timer_wait(
                        *context_ptr, std::chrono::seconds{10}, response_token);
                    if (!waited || response_token.is_cancelled())
                        co_return std::nullopt;
                    *emitted = true;
                    constexpr std::string_view content{"must-not-arrive"};
                    cnetmod::http::request_body_chunk chunk;
                    chunk.insert(chunk.end(),
                        reinterpret_cast<const std::byte*>(content.data()),
                        reinterpret_cast<const std::byte*>(content.data()) + content.size());
                    co_return chunk;
                });
            cnetmod::http::v3::http3_push push;
            push.request.method = cnetmod::http::http_method::GET;
            push.request.scheme = "https";
            push.request.host = request.host;
            push.request.port = request.port;
            push.request.path = "/asset-cancel";
            push.response = std::move(pushed);
            response.pushes.push_back(std::move(push));
            co_return {};
        }
        if (request.path == "/push-cancel-count")
        {
            // Request streams and the client control stream are independent,
            // so this request can legally overtake the earlier CANCEL_PUSH on
            // the wire. Wait asynchronously for the observable cancellation
            // instead of turning cross-stream scheduling into a flaky test
            // ordering assumption.
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds{8};
            while (cancelled_push_frames->load(std::memory_order_acquire) == 0U &&
                std::chrono::steady_clock::now() < deadline &&
                !token.is_cancelled())
            {
                const auto waited = co_await cnetmod::async_timer_wait(
                    *context_ptr, std::chrono::milliseconds{5}, token);
                if (!waited)
                    break;
            }
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = std::to_string(
                cancelled_push_frames->load(std::memory_order_acquire));
            co_return {};
        }
        if (request.path == "/post")
        {
            while (auto chunk = co_await body_stream.receive())
                request.body.append(reinterpret_cast<const char*>(chunk->data()),
                    chunk->size());
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = request.body;
            co_return {};
        }
        if (request.path == "/stream-response")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.trailers["x-stream-complete"] = "1";
            auto index = std::make_shared<std::size_t>(0U);
            response.body_source = std::make_shared<
                cnetmod::http::response_body_source>(
                [index](cnetmod::cancel_token& token)
                    -> cnetmod::task<std::optional<
                        cnetmod::http::request_body_chunk>>
                {
                    if (token.is_cancelled() || *index >= 3U)
                        co_return std::nullopt;
                    constexpr std::array<std::string_view, 3> parts{
                        "stream-", "response-", "body"};
                    const auto part = parts[(*index)++];
                    cnetmod::http::request_body_chunk chunk;
                    chunk.insert(chunk.end(),
                        reinterpret_cast<const std::byte*>(part.data()),
                        reinterpret_cast<const std::byte*>(part.data()) + part.size());
                    co_return chunk;
                },
                20U);
            co_return {};
        }
        if (request.path == "/bad-length")
        {
            response.status = 200;
            response.headers["content-length"] = "99";
            response.body = "short";
            co_return {};
        }
        if (request.path == "/stream-upload")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            while (auto chunk = co_await body_stream.receive())
                response.body.append(reinterpret_cast<const char*>(chunk->data()),
                    chunk->size());
            co_return {};
        }
        if (request.path == "/request-trailers")
        {
            while (auto chunk = co_await body_stream.receive())
                request.body.append(reinterpret_cast<const char*>(chunk->data()),
                    chunk->size());
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = request.body == "trailer-body" &&
                    request.trailers["x-request-complete"] == "1"
                ? "request-trailers-ok"
                : "request-trailers-invalid";
            co_return {};
        }
        if (request.path == "/head")
        {
            response.status = 200;
            response.headers["content-length"] = "7";
            response.body = "head-ok";
            co_return {};
        }
        if (request.path == "/options")
        {
            response.status = 204;
            response.headers["allow"] = "GET, HEAD, OPTIONS, POST";
            response.body = "must-not-send";
            co_return {};
        }
        if (request.path == "/early-count")
        {
            response.status = 200;
            response.headers["content-type"] = "text/plain";
            response.body = std::to_string(early_data_cache->size());
            co_return {};
        }
        if (request.path.starts_with("/batch/"))
        {
            batch->arrivals.fetch_add(1U, std::memory_order_acq_rel);
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds{1};
            while (batch->arrivals.load(std::memory_order_acquire) <
                    batch_fixture::request_count &&
                std::chrono::steady_clock::now() < deadline)
            {
                const auto waited = co_await cnetmod::async_timer_wait(
                    *context_ptr, std::chrono::milliseconds{1}, token);
                if (!waited)
                    co_return std::unexpected(waited.error());
            }
            if (batch->arrivals.load(std::memory_order_acquire) <
                batch_fixture::request_count)
            {
                response.status = 503;
                response.body = "batch-not-concurrent";
                co_return {};
            }
            response.status = 200;
            response.body = request.path;
            co_return {};
        }

        response.status = 404;
        response.body = "not-found";
        co_return {};
    };

    auto server = cnetmod::http::v3::make_http3_server(*context, tls,
        cnetmod::endpoint{*address, arguments.port}, std::move(handler));
    if (auto configured = server->set_push_cancellation_observer(
            [cancelled_push_frames](std::uint64_t)
            {
                cancelled_push_frames->fetch_add(1U, std::memory_order_release);
            });
        !configured)
    {
        logger::error{"Push cancellation observer configuration failed: {}",
            configured.error().message()};
        return 1;
    }
    if (arguments.enable_dynamic_qpack)
    {
        if (auto configured = server->set_qpack_settings(64U * 1024U, 100U);
            !configured)
        {
            logger::error{"QPACK dynamic-table configuration failed: {}",
                configured.error().message()};
            return 1;
        }
    }
    if (arguments.enable_multipath)
    {
        if (auto configured = server->set_multipath_initial_max_path_id(1U);
            !configured)
        {
            logger::error{"Multipath QUIC configuration failed: {}",
                configured.error().message()};
            return 1;
        }
    }
    if (arguments.enable_path_mtu_discovery)
    {
        if (auto configured = server->set_path_mtu_discovery(1452,
                std::chrono::seconds{1}, arguments.path_mtu_initial_probe_delay);
            !configured)
        {
            logger::error{"Path MTU discovery configuration failed: {}",
                configured.error().message()};
            return 1;
        }
    }
    if (arguments.enable_early_data)
    {
        auto callbacks = std::make_shared<
            cnetmod::quic::server_early_data_ticket_callbacks>();
        callbacks->max_overhead = ticket_magic.size();
        callbacks->replay_cache = early_data_cache;
        callbacks->seal = [](std::span<const std::byte> plaintext)
            -> std::expected<std::vector<std::byte>, std::error_code>
        {
            std::vector<std::byte> ciphertext;
            ciphertext.reserve(plaintext.size() + ticket_magic.size());
            for (const auto byte : plaintext)
                ciphertext.push_back(byte ^ std::byte{0xa5});
            ciphertext.insert(ciphertext.end(), ticket_magic.begin(), ticket_magic.end());
            return ciphertext;
        };
        callbacks->open = [](std::span<const std::byte> ciphertext)
            -> std::expected<cnetmod::quic::server_early_data_ticket,
                std::error_code>
        {
            if (ciphertext.size() < ticket_magic.size() ||
                !std::ranges::equal(ciphertext.last(ticket_magic.size()), ticket_magic))
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            const auto plaintext_size = ciphertext.size() - ticket_magic.size();
            std::vector<std::byte> plaintext;
            plaintext.reserve(plaintext_size);
            for (const auto byte : ciphertext.first(plaintext_size))
                plaintext.push_back(byte ^ std::byte{0xa5});
            return cnetmod::quic::server_early_data_ticket{
                .plaintext = std::move(plaintext),
                .identity = std::vector<std::byte>{ciphertext.begin(),
                    ciphertext.begin() + static_cast<std::ptrdiff_t>(plaintext_size)},
                .early_data_expires_at = std::chrono::steady_clock::now() +
                    std::chrono::minutes{5}};
        };
        // The context is deliberately stable across connections and binds this
        // fixture's QUIC/H3 settings. Production applications should derive it
        // from their complete transport-parameter and SETTINGS serialization.
        constexpr std::array early_context{
            std::byte{'c'}, std::byte{'n'}, std::byte{'e'}, std::byte{'t'},
            std::byte{'m'}, std::byte{'o'}, std::byte{'d'}, std::byte{'-'},
            std::byte{'h'}, std::byte{'3'}, std::byte{'-'}, std::byte{'e'},
            std::byte{'2'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}};
        if (auto configured = server->set_early_data_tickets(std::move(callbacks),
                {early_context.begin(), early_context.end()});
            !configured)
        {
            logger::error{"early-data ticket configuration failed: {}",
                configured.error().message()};
            return 1;
        }
    }
    if (auto result = server->start(); !result)
    {
        logger::error{"server start failed: {}", result.error().message()};
        return 1;
    }
    logger::info{"HTTP/3 E2E server listening on UDP {}", arguments.port};
    context->run();
    return 0;
}
