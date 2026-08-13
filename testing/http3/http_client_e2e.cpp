// End-to-end regression for the public cnetmod::http::client HTTP/3 path.

import std;
import cnetmod.core;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.task_group;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.frame;
import cnetmod.protocol.http.v3.session;

namespace {

struct test_state
{
    bool ok = true;
    std::string failure;

    void fail(std::string message)
    {
        if (ok)
        {
            ok = false;
            failure = std::move(message);
        }
    }
};

auto require_response(test_state& state,
    const std::expected<cnetmod::http::response, std::error_code>& result,
    int expected_status, std::string_view expected_body, std::string_view label) -> bool
{
    if (!result)
    {
        state.fail(std::string(label) + " failed: " + result.error().message());
        return false;
    }
    if (result->status_code() != expected_status)
    {
        state.fail(std::string(label) + " returned status " +
            std::to_string(result->status_code()));
        return false;
    }
    if (result->body() != expected_body)
    {
        state.fail(std::string(label) + " returned unexpected body: " +
            std::string(result->body()));
        return false;
    }
    return true;
}

auto run_http_semantics(cnetmod::io_context& context, std::uint16_t port,
    test_state& state) -> cnetmod::task<void>
{
    cnetmod::http::client_options options;
    // The race has its own regression coverage; keep this long-lived client
    // pinned to H3 so this fixture isolates HTTP/3 request semantics.
    options.version_pref = cnetmod::http::http_version_preference::http3_only;
    options.verify_peer = false;
    options.follow_redirects = false;
    options.request_timeout = std::chrono::seconds{10};
    cnetmod::http::client client{context, options};
    const auto url = [port](std::string_view path)
    {
        return std::string{"https://127.0.0.1:"} + std::to_string(port) +
            std::string(path);
    };

    auto get = co_await client.get(url("/get"));
    require_response(state, get, 200, "get-ok", "GET");

    // The unified client does not opt into server push by default. A server
    // may still prepare pushes, but its primary response must remain valid
    // and no unsolicited resource is accepted on the connection.
    auto push_disabled = co_await client.get(url("/push"));
    require_response(state, push_disabled, 200, "parent-ok",
        "server push disabled by default");

    auto post = co_await client.post(url("/post"), "post-body");
    require_response(state, post, 200, "post-body", "POST");

    auto upload_index = std::make_shared<std::size_t>(0);
    auto upload_source = std::make_shared<cnetmod::http::request_body_source>(
        [upload_index](cnetmod::cancel_token& token)
            -> cnetmod::task<std::optional<cnetmod::http::request_body_chunk>>
        {
            if (token.is_cancelled() || *upload_index >= 3)
                co_return std::nullopt;
            const std::array<std::string_view, 3> parts{
                "stream-", "upload-", "body"};
            const auto part = parts[(*upload_index)++];
            cnetmod::http::request_body_chunk chunk;
            chunk.insert(chunk.end(),
                reinterpret_cast<const std::byte*>(part.data()),
                reinterpret_cast<const std::byte*>(part.data()) + part.size());
            co_return chunk;
        });
    cnetmod::http::request streaming_request{
        cnetmod::http::http_method::POST, url("/stream-upload")};
    streaming_request.set_header("Content-Type", "text/plain");
    streaming_request.set_body_stream(upload_source);
    auto streamed = co_await client.send(streaming_request);
    require_response(state, streamed, 200, "stream-upload-body", "streaming POST");

    auto streamed_response = co_await client.get(url("/stream-response"));
    require_response(state, streamed_response, 200, "stream-response-body",
        "streaming response");
    // Exercise the explicit low-level HTTP/3 response streaming API.  The
    // unified client intentionally keeps its established complete-body
    // response contract; callers that need bounded incremental consumption
    // opt into `http3_client::send_request_streaming`.
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        state.fail("could not create HTTP/3 streaming TLS context");
    }
    else
    {
        auto streaming_tls = std::move(*tls_result);
        cnetmod::http::v3::http3_client_options streaming_options;
        streaming_options.verify_certificate = false;
        streaming_options.tls_sni_host = "127.0.0.1";
        auto pushed_path = std::make_shared<std::string>();
        auto pushed_body = std::make_shared<std::string>();
        auto pushed_stream_path = std::make_shared<std::string>();
        auto pushed_stream_body = std::make_shared<std::string>();
        auto push_count = std::make_shared<std::atomic<std::size_t>>();
        auto cancelled_push_promises = std::make_shared<std::atomic<std::size_t>>();
        // The fixture exercises three accepted Push IDs (0, 1, and the
        // rejected 2), so advertise the inclusive RFC 9114 limit accordingly.
        streaming_options.max_push_id = 2U;
        streaming_options.on_server_push_promise = [cancelled_push_promises](
                                                       cnetmod::http::v3::server_push_promise promise)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            if (promise.request.path == "/asset-cancel")
            {
                cancelled_push_promises->fetch_add(1U, std::memory_order_release);
                // A rejected promise must send CANCEL_PUSH before the delayed
                // producer is allowed to emit its first DATA frame.
                co_return std::unexpected(std::make_error_code(
                    std::errc::operation_canceled));
            }
            co_return {};
        };
        streaming_options.on_server_push = [pushed_path, pushed_body,
                                               pushed_stream_path, pushed_stream_body,
                                               push_count](
                                               cnetmod::http::v3::http3_request promise,
                                               cnetmod::http::v3::http3_response pushed)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            if (promise.path == "/asset")
            {
                *pushed_path = std::move(promise.path);
                *pushed_body = std::move(pushed.body);
            }
            else if (promise.path == "/asset-stream")
            {
                *pushed_stream_path = std::move(promise.path);
                *pushed_stream_body = std::move(pushed.body);
            }
            push_count->fetch_add(1U, std::memory_order_release);
            co_return {};
        };
        cnetmod::http::v3::http3_client streaming_client{
            context, streaming_tls, std::move(streaming_options)};
        auto connected = co_await streaming_client.connect("127.0.0.1", port);
        if (!connected)
        {
            state.fail("low-level HTTP/3 streaming client connect failed: " +
                connected.error().message());
        }
        else
        {
            cnetmod::http::v3::http3_request streaming_request;
            streaming_request.host = "127.0.0.1";
            streaming_request.port = port;
            streaming_request.path = "/stream-response";
            std::string streamed_chunks;
            std::size_t streamed_chunk_count = 0U;
            auto streaming_result = co_await streaming_client.send_request_streaming(
                streaming_request,
                [&streamed_chunks, &streamed_chunk_count](cnetmod::http::v3::http3_response& response,
                    cnetmod::http::request_body_stream& body,
                    cnetmod::cancel_token& token)
                    -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    if (response.status != 200)
                        co_return std::unexpected(std::make_error_code(
                            std::errc::protocol_error));
                    while (!token.is_cancelled())
                    {
                        auto chunk = co_await body.receive();
                        if (!chunk)
                            break;
                        ++streamed_chunk_count;
                        streamed_chunks.append(reinterpret_cast<const char*>(chunk->data()),
                            chunk->size());
                    }
                    co_return {};
                });
            if (!streaming_result || streaming_result->body != "" ||
                streamed_chunks != "stream-response-body" ||
                streamed_chunk_count < 2U ||
                streaming_result->trailers["x-stream-complete"] != "1")
                state.fail("HTTP/3 response streaming API returned invalid data");

            cnetmod::http::v3::http3_request trailer_request;
            trailer_request.method = cnetmod::http::http_method::POST;
            trailer_request.host = "127.0.0.1";
            trailer_request.port = port;
            trailer_request.path = "/request-trailers";
            trailer_request.body = "trailer-body";
            trailer_request.trailers["x-request-complete"] = "1";
            auto trailers_result = co_await streaming_client.send_request(trailer_request);
            if (!trailers_result || trailers_result->status != 200 ||
                trailers_result->body != "request-trailers-ok")
                state.fail("HTTP/3 request trailers were not delivered");

            cnetmod::http::v3::http3_request push_request;
            push_request.host = "127.0.0.1";
            push_request.port = port;
            push_request.path = "/push";
            auto parent = co_await streaming_client.send_request(push_request);
            if (!parent || parent->status != 200 || parent->body != "parent-ok")
            {
                state.fail("HTTP/3 server-push parent response was invalid");
            }
            else
            {
                const auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds{2};
                while (push_count->load(std::memory_order_acquire) != 1U &&
                    std::chrono::steady_clock::now() < deadline)
                    (void)co_await cnetmod::async_timer_wait(context,
                        std::chrono::milliseconds{1});
                if (push_count->load(std::memory_order_acquire) != 1U ||
                    *pushed_path != "/asset" || *pushed_body != "asset-ok")
                    state.fail("HTTP/3 server push was not delivered correctly");
            }

            cnetmod::http::v3::http3_request stream_push_request;
            stream_push_request.host = "127.0.0.1";
            stream_push_request.port = port;
            stream_push_request.path = "/push-stream";
            auto stream_parent = co_await streaming_client.send_request(
                stream_push_request);
            if (!stream_parent || stream_parent->status != 200 ||
                stream_parent->body != "stream-parent-ok")
            {
                state.fail("HTTP/3 streaming server-push parent response was invalid: " +
                    (stream_parent ? std::to_string(stream_parent->status) +
                                " / " + stream_parent->body
                                   : stream_parent.error().message()));
            }
            else
            {
                const auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds{2};
                while (push_count->load(std::memory_order_acquire) != 2U &&
                    std::chrono::steady_clock::now() < deadline)
                    (void)co_await cnetmod::async_timer_wait(context,
                        std::chrono::milliseconds{1});
                if (push_count->load(std::memory_order_acquire) != 2U ||
                    *pushed_stream_path != "/asset-stream" ||
                    *pushed_stream_body != "pushed-stream-body")
                    state.fail("HTTP/3 streaming server push was not delivered correctly");
            }

            cnetmod::http::v3::http3_request cancelled_push_request;
            cancelled_push_request.host = "127.0.0.1";
            cancelled_push_request.port = port;
            cancelled_push_request.path = "/push-cancel";
            std::string cancelled_parent_body;
            auto cancelled_parent = co_await streaming_client.send_request_streaming(
                cancelled_push_request,
                [&cancelled_parent_body](cnetmod::http::v3::http3_response& response,
                    cnetmod::http::request_body_stream& body,
                    cnetmod::cancel_token& request_token)
                    -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    if (response.status != 200)
                        co_return std::unexpected(std::make_error_code(
                            std::errc::protocol_error));
                    while (!request_token.is_cancelled())
                    {
                        auto chunk = co_await body.receive();
                        if (!chunk)
                            break;
                        cancelled_parent_body.append(
                            reinterpret_cast<const char*>(chunk->data()),
                            chunk->size());
                    }
                    co_return {};
                });
            if (!cancelled_parent || cancelled_parent->status != 200 ||
                cancelled_parent_body != "cancel-parent-ok")
            {
                state.fail("HTTP/3 cancelled-push parent response was invalid: " +
                    (cancelled_parent ? std::to_string(cancelled_parent->status) +
                                " / " + cancelled_parent_body
                                      : cancelled_parent.error().message()));
            }
            else
            {
                const auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds{2};
                while (cancelled_push_promises->load(std::memory_order_acquire) != 1U &&
                    std::chrono::steady_clock::now() < deadline)
                    (void)co_await cnetmod::async_timer_wait(context,
                        std::chrono::milliseconds{1});
                // The completion callback count must stay at two: the rejected
                // Push is cancelled locally and remotely, never delivered as
                // an empty/partial application response.
                if (cancelled_push_promises->load(std::memory_order_acquire) != 1U ||
                    push_count->load(std::memory_order_acquire) != 2U)
                    state.fail("HTTP/3 PUSH_PROMISE rejection did not cancel the Push stream");

                // The server fixture increments this only when the body
                // producer's cancel token is signalled by the received
                // CANCEL_PUSH control frame. This rules out a client-only
                // suppression implementation.
                (void)co_await cnetmod::async_timer_wait(context,
                    std::chrono::milliseconds{25});
                cnetmod::http::v3::http3_request cancellation_count_request;
                cancellation_count_request.host = "127.0.0.1";
                cancellation_count_request.port = port;
                cancellation_count_request.path = "/push-cancel-count";
                auto cancellation_count = co_await streaming_client.send_request(
                    cancellation_count_request);
                if (!cancellation_count || cancellation_count->status != 200 ||
                    cancellation_count->body != "1")
                    state.fail("HTTP/3 server did not observe CANCEL_PUSH");
            }

            // A public close() may race an application request that is already
            // suspended in response I/O.  The request retains its session and
            // transport until QUIC wakes it, then must finish promptly rather
            // than dereferencing the client's now-cleared owner.
            struct close_request_state
            {
                std::atomic<bool> done{};
                std::optional<std::expected<cnetmod::http::v3::http3_response,
                    std::error_code>>
                    result;
            };

            auto close_request = std::make_shared<close_request_state>();
            cnetmod::http::v3::http3_request slow_request;
            slow_request.host = "127.0.0.1";
            slow_request.port = port;
            slow_request.path = "/slow";
            cnetmod::spawn(context, [&streaming_client, slow_request, close_request]() -> cnetmod::task<void>
                {
                    close_request->result = co_await streaming_client.send_request(slow_request);
                    close_request->done.store(true, std::memory_order_release);
                }());
            (void)co_await cnetmod::async_timer_wait(context,
                std::chrono::milliseconds{25});
            co_await streaming_client.close();
            const auto close_request_deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds{1};
            while (!close_request->done.load(std::memory_order_acquire) &&
                std::chrono::steady_clock::now() < close_request_deadline)
                (void)co_await cnetmod::async_timer_wait(context,
                    std::chrono::milliseconds{5});
            if (!close_request->done.load(std::memory_order_acquire) ||
                !close_request->result || *close_request->result)
                state.fail("HTTP/3 close did not cancel an in-flight request safely");
        }
    }

    cnetmod::http::request head_request{cnetmod::http::http_method::HEAD, url("/head")};
    auto head = co_await client.send(head_request);
    if (!head || head->status_code() != 200 || !head->body().empty())
        state.fail("HEAD failed");

    cnetmod::http::request options_request{
        cnetmod::http::http_method::OPTIONS, url("/options")};
    auto options_response = co_await client.send(options_request);
    if (!options_response || options_response->status_code() != 204 ||
        !options_response->body().empty())
        state.fail("OPTIONS failed");

    auto bad_length = co_await client.get(url("/bad-length"));
    if (bad_length)
        state.fail("mismatched Content-Length unexpectedly succeeded");

    std::array<cnetmod::http::request, 3> batch{
        cnetmod::http::request{cnetmod::http::http_method::GET, url("/batch/0")},
        cnetmod::http::request{cnetmod::http::http_method::GET, url("/batch/1")},
        cnetmod::http::request{cnetmod::http::http_method::GET, url("/batch/2")},
    };
    const auto batch_started = std::chrono::steady_clock::now();
    auto batch_results = co_await client.send_batch(batch);
    const auto batch_elapsed = std::chrono::steady_clock::now() - batch_started;
    if (batch_elapsed > std::chrono::milliseconds{260})
        state.fail("HTTP/3 batch responses were serialized");
    if (batch_results.size() != batch.size())
    {
        state.fail("HTTP/3 batch returned the wrong result count");
    }
    else
    {
        for (std::size_t index = 0; index < batch_results.size(); ++index)
        {
            const auto expected = std::string{"/batch/"} + std::to_string(index);
            require_response(state, batch_results[index], 200, expected, "batch");
        }
    }

    cnetmod::cancel_token cancellation;
    auto cancel_timer = [&]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_timer_wait(context,
            std::chrono::milliseconds{100});
        cancellation.cancel();
    };
    cnetmod::spawn(context, cancel_timer());
    cnetmod::http::request slow_request{
        cnetmod::http::http_method::GET, url("/slow")};
    auto cancelled = co_await client.send(slow_request, cancellation);
    if (cancelled || !cancellation.is_cancelled())
        state.fail("cancelled HTTP/3 request unexpectedly completed");

    co_await client.close_async();
    co_return;
}

// This must exercise the on-wire PRIORITY_UPDATE path rather than merely the
// priority_service_budget unit test.  The fixture holds all three requests
// until their priority updates have reached the server, then emits enough DATA
// frames to observe both foreground preference and the bounded background
// service guarantee.
auto run_priority_semantics(cnetmod::io_context& context, std::uint16_t port,
    test_state& state) -> cnetmod::task<void>
{
    constexpr std::size_t request_count = 3U;
    constexpr std::size_t body_chunks = 24U;

    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        state.fail("could not create HTTP/3 priority TLS context");
        co_return;
    }
    auto tls = std::move(*tls_result);
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = "127.0.0.1";
    cnetmod::http::v3::http3_client client{context, tls, std::move(options)};
    auto connected = co_await client.connect("127.0.0.1", port);
    if (!connected)
    {
        state.fail("HTTP/3 priority client connect failed: " +
            connected.error().message());
        co_return;
    }

    std::array<std::atomic<std::size_t>, request_count> first_chunk{};
    std::atomic<std::size_t> sequence{};
    std::array<std::size_t, request_count> received_chunks{};
    cnetmod::task_group group{context};

    for (std::size_t index{}; index < request_count; ++index)
    {
        // Open the background stream first. Without the received
        // PRIORITY_UPDATE, stream-id/FIFO order would service it first and
        // this regression would fail instead of accidentally passing.
        const bool foreground = index != 0U;
        const auto urgency = foreground ? std::uint8_t{0U} : std::uint8_t{7U};
        const bool started = group.run(
            [&client, &first_chunk, &sequence, &received_chunks, index, urgency,
                port](cnetmod::cancel_token& token)
                -> cnetmod::task<std::expected<void, std::error_code>>
            {
                cnetmod::http::v3::http3_request request;
                request.host = "127.0.0.1";
                request.port = port;
                request.path = "/priority/" + std::to_string(index);
                request.priority = cnetmod::http::v3::http_priority{
                    .urgency = urgency,
                    .incremental = true};
                auto result = co_await client.send_request_streaming(request, [&first_chunk, &sequence, &received_chunks, index](cnetmod::http::v3::http3_response& response, cnetmod::http::request_body_stream& body, cnetmod::cancel_token& request_token) -> cnetmod::task<std::expected<void, std::error_code>>
                    {
                        if (response.status != 200)
                            co_return std::unexpected(std::make_error_code(
                                std::errc::protocol_error));
                        while (!request_token.is_cancelled())
                        {
                            auto chunk = co_await body.receive();
                            if (!chunk)
                                break;
                            std::size_t expected{};
                            const auto ordinal = sequence.fetch_add(1U,
                                                     std::memory_order_acq_rel) +
                                1U;
                            (void)first_chunk[index].compare_exchange_strong(
                                expected, ordinal, std::memory_order_acq_rel,
                                std::memory_order_acquire);
                            ++received_chunks[index];
                        }
                        co_return {};
                    },
                    token);
                if (!result)
                    co_return std::unexpected(result.error());
                co_return std::expected<void, std::error_code>{};
            });
        if (!started)
        {
            state.fail("HTTP/3 priority task group rejected a request");
            co_await client.close();
            co_return;
        }
    }

    auto joined = co_await group.join();
    co_await client.close();
    if (!joined)
    {
        state.fail("HTTP/3 priority request failed: " + joined.error().message());
        co_return;
    }
    for (std::size_t index{}; index < request_count; ++index)
    {
        if (received_chunks[index] != body_chunks ||
            first_chunk[index].load(std::memory_order_acquire) == 0U)
        {
            state.fail("HTTP/3 priority fixture lost a response DATA frame");
            co_return;
        }
    }
    const auto first_background = first_chunk[0].load(std::memory_order_acquire);
    const auto first_foreground = std::min(
        first_chunk[1].load(std::memory_order_acquire),
        first_chunk[2].load(std::memory_order_acquire));
    // The client observes decrypted stream delivery, not the server's packet
    // builder order: UDP packet aggregation, loss recovery and independent
    // stream consumer wakeups can legitimately deliver a background chunk
    // first. Do not infer a server-side RFC 9218 service bound from that
    // transport-independent receive race. `test_quic_frame` asserts the exact
    // 16-frame packet-builder budget; this E2E gate proves that on-wire
    // PRIORITY_UPDATE remains compatible with concurrent response streaming,
    // including full liveness of the lower-priority response.
    logger::info{"HTTP/3 priority E2E passed: first foreground={}, first background={}",
        first_foreground, first_background};
}

auto run_multipath_smoke(cnetmod::io_context& context, std::uint16_t port,
    test_state& state, bool enable_path_mtu_discovery = false,
    bool expect_path_mtu_growth = true,
    const std::filesystem::path& pmtu_blackhole_arm_marker = {}) -> cnetmod::task<void>
{
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        state.fail("could not create Multipath QUIC TLS context");
        co_return;
    }
    auto address = cnetmod::ip_address::from_string("127.0.0.1");
    if (!address)
    {
        state.fail("could not create Multipath QUIC loopback endpoint");
        co_return;
    }
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = "127.0.0.1";
    options.multipath_initial_max_path_id = 1U;
    options.enable_path_mtu_discovery = enable_path_mtu_discovery;
    if (!expect_path_mtu_growth)
        options.path_mtu_initial_probe_delay = std::chrono::milliseconds{500};
    cnetmod::http::v3::http3_client client{context, *tls_result, std::move(options)};
    const auto connected = co_await client.connect("127.0.0.1", port);
    if (!connected)
    {
        state.fail("Multipath QUIC connect failed: " + connected.error().message());
        co_return;
    }
    // PATH_NEW_CONNECTION_ID is delivered in the post-handshake application
    // flight. Retry only while that authenticated control frame is in flight;
    // a different error is a real multipath negotiation failure.
    std::expected<void, std::error_code> probed =
        std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    for (std::size_t attempt{}; attempt < 20U; ++attempt)
    {
        // Bind an independent ephemeral local UDP socket. This proves Path
        // ID 1 has a different four-tuple and exercises its owned receiver
        // lifecycle rather than merely using a second logical Path ID over
        // the original socket.
        probed = co_await client.async_probe_path(1U,
            cnetmod::endpoint{*address, port}, cnetmod::endpoint{*address, 0U});
        if (probed)
            break;
        if (probed.error() != std::make_error_code(std::errc::no_such_file_or_directory) &&
            probed.error() != std::make_error_code(std::errc::operation_in_progress))
            break;
        (void)co_await cnetmod::async_timer_wait(context,
            std::chrono::milliseconds{20});
    }
    if (!probed)
    {
        state.fail("Multipath QUIC path probe failed: " + probed.error().message());
        co_await client.close();
        co_return;
    }
    if (!client.path_is_validated(1U))
    {
        state.fail("Multipath QUIC Path ID 1 was not validated by PATH_RESPONSE");
        co_await client.close();
        co_return;
    }
    const auto primary_local = client.local_path_endpoint(0U);
    const auto multipath_local = client.local_path_endpoint(1U);
    if (!primary_local || !multipath_local ||
        primary_local->to_string() == multipath_local->to_string())
    {
        state.fail("Multipath QUIC Path ID 1 did not retain a distinct local UDP endpoint");
        co_await client.close();
        co_return;
    }
    if (!pmtu_blackhole_arm_marker.empty())
    {
        // The blackhole fixture is armed only after PATH_RESPONSE validates
        // Path 1. The deliberately configured first-probe delay gives the
        // proxy time to observe this marker before PLPMTUD emits its probe.
        std::ofstream marker{pmtu_blackhole_arm_marker, std::ios::binary};
        if (!marker)
        {
            state.fail("could not arm Multipath QUIC PMTU blackhole fixture");
            co_await client.close();
            co_return;
        }
        marker << "Path ID 1 validated\n";
    }
    if (enable_path_mtu_discovery)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto mtu = client.discovered_path_mtu(1U);
            if (expect_path_mtu_growth && mtu && *mtu > cnetmod::quic::min_initial_pkt_size)
                break;
            (void)co_await cnetmod::async_timer_wait(context,
                std::chrono::milliseconds{10});
        }
        const auto mtu = client.discovered_path_mtu(1U);
        if (expect_path_mtu_growth &&
            (!mtu || *mtu <= cnetmod::quic::min_initial_pkt_size))
        {
            state.fail("Multipath QUIC Path ID 1 PMTU did not advance after an acknowledged probe");
            co_await client.close();
            co_return;
        }
        if (!expect_path_mtu_growth &&
            (!mtu || *mtu != cnetmod::quic::min_initial_pkt_size))
        {
            state.fail("Multipath QUIC black-holed PMTU probe incorrectly raised Path ID 1 MTU");
            co_await client.close();
            co_return;
        }
    }

    cnetmod::http::v3::http3_request request;
    request.host = "127.0.0.1";
    request.port = port;
    request.path = "/get";
    if (const auto backup = client.set_path_backup(0U, true); !backup)
    {
        state.fail("Multipath QUIC could not make Path ID 0 a backup path");
        co_await client.close();
        co_return;
    }
    // With Path 0 explicitly backup, this request must be emitted through
    // Path 1's separately bound socket. Its successful response proves both
    // per-path sending and the owned secondary receive driver.
    const auto result = co_await client.send_request(request);
    if (!result || result->status != 200 || result->body != "get-ok")
    {
        state.fail("Multipath QUIC request after PATH_RESPONSE failed");
        co_await client.close();
        co_return;
    }
    const auto abandoned = co_await client.async_abandon_path(1U);
    if (!abandoned)
    {
        state.fail("Multipath QUIC PATH_ABANDON failed: " +
            abandoned.error().message());
        co_await client.close();
        co_return;
    }
    (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{50});
    const auto fallback = co_await client.send_request(request);
    if (!fallback || fallback->status != 200 || fallback->body != "get-ok")
        state.fail("Multipath QUIC request after PATH_ABANDON did not fall back");
    co_await client.close();
}

auto run_multipath_timeout_smoke(cnetmod::io_context& context, std::uint16_t port,
    test_state& state) -> cnetmod::task<void>
{
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        state.fail("could not create Multipath QUIC TLS context");
        co_return;
    }
    auto address = cnetmod::ip_address::from_string("127.0.0.1");
    if (!address)
    {
        state.fail("could not create Multipath QUIC loopback endpoint");
        co_return;
    }
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = "127.0.0.1";
    options.multipath_initial_max_path_id = 1U;
    cnetmod::http::v3::http3_client client{context, *tls_result, std::move(options)};
    const auto connected = co_await client.connect("127.0.0.1", port);
    if (!connected)
    {
        state.fail("Multipath QUIC timeout fixture connect failed: " + connected.error().message());
        co_return;
    }

    // Deliberately send a path validation challenge to an unbound loopback
    // port. Successful UDP submission is insufficient: the public API must
    // wait for PATH_RESPONSE and report the bounded validation timeout.
    const auto blackhole_port = port == std::numeric_limits<std::uint16_t>::max()
        ? static_cast<std::uint16_t>(port - 1U)
        : static_cast<std::uint16_t>(port + 1U);
    const auto probe = co_await client.async_probe_path(1U,
        cnetmod::endpoint{*address, blackhole_port});
    if (probe || probe.error() != std::make_error_code(std::errc::timed_out))
    {
        state.fail("Multipath QUIC probe without PATH_RESPONSE did not time out");
        co_await client.close();
        co_return;
    }
    if (client.path_is_validated(1U))
    {
        state.fail("Multipath QUIC timed-out Path ID 1 became validated");
        co_await client.close();
        co_return;
    }

    cnetmod::http::v3::http3_request request;
    request.host = "127.0.0.1";
    request.port = port;
    request.path = "/get";
    const auto fallback = co_await client.send_request(request);
    if (!fallback || fallback->status != 200 || fallback->body != "get-ok")
        state.fail("Multipath QUIC probe timeout broke Path ID 0 fallback");
    co_await client.close();
}

auto run_multipath_close_smoke(cnetmod::io_context& context, std::uint16_t port,
    test_state& state) -> cnetmod::task<void>
{
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        state.fail("could not create Multipath QUIC TLS context for close test");
        co_return;
    }
    auto address = cnetmod::ip_address::from_string("127.0.0.1");
    if (!address)
    {
        state.fail("could not create Multipath QUIC loopback endpoint for close test");
        co_return;
    }
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = "127.0.0.1";
    options.multipath_initial_max_path_id = 1U;
    cnetmod::http::v3::http3_client client{context, *tls_result, std::move(options)};
    const auto connected = co_await client.connect("127.0.0.1", port);
    if (!connected)
    {
        state.fail("Multipath QUIC close fixture connect failed: " + connected.error().message());
        co_return;
    }

    // PATH_NEW_CONNECTION_ID is post-handshake traffic. Let it arrive before
    // beginning the deliberately unanswered probe; this keeps the regression
    // about cancellation/receiver ownership rather than CID negotiation.
    (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{250});
    const auto blackhole_port = port == std::numeric_limits<std::uint16_t>::max()
        ? static_cast<std::uint16_t>(port - 1U)
        : static_cast<std::uint16_t>(port + 1U);

    struct close_probe_state
    {
        std::atomic<bool> done{};
        std::optional<std::expected<void, std::error_code>> result;
    };

    auto probe_state = std::make_shared<close_probe_state>();
    auto probe = [&client, address = *address, blackhole_port, probe_state]()
        -> cnetmod::task<void>
    {
        probe_state->result = co_await client.async_probe_path(1U,
            cnetmod::endpoint{address, blackhole_port},
            cnetmod::endpoint{address, 0U});
        probe_state->done.store(true, std::memory_order_release);
    };
    cnetmod::spawn(context, probe());
    (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{50});

    auto peer_close_done = std::make_shared<std::atomic<bool>>();
    auto peer_close = [&client, &context, peer_close_done]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{5});
        co_await client.close();
        peer_close_done->store(true, std::memory_order_release);
    };
    cnetmod::spawn(context, peer_close());
    const auto close_started = std::chrono::steady_clock::now();
    co_await client.close();
    if (std::chrono::steady_clock::now() - close_started > std::chrono::seconds{2})
    {
        state.fail("Multipath QUIC close did not promptly join the local path receiver");
        co_return;
    }
    const auto second_close_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{1};
    while (!peer_close_done->load(std::memory_order_acquire) &&
        std::chrono::steady_clock::now() < second_close_deadline)
    {
        (void)co_await cnetmod::async_timer_wait(context,
            std::chrono::milliseconds{10});
    }
    if (!peer_close_done->load(std::memory_order_acquire))
    {
        state.fail("Multipath QUIC concurrent close did not complete");
        co_return;
    }
    const auto completion_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{1};
    while (!probe_state->done.load(std::memory_order_acquire) &&
        std::chrono::steady_clock::now() < completion_deadline)
    {
        (void)co_await cnetmod::async_timer_wait(context,
            std::chrono::milliseconds{10});
    }
    if (!probe_state->done.load(std::memory_order_acquire) || !probe_state->result)
    {
        state.fail("Multipath QUIC close left a local path probe pending");
        co_return;
    }
    if (*probe_state->result ||
        (probe_state->result->error() != std::make_error_code(std::errc::operation_canceled) &&
            probe_state->result->error() != std::make_error_code(std::errc::not_connected)))
    {
        state.fail("Multipath QUIC close did not cancel the local path probe");
    }
}

auto run_connect_close_smoke(cnetmod::io_context& context, std::uint16_t port,
    test_state& state) -> cnetmod::task<void>
{
    auto tls_result = cnetmod::ssl_context::quic_client();
    if (!tls_result)
    {
        state.fail("could not create HTTP/3 TLS context for connect-close test");
        co_return;
    }
    cnetmod::http::v3::http3_client_options options;
    options.verify_certificate = false;
    options.tls_sni_host = "127.0.0.1";
    options.connect_timeout = std::chrono::seconds{5};
    cnetmod::http::v3::http3_client client{context, *tls_result, std::move(options)};

    struct connect_state
    {
        std::atomic<bool> done{};
        std::optional<std::expected<void, std::error_code>> result;
    };

    const auto blackhole_port = port == std::numeric_limits<std::uint16_t>::max()
        ? static_cast<std::uint16_t>(port - 1U)
        : static_cast<std::uint16_t>(port + 1U);
    auto attempt = std::make_shared<connect_state>();
    cnetmod::spawn(context, [&client, blackhole_port, attempt]() -> cnetmod::task<void>
        {
            attempt->result = co_await client.connect("127.0.0.1", blackhole_port);
            attempt->done.store(true, std::memory_order_release);
        }());
    (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{25});
    co_await client.close();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (!attempt->done.load(std::memory_order_acquire) &&
        std::chrono::steady_clock::now() < deadline)
        (void)co_await cnetmod::async_timer_wait(context, std::chrono::milliseconds{5});
    if (!attempt->done.load(std::memory_order_acquire) || !attempt->result ||
        *attempt->result || client.is_connected())
        state.fail("HTTP/3 close did not linearize an in-progress connect");
}

auto run_suite(cnetmod::io_context& context, std::uint16_t port, test_state& state,
    bool multipath, bool enable_path_mtu_discovery, bool multipath_timeout,
    bool multipath_pmtu_blackhole, bool multipath_close, bool connect_close,
    const std::filesystem::path& pmtu_blackhole_arm_marker)
    -> cnetmod::task<void>
{
    if (multipath_timeout)
    {
        co_await run_multipath_timeout_smoke(context, port, state);
        context.stop();
        co_return;
    }
    if (multipath_close)
    {
        co_await run_multipath_close_smoke(context, port, state);
        context.stop();
        co_return;
    }
    if (connect_close)
    {
        co_await run_connect_close_smoke(context, port, state);
        context.stop();
        co_return;
    }
    if (multipath)
    {
        co_await run_multipath_smoke(context, port, state, enable_path_mtu_discovery,
            !multipath_pmtu_blackhole, pmtu_blackhole_arm_marker);
        context.stop();
        co_return;
    }
    // Run the normal request semantics before the ticket/replay connections.
    // This keeps the HTTP/3 request regression independent from the separate
    // 0-RTT lifecycle and still exercises close_async() on both paths.
    co_await run_http_semantics(context, port, state);
    if (!state.ok)
    {
        context.stop();
        co_return;
    }
    co_await run_priority_semantics(context, port, state);
    if (!state.ok)
    {
        context.stop();
        co_return;
    }
    const std::string ticket_file = "http3-client-ticket.cache";
    // Bootstrap one ticket over a normal 1-RTT HTTP/3 connection. The next
    // client instance opts into early data explicitly and exercises the
    // rejection-safe replay path when the fixture server keeps its default
    // anti-replay policy (0-RTT disabled).
    cnetmod::http::client_options ticket_options;
    ticket_options.version_pref = cnetmod::http::http_version_preference::http3_only;
    ticket_options.verify_peer = false;
    ticket_options.follow_redirects = false;
    ticket_options.http3_resumption_ticket_file = ticket_file;
    cnetmod::http::client ticket_client{context, ticket_options};
    auto ticket_result = co_await ticket_client.get(
        std::string{"https://127.0.0.1:"} + std::to_string(port) + "/get");
    if (!require_response(state, ticket_result, 200, "get-ok", "ticket bootstrap"))
    {
        co_await ticket_client.close_async();
        context.stop();
        co_return;
    }
    // NewSessionTicket is post-handshake; give the QUIC driver a short window
    // to process it before the client is closed and the cache is inspected.
    (void)co_await cnetmod::async_timer_wait(context,
        std::chrono::milliseconds{100});
    auto ticket_refresh = co_await ticket_client.get(
        std::string{"https://127.0.0.1:"} + std::to_string(port) + "/get");
    require_response(state, ticket_refresh, 200, "get-ok", "ticket refresh");
    co_await ticket_client.close_async();
    if (std::filesystem::exists(ticket_file))
    {
        const std::string replay_ticket_file = ticket_file + ".replay";
        std::error_code copy_error;
        std::filesystem::copy_file(ticket_file, replay_ticket_file,
            std::filesystem::copy_options::overwrite_existing, copy_error);
        if (copy_error)
        {
            state.fail("could not snapshot the TLS ticket for replay testing: " +
                copy_error.message());
            context.stop();
            co_return;
        }
        cnetmod::http::client_options early_options = ticket_options;
        early_options.enable_http3_early_data = true;
        cnetmod::http::client early_client{context, early_options};
        auto early_result = co_await early_client.get(
            std::string{"https://127.0.0.1:"} + std::to_string(port) + "/get");
        require_response(state, early_result, 200, "get-ok", "early-data fallback");
        co_await early_client.close_async();

        // The fixture accepts 0-RTT with an application-owned replay cache.
        // Query it over a fresh 1-RTT connection so this assertion proves the
        // request really reached the server in early data.
        cnetmod::http::client_options status_options = ticket_options;
        status_options.http3_resumption_ticket_file.clear();
        cnetmod::http::client status_client{context, status_options};
        auto accepted_count = co_await status_client.get(
            std::string{"https://127.0.0.1:"} + std::to_string(port) + "/early-count");
        require_response(state, accepted_count, 200, "1", "0-RTT acceptance");

        // Reuse the exact same persisted ticket. The shared replay cache must
        // reject the second early offer, while the idempotent GET is retried
        // once at 1-RTT and still succeeds.
        cnetmod::http::client_options replay_options = ticket_options;
        replay_options.http3_resumption_ticket_file = replay_ticket_file;
        replay_options.enable_http3_early_data = true;
        cnetmod::http::client replay_client{context, replay_options};
        auto replay_result = co_await replay_client.get(
            std::string{"https://127.0.0.1:"} + std::to_string(port) + "/get");
        require_response(state, replay_result, 200, "get-ok", "replayed 0-RTT fallback");
        if (replay_result && replay_result->get_header("x-early-count") != "1")
            state.fail("0-RTT replay unexpectedly reached the application");
        co_await replay_client.close_async();
        co_await status_client.close_async();
    }
    else
    {
        state.fail("fixture did not issue a TLS session ticket");
        context.stop();
        co_return;
    }

    context.stop();
}

} // namespace

auto main(int argc, char** argv) -> int
{
    const auto has_mode = argc == 3 || argc == 4;
    const auto mode = has_mode ? std::string_view{argv[2]} : std::string_view{};
    const auto blackhole_marker_argument = argc == 4 && mode == "--multipath-pmtu-blackhole";
    if (argc != 2 && !(has_mode && (mode == "--multipath" || mode == "--multipath-pmtu" || mode == "--multipath-timeout" || mode == "--multipath-close" || mode == "--connect-close" || mode == "--multipath-pmtu-blackhole")) ||
        (argc == 4 && !blackhole_marker_argument) ||
        (mode == "--multipath-pmtu-blackhole" && argc != 4))
    {
        logger::error{"usage: http_client_http3_e2e <port> [--multipath|--multipath-pmtu|--multipath-timeout|--multipath-close|--connect-close|--multipath-pmtu-blackhole <arm-marker>]"};
        return 2;
    }
    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    test_state state;
    const auto pmtu_blackhole_arm_marker = blackhole_marker_argument
        ? std::filesystem::path{argv[3]}
        : std::filesystem::path{};
    cnetmod::spawn(*context, run_suite(*context, static_cast<std::uint16_t>(std::stoul(argv[1])), state, mode == "--multipath" || mode == "--multipath-pmtu" || mode == "--multipath-pmtu-blackhole", mode == "--multipath-pmtu" || mode == "--multipath-pmtu-blackhole", mode == "--multipath-timeout", mode == "--multipath-pmtu-blackhole", mode == "--multipath-close", mode == "--connect-close", pmtu_blackhole_arm_marker));
    context->run();
    if (!state.ok)
    {
        logger::error{"HTTP/3 client end-to-end regression failed: {}", state.failure};
        return 1;
    }
    logger::info{"HTTP/3 client end-to-end regression passed"};
    return 0;
}
