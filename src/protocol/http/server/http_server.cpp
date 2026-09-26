module;

#include <cnetmod/config.hpp>
#include <ctime>

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
    #include <cstdlib>
    #include <liburing.h>
#endif

module cnetmod.protocol.http;

import std;
import cnetmod.protocol.http.semantics;
import :parser;
import :request;
import :response;
import :router;
import :cookie;
import :server;
import cnetmod.core.error;
import cnetmod.core.log;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.file;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.protocol.http.v2.session;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
import cnetmod.io.platform.io_uring;
import cnetmod.io.platform.io_uring_recv_service;
import cnetmod.io.platform.io_uring_multishot_recv;
#endif

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::http {

// =============================================================================
// MIME Type Inference Implementation
// =============================================================================

auto guess_mime_type(std::string_view ext) noexcept -> std::string_view
{
    if (ext.empty())
        return "application/octet-stream";
    if (ext[0] == '.')
        ext.remove_prefix(1);

    // Text
    if (ext == "html" || ext == "htm")
        return "text/html; charset=utf-8";
    if (ext == "css")
        return "text/css; charset=utf-8";
    if (ext == "js")
        return "application/javascript; charset=utf-8";
    if (ext == "json")
        return "application/json; charset=utf-8";
    if (ext == "xml")
        return "application/xml; charset=utf-8";
    if (ext == "txt")
        return "text/plain; charset=utf-8";
    if (ext == "csv")
        return "text/csv; charset=utf-8";
    if (ext == "md")
        return "text/markdown; charset=utf-8";

    // Images
    if (ext == "png")
        return "image/png";
    if (ext == "jpg" || ext == "jpeg")
        return "image/jpeg";
    if (ext == "gif")
        return "image/gif";
    if (ext == "svg")
        return "image/svg+xml";
    if (ext == "ico")
        return "image/x-icon";
    if (ext == "webp")
        return "image/webp";
    if (ext == "bmp")
        return "image/bmp";

    // Audio/Video
    if (ext == "mp3")
        return "audio/mpeg";
    if (ext == "mp4")
        return "video/mp4";
    if (ext == "webm")
        return "video/webm";
    if (ext == "ogg")
        return "audio/ogg";
    if (ext == "wav")
        return "audio/wav";

    // Compressed/Binary
    if (ext == "pdf")
        return "application/pdf";
    if (ext == "zip")
        return "application/zip";
    if (ext == "gz" || ext == "gzip")
        return "application/gzip";
    if (ext == "tar")
        return "application/x-tar";
    if (ext == "wasm")
        return "application/wasm";

    // Fonts
    if (ext == "woff")
        return "font/woff";
    if (ext == "woff2")
        return "font/woff2";
    if (ext == "ttf")
        return "font/ttf";
    if (ext == "otf")
        return "font/otf";

    return "application/octet-stream";
}

// =============================================================================
// Static File Serving Implementation
// =============================================================================

namespace detail {

    auto serve_dir_body(const static_file_options& opts, request_context& ctx)
        -> task<void>
    {
        auto rel_path = ctx.wildcard();
        if (rel_path.empty())
            rel_path = opts.index_file;

        auto rel_str = std::string(rel_path);
        if (rel_str.find("..") != std::string::npos)
        {
            ctx.text(status::forbidden, "403 Forbidden");
            co_return;
        }

        auto full_path = opts.root / rel_str;

        std::error_code ec;
        if (std::filesystem::is_directory(full_path, ec))
        {
            full_path /= opts.index_file;
        }

        if (!std::filesystem::exists(full_path, ec))
        {
            ctx.not_found();
            co_return;
        }

        auto f = file::open(full_path, open_mode::read);
        if (!f)
        {
            ctx.text(status::internal_server_error, "500 Cannot open file");
            co_return;
        }

        auto file_size_r = f->size();
        if (!file_size_r)
        {
            ctx.text(status::internal_server_error, "500 Cannot stat file");
            co_return;
        }
        auto file_size = *file_size_r;

        auto ext = full_path.extension().string();
        auto mime = guess_mime_type(ext);

        auto range_hdr = ctx.get_header("Range");
        std::uint64_t range_start = 0;
        std::uint64_t range_end = file_size - 1;
        bool is_range = false;

        if (!range_hdr.empty() && range_hdr.starts_with("bytes="))
        {
            auto spec = range_hdr.substr(6);
            auto dash = spec.find('-');
            if (dash != std::string_view::npos)
            {
                auto start_str = spec.substr(0, dash);
                auto end_str = spec.substr(dash + 1);
                if (!start_str.empty())
                {
                    std::from_chars(start_str.data(), start_str.data() + start_str.size(),
                        range_start);
                }
                if (!end_str.empty())
                {
                    std::from_chars(end_str.data(), end_str.data() + end_str.size(),
                        range_end);
                }
                if (range_start <= range_end && range_end < file_size)
                {
                    is_range = true;
                }
            }
        }

        auto& resp = ctx.resp();
        if (is_range)
        {
            resp.set_status(status::partial_content);
            auto content_len = range_end - range_start + 1;
            resp.set_header("Content-Range", std::format("bytes {}-{}/{}", range_start, range_end, file_size));
            resp.set_header("Content-Length", std::to_string(content_len));
        }
        else
        {
            resp.set_status(status::ok);
            resp.set_header("Content-Length", std::to_string(file_size));
            range_start = 0;
            range_end = file_size - 1;
        }
        resp.set_header("Content-Type", mime);
        resp.set_header("Accept-Ranges", "bytes");

        auto header_data = resp.serialize();
        auto wr = co_await async_write_all(
            ctx.io_ctx(), ctx.raw_socket(),
            const_buffer{header_data.data(), header_data.size()});
        if (!wr)
            co_return;

        constexpr std::size_t CHUNK_SIZE = 65536;
        std::vector<std::byte> buf(CHUNK_SIZE);
        std::uint64_t offset = range_start;
        std::uint64_t remaining = range_end - range_start + 1;

        while (remaining > 0)
        {
            auto to_read = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, CHUNK_SIZE));
            auto rd = co_await async_file_read(
                ctx.io_ctx(), *f, mutable_buffer{buf.data(), to_read}, offset);
            if (!rd || *rd == 0)
                break;

            auto wf = co_await async_write_all(ctx.io_ctx(), ctx.raw_socket(),
                const_buffer{buf.data(), *rd});
            if (!wf)
                break;

            offset += *rd;
            remaining -= *rd;
        }

        resp.set_header("X-Streamed", "1");
        co_return;
    }

} // namespace detail

auto serve_dir(static_file_options opts) -> handler_fn
{
    auto opts_ptr = std::make_shared<static_file_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void>
    {
        return detail::serve_dir_body(*opts_ptr, ctx);
    };
}

// =============================================================================
// File Upload Implementation
// =============================================================================

namespace detail {

    auto save_upload_body(const upload_options& opts, request_context& ctx)
        -> task<void>
    {
        auto body = ctx.body();
        if (body.empty())
        {
            ctx.text(status::bad_request, "Empty body");
            co_return;
        }

        if (body.size() > opts.max_size)
        {
            ctx.text(status::payload_too_large, "File too large");
            co_return;
        }

        std::error_code ec;
        std::filesystem::create_directories(opts.save_dir, ec);

        auto ct = ctx.get_header("Content-Type");
        auto ct_parsed = parse_content_type(ct);

        if (ct_parsed.mime == "multipart/form-data")
        {
            auto form_r = ctx.parse_form();
            if (!form_r)
            {
                ctx.text(status::bad_request, std::format("Multipart parse error: {}", form_r.error().message()));
                co_return;
            }
            auto& form = **form_r;

            std::string json_files = "[";
            bool first = true;

            for (auto& ff : form.all_files())
            {
                auto fname = ff.filename;
                if (fname.empty())
                    fname = opts.default_filename;
                if (fname.find("..") != std::string::npos ||
                    fname.find('/') != std::string::npos ||
                    fname.find('\\') != std::string::npos)
                {
                    fname = opts.default_filename;
                }

                auto full_path = opts.save_dir / fname;
                auto f = file::open(full_path, open_mode::write | open_mode::create | open_mode::truncate);
                if (!f)
                    continue;

                auto wr = co_await async_file_write(
                    ctx.io_ctx(), *f, const_buffer{ff.data.data(), ff.data.size()});
                if (!wr)
                    continue;

                if (!first)
                    json_files += ",";
                json_files += std::format(
                    R"({{"field":"{}","filename":"{}","size":{},"type":"{}"}})",
                    ff.field_name, fname, ff.data.size(), ff.content_type);
                first = false;
            }
            json_files += "]";

            std::string json_fields = "[";
            first = true;
            for (auto& field : form.all_fields())
            {
                if (!first)
                    json_fields += ",";
                json_fields += std::format(R"({{"name":"{}","value":"{}"}})", field.name,
                    field.value);
                first = false;
            }
            json_fields += "]";

            ctx.json(
                status::ok,
                std::format(
                    R"({{"files":{},"fields":{},"file_count":{},"field_count":{}}})",
                    json_files, json_fields, form.file_count(), form.field_count()));
            co_return;
        }

        // Raw body mode
        std::string filename = opts.default_filename;
        auto qs = ctx.query_string();
        auto name_pos = qs.find("name=");
        if (name_pos != std::string_view::npos)
        {
            auto start = name_pos + 5;
            auto end = qs.find('&', start);
            auto name = (end != std::string_view::npos) ? qs.substr(start, end - start)
                                                        : qs.substr(start);
            if (!name.empty())
            {
                auto name_str = std::string(name);
                if (name_str.find("..") == std::string::npos &&
                    name_str.find('/') == std::string::npos &&
                    name_str.find('\\') == std::string::npos)
                {
                    filename = std::move(name_str);
                }
            }
        }

        auto full_path = opts.save_dir / filename;
        auto f = file::open(full_path, open_mode::write | open_mode::create | open_mode::truncate);
        if (!f)
        {
            ctx.text(status::internal_server_error, "Cannot create file");
            co_return;
        }

        auto wr = co_await async_file_write(ctx.io_ctx(), *f,
            const_buffer{body.data(), body.size()});
        if (!wr)
        {
            ctx.text(status::internal_server_error, "Write failed");
            co_return;
        }

        ctx.json(status::ok, std::format(R"({{"filename":"{}","size":{}}})", filename, body.size()));
        co_return;
    }

    auto not_found_handler(request_context& ctx) -> task<void>
    {
        ctx.not_found();
        co_return;
    }

    auto unmatched_route_handler(const router& routes, std::string_view path)
        -> handler_fn
    {
        const auto methods = routes.allowed_methods(path);
        if (methods.empty())
            return not_found_handler;

        std::string allow;
        for (const auto method : methods)
        {
            if (!allow.empty())
                allow += ", ";
            allow += method_to_string(method);
        }
        return [allow = std::move(allow)](request_context& context) -> task<void>
        {
            context.resp().set_header("Allow", allow);
            context.text(status::method_not_allowed,
                "405 Method Not Allowed");
            co_return;
        };
    }

} // namespace detail

auto save_upload(upload_options opts) -> handler_fn
{
    auto opts_ptr = std::make_shared<upload_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void>
    {
        return detail::save_upload_body(*opts_ptr, ctx);
    };
}

// =============================================================================
// Date Cache Implementation
// =============================================================================

auto date_cache::get() -> std::array<char, 29U>
{
    const auto now = std::time(nullptr);
    if (now != cached_time_.load(std::memory_order_acquire) &&
        !refreshing_.test_and_set(std::memory_order_acquire))
    {
        std::tm gmt{};
#ifdef CNETMOD_PLATFORM_WINDOWS
        gmtime_s(&gmt, &now);
#else
        gmtime_r(&now, &gmt);
#endif
        char buf[30];
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);

        // Odd means that a refresh owns the words.  The words themselves are
        // atomic so this is a standard C++ data-race-free seqlock, rather
        // than relying on an unsafe concurrent memcpy of ordinary chars.
        sequence_.fetch_add(1U, std::memory_order_acq_rel);
        std::array<std::uint64_t, word_count> snapshot{};
        std::memcpy(snapshot.data(), buf, date_size);
        for (std::size_t index{}; index < word_count; ++index)
            words_[index].store(snapshot[index], std::memory_order_relaxed);
        cached_time_.store(now, std::memory_order_release);
        sequence_.fetch_add(1U, std::memory_order_release);
        sequence_.notify_all();
        refreshing_.clear(std::memory_order_release);
    }

    for (;;)
    {
        const auto before = sequence_.load(std::memory_order_acquire);
        if ((before & 1U) != 0U)
        {
            sequence_.wait(before, std::memory_order_relaxed);
            continue;
        }

        std::array<std::uint64_t, word_count> snapshot{};
        for (std::size_t index{}; index < word_count; ++index)
            snapshot[index] = words_[index].load(std::memory_order_relaxed);

        if (sequence_.load(std::memory_order_acquire) == before)
        {
            std::array<char, date_size> result{};
            std::memcpy(result.data(), snapshot.data(), result.size());
            return result;
        }
    }
}

// =============================================================================
// Server Implementation
// =============================================================================

struct server::conn_count_guard
{
    std::atomic<std::size_t>* counter;

    conn_count_guard(std::atomic<std::size_t>& c) noexcept
        : counter(&c)
    {
        counter->fetch_add(1, std::memory_order_relaxed);
    }

    ~conn_count_guard()
    {
        if (counter)
            counter->fetch_sub(1, std::memory_order_release);
    }

    conn_count_guard(conn_count_guard&& other) noexcept
        : counter(std::exchange(other.counter, nullptr)) {}

    conn_count_guard(const conn_count_guard&) = delete;
    auto operator=(const conn_count_guard&) -> conn_count_guard& = delete;
};

/**
 * @brief Tracks a socket only while its owning coroutine can use it.
 *
 * Intrusive registration does not allocate. Removal precedes destruction of
 * the socket parameter, preventing shutdown from accessing a retired socket.
 */
struct server::connection_registration
{
    server& owner;
    socket& client;
    cancel_token cancellation;
    connection_registration* previous{};
    connection_registration* next{};

    connection_registration(server& server, socket& connection) noexcept
        : owner(server), client(connection)
    {
        concurrent_containers::exclusive_latch_guard lock{owner.connections_latch_};
        next = owner.connections_;
        if (next)
            next->previous = this;
        owner.connections_ = this;
        if (owner.connections_aborted_)
        {
            cancellation.cancel();
            client.shutdown_both();
        }
    }

    ~connection_registration()
    {
        concurrent_containers::exclusive_latch_guard lock{owner.connections_latch_};
        if (previous)
            previous->next = next;
        else
            owner.connections_ = next;
        if (next)
            next->previous = previous;
    }

    connection_registration(const connection_registration&) = delete;
    auto operator=(const connection_registration&) -> connection_registration& = delete;
};

server::server(io_context& ctx)
    : ctx_(ctx) {}

server::server(server_context& sctx)
    : ctx_(sctx.accept_io()), sctx_(&sctx) {}

#ifdef CNETMOD_HAS_SSL
void server::set_ssl_context(ssl_context& ssl_ctx)
{
    ssl_ctx_ = &ssl_ctx;
}
#endif

auto server::listen(std::string_view host, std::uint16_t port,
    socket_options opts)
    -> std::expected<void, std::error_code>
{
    auto addr_r = ip_address::from_string(host);
    if (!addr_r)
        return std::unexpected(addr_r.error());

    acc_ = std::make_unique<tcp::acceptor>(ctx_);
    auto ep = cnetmod::endpoint{*addr_r, port};
    opts.reuse_address = true;
    auto r = acc_->open(ep, opts);
    if (!r)
        return std::unexpected(r.error());

    host_ = std::string(host);
    port_ = port;
    return {};
}

auto server::local_endpoint()
    -> std::expected<cnetmod::endpoint, std::error_code>
{
    if (!acc_)
        return std::unexpected(make_error_code(errc::bad_descriptor));
    return acc_->native_socket().local_endpoint();
}

void server::set_router(router r)
{
    router_ = std::move(r);
}

void server::use(middleware_fn mw)
{
    if (mw)
        middlewares_.push_back(std::move(mw));
}

void server::set_max_connections(std::size_t n)
{
    max_connections_ = n;
}

void server::set_response_header_options(
    response_header_options options) noexcept
{
    response_headers_ = options;
}

auto server::active_connections() const noexcept -> std::size_t
{
    return active_connections_.load(std::memory_order_acquire);
}

void server::stop()
{
    running_ = false;
    accept_cancellation_.cancel();
    if (acc_ && !accept_cancellation_.pending_.load(std::memory_order_acquire))
        acc_->close();
}

void server::abort_connections() noexcept
{
    concurrent_containers::exclusive_latch_guard lock{connections_latch_};
    connections_aborted_ = true;
    for (auto* connection = connections_; connection; connection = connection->next)
    {
        // Cancellation adapters marshal kernel cancellation to the operation's
        // owning io_context. Do not close a worker-owned socket from the accept
        // or control loop: IOCP and io_uring registrations are loop-affine.
        connection->cancellation.cancel();
    }
}

// =============================================================================
// Server Run Method
// =============================================================================

namespace {

    /**
     * @brief Reports an isolated connection failure without exposing exception text.
     *
     * Called while the guarded dispatch still owns the connection coroutine.
     * Diagnostic failures are contained by spawn_guarded.
     */
    void report_connection_failure(std::exception_ptr failure)
    {
        std::error_code code = std::make_error_code(std::errc::io_error);
        try
        {
            if (failure)
                std::rethrow_exception(failure);
        }
        catch (const std::system_error& error)
        {
            code = error.code();
        }
        catch (const std::bad_alloc&)
        {
            code = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
        }
        logger::error("HTTP connection task failed: category={}, code={}", code.category().name(), code.value());
    }

    /**
     * @brief Delivers an admission response before draining the rejected peer.
     *
     * The caller bounds both writing and draining with one cancellation deadline.
     * Half-closing avoids discarding the response when unread request bytes remain.
     */
    auto reject_connection(io_context& io, socket& client, cancel_token& token)
        -> task<std::expected<void, std::error_code>>
    {
        response resp(status::too_many_requests);
        resp.set_header("Connection", "close");
        resp.set_header("Retry-After", "1");
        resp.set_body(std::string_view{"429 Too Many Requests: too many connections"});
        const auto data = resp.serialize();
        const auto written = co_await async_write_all(io, client,
            const_buffer{data.data(), data.size()}, token);
        if (!written)
            co_return std::unexpected(written.error());
        client.shutdown_send();
        std::array<std::byte, 4096> discarded;
        while (!token.is_cancelled())
        {
            const auto received = co_await async_read(io, client,
                mutable_buffer{discarded.data(), discarded.size()}, token);
            if (!received || *received == 0)
                break;
        }
        co_return {};
    }

} // namespace

auto server::run() -> task<void>
{
    accept_cancellation_.reset();
    running_ = true;
    while (running_)
    {
        auto r = co_await async_accept(ctx_, acc_->native_socket(), accept_cancellation_);
        if (!r)
        {
            if (!running_)
                break;
            continue;
        }

        if (!running_)
            break;

        // Connection limit check
        if (max_connections_ > 0 &&
            active_connections_.load(std::memory_order_relaxed) >=
                max_connections_)
        {
            // A rejected peer cannot hold the listener indefinitely. The
            // deadline wrapper joins its timer and I/O before token reuse.
            (void)co_await with_timeout(ctx_, std::chrono::seconds{1},
                reject_connection(ctx_, *r, accept_cancellation_), accept_cancellation_);
            r->close();
            if (running_)
                accept_cancellation_.reset();
            continue;
        }

        if (sctx_)
        {
            // Multi-core mode: round-robin dispatch to worker io_context
            auto& worker = sctx_->next_worker_io();
            spawn_guarded<report_connection_failure>(worker,
                handle_connection(std::move(*r), worker, conn_count_guard{active_connections_}));
        }
        else
        {
            // Single-threaded mode: handle on current io_context
            spawn_guarded<report_connection_failure>(ctx_,
                handle_connection(std::move(*r), ctx_, conn_count_guard{active_connections_}));
        }
    }
    if (acc_)
        acc_->close();
}

// =============================================================================
// Connection Handling
// =============================================================================

auto server::handle_connection(socket client, io_context& io, conn_count_guard ownership) -> task<void>
{
    connection_registration registration{*this, client};

#ifdef CNETMOD_PLATFORM_WINDOWS
    // A local, small HTTP response commonly completes WSASend inline.  The
    // IOCP awaiters already handle that path without suspension, but Windows
    // only suppresses the otherwise redundant completion-port notification
    // when this mode is enabled on the connected socket.  This is a best
    // effort capability: an unusual Winsock provider may decline it without
    // changing HTTP semantics.
    (void)client.apply_options(
        {.non_blocking = false, .skip_completion_on_success = true});
#endif

#ifdef CNETMOD_HAS_SSL
    // --- TLS path ---
    if (ssl_ctx_)
    {
        ssl_stream ssl(*ssl_ctx_, io, client);
        ssl.set_accept_state();
        auto hr = co_await ssl.async_handshake(registration.cancellation);
        if (!hr)
        {
            co_return;
        }

        if (ssl.get_alpn_selected() == "h2")
        {
            co_await handle_h2_tls(client, io, ssl, registration.cancellation);
        }
        else
        {
            co_await handle_h1_tls(client, io, ssl, registration.cancellation);
        }
        co_return;
    }
#endif

    // --- Cleartext path ---
    // Read initial data and check for HTTP/2 client connection preface
    constexpr std::string_view h2_client_magic =
        "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    std::array<std::byte, 8192> peek_buf{};
    auto peek_rd = co_await async_read(
        io, client, mutable_buffer{peek_buf.data(), peek_buf.size()},
        registration.cancellation);
    if (!peek_rd || *peek_rd == 0)
    {
        co_return;
    }
    auto peek_len = *peek_rd;

    auto matches_h2_preface_prefix = [&]() -> bool
    {
        if (peek_len > h2_client_magic.size())
            return false;
        auto view = std::string_view{
            reinterpret_cast<const char*>(peek_buf.data()),
            peek_len,
        };
        return h2_client_magic.starts_with(view);
    };

    while (peek_len < h2_client_magic.size() && matches_h2_preface_prefix())
    {
        auto rd = co_await async_read(
            io, client,
            mutable_buffer{peek_buf.data() + peek_len, peek_buf.size() - peek_len},
            registration.cancellation);
        if (!rd || *rd == 0)
        {
            co_return;
        }
        peek_len += *rd;
    }

    if (peek_len >= h2_client_magic.size() &&
        std::string_view{reinterpret_cast<const char*>(peek_buf.data()),
            h2_client_magic.size()} == h2_client_magic)
    {
        // TCP_NODELAY reduces latency for HTTP/2 multiplexed frames
        (void)client.apply_options({.non_blocking = false, .no_delay = true});

        co_await handle_h2(client, io, {peek_buf.data(), peek_len},
            registration.cancellation);
        co_return;
    }

    // HTTP/1.1 cleartext — feed already-read bytes to parser
    co_await handle_h1_clear(
        client, io, reinterpret_cast<const char*>(peek_buf.data()), peek_len,
        registration.cancellation);
}

auto server::make_h2_streaming_handler(io_context& io, socket& client)
    -> v2::streaming_server_handler
{
    return [this, &io, &client](v2::server_request request,
               cancel_token& cancellation) -> task<v2::server_response>
    {
        std::string method = "GET";
        std::string uri = "/";
        header_map headers;
        headers.reserve(request.headers.size());
        for (const auto& field : request.headers)
        {
            if (field.name == ":method")
                method = field.value;
            else if (field.name == ":path")
                uri = field.value;
            else if (!field.name.starts_with(':'))
                headers[field.name] = field.value;
        }
        const auto query = uri.find('?');
        const auto path = query == std::string::npos
            ? std::string_view(uri)
            : std::string_view(uri).substr(0, query);
        auto match = router_.match(method, path);
        response output(status::ok, http_version::http_2);
        if (response_headers_.emit_server)
            output.set_header("Server", "cnetmod");
        if (response_headers_.emit_date)
            output.set_cached_date_header(date_cache_.get());
        route_params params;
        handler_fn route;
        if (match)
        {
            route = std::move(match->handler);
            params = std::move(match->params);
        }
        else
        {
            route = detail::unmatched_route_handler(router_, path);
        }
        std::string buffered_body;
        if (request.body_stream && (!match || !match->request_stream))
        {
            while (auto chunk = co_await request.body_stream->receive())
                buffered_body.append(
                    reinterpret_cast<const char*>(chunk->data()), chunk->size());
        }
        if (request.body_stream && match && match->request_stream)
            request.body_stream->constrain_limit(match->request_stream->max_bytes);
        const auto body = request.body.empty()
            ? std::string_view{buffered_body}
            : std::string_view{reinterpret_cast<const char*>(request.body.data()),
                  request.body.size()};
        request_context context(io, client, method, uri, headers, body, output,
            std::move(params), match && match->request_stream ? request.body_stream : std::shared_ptr<request_body_stream>{});
        if (cancellation.is_cancelled())
            context.cancel_pending_operations();
        co_await execute_chain(context, route);
        v2::server_response result;
        if (request.body_stream && request.body_stream->error() == std::make_error_code(std::errc::message_size))
            output.set_status(status::payload_too_large);
        result.status = static_cast<std::uint32_t>(output.status_code());
        result.body.assign(
            reinterpret_cast<const std::byte*>(output.body().data()),
            reinterpret_cast<const std::byte*>(output.body().data()) +
                output.body().size());
        // HTTP/2 owns this response until HPACK has encoded it, so reserve the
        // exact number of application headers before copying them.  The usual
        // response has Date, Server and Content-Length; growing from an empty
        // vector otherwise allocates repeatedly on every request.
        result.headers.reserve(output.headers().size());
        for (const auto& [name, value] : output.headers())
        {
            std::string lowercase;
            lowercase.reserve(name.size());
            for (const auto character : name)
                lowercase.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            result.headers.push_back({std::move(lowercase), value});
        }
        if (const auto date = output.cached_date_header(); !date.empty())
            result.headers.push_back({"date", std::string{date}});
        result.trailers.reserve(output.trailers().size());
        for (const auto& [name, value] : output.trailers())
        {
            std::string lowercase;
            lowercase.reserve(name.size());
            for (const auto character : name)
                lowercase.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            result.trailers.push_back({std::move(lowercase), value});
        }
        co_return result;
    };
}

auto server::handle_h2(socket& client, io_context& io,
    std::span<const std::byte> initial, cancel_token& cancellation) -> task<void>
{
    auto reader = [&io, &client, &cancellation](mutable_buffer buffer)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        co_return co_await async_read(io, client, buffer, cancellation);
    };
    auto writer = [&io, &client, &cancellation](const_buffer buffer)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_write_all(io, client, buffer, cancellation);
    };
    v2::session session(io, client, make_h2_streaming_handler(io, client), {},
        std::move(reader), std::move(writer));
    co_await session.run(initial);
}

// =============================================================================
// HTTP/1.1 Cleartext Handler
// =============================================================================

auto server::handle_h1_clear(socket& client, io_context& io,
    const char* initial_data, std::size_t initial_len,
    cancel_token& cancellation)
    -> task<void>
{
    bool keep_alive = true;
    response resp(status::ok);
    std::string response_wire;
    request_parser parser;
    std::array<std::byte, 8192> buf;

#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)
    std::unique_ptr<io_uring_recv_service> recv_service;
    std::unique_ptr<io_uring_multishot_recv> recv_stream;
    if (const auto* enabled = std::getenv("CNETMOD_HTTP_MULTISHOT_RECV");
        enabled && enabled[0] != '\0' && enabled[0] != '0')
    {
        if (auto* uring = dynamic_cast<io_uring_context*>(&io))
        {
            // This per-connection adapter is deliberately opt-in.  The next
            // iteration promotes the service to worker-context ownership.
            static std::atomic<unsigned> next_group{1};
            const auto group = static_cast<std::uint16_t>(
                next_group.fetch_add(1, std::memory_order_relaxed));
            try
            {
                recv_service =
                    std::make_unique<io_uring_recv_service>(*uring, group, 4096, 16);
                recv_stream = recv_service->make_receiver(client);
            }
            catch (...)
            {
                recv_service.reset();
            }
        }
    }

    auto stop_recv = [&]() -> task<void>
    {
        if (recv_stream)
            (void)co_await recv_stream->async_stop();
    };
#endif

    while (keep_alive)
    {
        parser.reset();

        // Feed pre-read data on first iteration
        if (initial_data && initial_len > 0)
        {
            auto consumed = parser.consume(initial_data, initial_len);
            if (!consumed)
                co_return;
            initial_data = nullptr;
            initial_len = 0;
        }

        auto receive_more = [&]() -> task<bool>
        {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)
            if (recv_stream)
            {
                auto rd = co_await recv_stream->async_next();
                if (!rd || rd->size() == 0)
                {
                    co_await stop_recv();
                    co_return false;
                }
                auto data = rd->data();
                auto consumed =
                    parser.consume(static_cast<const char*>(data.data), data.size);
                rd->release();
                if (!consumed)
                {
                    co_await stop_recv();
                    co_return false;
                }
                co_return true;
            }
#endif
            auto rd = co_await async_read(io, client,
                mutable_buffer{buf.data(), buf.size()}, cancellation);
            if (!rd || *rd == 0)
            {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)
                co_await stop_recv();
#endif
                co_return false;
            }

            auto consumed =
                parser.consume(reinterpret_cast<const char*>(buf.data()), *rd);
            if (!consumed)
            {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)
                co_await stop_recv();
#endif
                co_return false;
            }
            co_return true;
        };

        while (!parser.headers_ready())
            if (!(co_await receive_more()))
                co_return;

        // Route matching
        auto uri = parser.uri();
        auto qpos = uri.find('?');
        auto path = (qpos != std::string_view::npos) ? uri.substr(0, qpos) : uri;

        auto mr = router_.match(parser.method(), path);

        resp.reset(status::ok, parser.version());
        if (response_headers_.emit_server)
            resp.set_header("Server", "cnetmod");
        if (response_headers_.emit_date)
            resp.set_cached_date_header(date_cache_.get());
        route_params rp;
        handler_fn handler;

        if (mr)
        {
            handler = std::move(mr->handler);
            rp = std::move(mr->params);
        }
        else
        {
            handler = detail::unmatched_route_handler(router_, path);
        }

        if (mr && mr->request_stream)
        {
            const auto options = *mr->request_stream;
            auto body_stream = std::make_shared<request_body_stream>(
                options.chunk_capacity, options.max_bytes);
            request_context rctx(io, client, parser.method(), parser.uri(),
                parser.headers(), {}, resp, std::move(rp), body_stream);
            auto pump_body = [&]() -> task<void>
            {
                for (;;)
                {
                    auto data = parser.take_body_chunk();
                    if (!data.empty() && !body_stream->is_closed())
                    {
                        request_body_chunk chunk(
                            reinterpret_cast<const std::byte*>(data.data()),
                            reinterpret_cast<const std::byte*>(data.data()) +
                                data.size());
                        (void)co_await body_stream->send(std::move(chunk));
                    }
                    if (parser.ready())
                    {
                        body_stream->close();
                        co_return;
                    }
                    if (!(co_await receive_more()))
                    {
                        body_stream->fail(
                            std::make_error_code(std::errc::connection_aborted));
                        co_return;
                    }
                }
            };
            auto invoke_handler = [&]() -> task<void>
            {
                co_await execute_chain(rctx, handler);
                body_stream->close();
            };
            co_await when_all(invoke_handler(), pump_body());
            if (body_stream->error() ==
                    std::make_error_code(std::errc::message_size) &&
                resp.get_header("X-Streamed") != "1")
                rctx.text(status::payload_too_large,
                    "Request body exceeds the route limit");
        }
        else
        {
            while (!parser.ready())
                if (!(co_await receive_more()))
                    co_return;
            request_context rctx(io, client, parser, resp, std::move(rp));
            co_await execute_chain(rctx, handler);
        }
        // Check if chunked encoding is needed
        bool use_chunked = false;
        if (resp.get_header("X-Streamed") != "1")
        {
            // If no Content-Length and body not empty, use chunked
            if (resp.get_header("Content-Length").empty() && !resp.body().empty() &&
                parser.version() == http_version::http_1_1)
            {
                use_chunked = true;
                resp.set_header("Transfer-Encoding", "chunked");
            }

            if (use_chunked)
            {
                // Send chunked response
                co_await send_chunked_response(io, client, resp, cancellation);
            }
            else
            {
                // Send normal response
                resp.serialize_to(response_wire);
                auto wr = co_await async_write_all(
                    io, client,
                    const_buffer{response_wire.data(), response_wire.size()},
                    cancellation);
                if (!wr)
                {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)
                    co_await stop_recv();
#endif
                    co_return;
                }
            }
        }

        auto conn_hdr = parser.get_header("Connection");
        if (parser.version() == http_version::http_1_1)
        {
            keep_alive = (conn_hdr != "close");
        }
        else
        {
            keep_alive = false;
        }
    }

#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)
    co_await stop_recv();
#endif
}

#ifdef CNETMOD_HAS_SSL
// =============================================================================
// TLS Handlers
// =============================================================================

auto server::handle_h2_tls(socket& client, io_context& io, ssl_stream& ssl,
    cancel_token& cancellation) -> task<void>
{
    auto reader = [&ssl, &cancellation](mutable_buffer buffer)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        co_return co_await ssl.async_read(buffer, cancellation);
    };
    auto writer =
        [&ssl, &cancellation](
            const_buffer buffer) -> task<std::expected<void, std::error_code>>
    {
        co_return co_await ssl.async_write_all(buffer, cancellation);
    };
    v2::session session(io, client, make_h2_streaming_handler(io, client), {},
        std::move(reader), std::move(writer));
    co_await session.run();
}

auto server::handle_h1_tls(socket& client, io_context& io, ssl_stream& ssl,
    cancel_token& cancellation) -> task<void>
{
    bool keep_alive = true;
    response resp(status::ok);
    std::string response_wire;
    request_parser parser;
    std::array<std::byte, 8192> buf;
    auto configure_stream_writer = [&ssl](request_context& context)
    {
        context.set_stream_writer(
            [&ssl](std::string_view bytes, cancel_token& token)
                -> task<std::expected<void, std::error_code>>
            {
                co_return co_await ssl.async_write_all(
                    const_buffer{bytes.data(), bytes.size()}, token);
            });
    };

    while (keep_alive)
    {
        parser.reset();

        auto receive_more = [&]() -> task<bool>
        {
            auto rd = co_await ssl.async_read(
                mutable_buffer{buf.data(), buf.size()}, cancellation);
            if (!rd || *rd == 0)
                co_return false;

            auto consumed =
                parser.consume(reinterpret_cast<const char*>(buf.data()), *rd);
            if (!consumed)
                co_return false;
            co_return true;
        };

        while (!parser.headers_ready())
            if (!(co_await receive_more()))
                co_return;

        auto uri = parser.uri();
        auto qpos = uri.find('?');
        auto path = (qpos != std::string_view::npos) ? uri.substr(0, qpos) : uri;

        auto mr = router_.match(parser.method(), path);

        resp.reset(status::ok, parser.version());
        if (response_headers_.emit_server)
            resp.set_header("Server", "cnetmod");
        if (response_headers_.emit_date)
            resp.set_cached_date_header(date_cache_.get());
        route_params rp;
        handler_fn handler;

        if (mr)
        {
            handler = std::move(mr->handler);
            rp = std::move(mr->params);
        }
        else
        {
            handler = detail::unmatched_route_handler(router_, path);
        }

        if (mr && mr->request_stream)
        {
            const auto options = *mr->request_stream;
            auto body_stream = std::make_shared<request_body_stream>(
                options.chunk_capacity, options.max_bytes);
            request_context rctx(io, client, parser.method(), parser.uri(),
                parser.headers(), {}, resp, std::move(rp), body_stream);
            configure_stream_writer(rctx);
            auto pump_body = [&]() -> task<void>
            {
                for (;;)
                {
                    auto data = parser.take_body_chunk();
                    if (!data.empty() && !body_stream->is_closed())
                    {
                        request_body_chunk chunk(
                            reinterpret_cast<const std::byte*>(data.data()),
                            reinterpret_cast<const std::byte*>(data.data()) +
                                data.size());
                        (void)co_await body_stream->send(std::move(chunk));
                    }
                    if (parser.ready())
                    {
                        body_stream->close();
                        co_return;
                    }
                    if (!(co_await receive_more()))
                    {
                        body_stream->fail(
                            std::make_error_code(std::errc::connection_aborted));
                        co_return;
                    }
                }
            };
            auto invoke_handler = [&]() -> task<void>
            {
                co_await execute_chain(rctx, handler);
                body_stream->close();
            };
            co_await when_all(invoke_handler(), pump_body());
            if (body_stream->error() ==
                    std::make_error_code(std::errc::message_size) &&
                resp.get_header("X-Streamed") != "1")
                rctx.text(status::payload_too_large,
                    "Request body exceeds the route limit");
        }
        else
        {
            while (!parser.ready())
                if (!(co_await receive_more()))
                    co_return;
            request_context rctx(io, client, parser, resp, std::move(rp));
            configure_stream_writer(rctx);
            co_await execute_chain(rctx, handler);
        }
        // Check if chunked encoding is needed
        bool use_chunked = false;
        if (resp.get_header("X-Streamed") != "1")
        {
            // If no Content-Length and body not empty, use chunked
            if (resp.get_header("Content-Length").empty() && !resp.body().empty() &&
                parser.version() == http_version::http_1_1)
            {
                use_chunked = true;
                resp.set_header("Transfer-Encoding", "chunked");
            }

            if (use_chunked)
            {
                // Send chunked response (TLS)
                co_await send_chunked_response_tls(io, ssl, resp, cancellation);
            }
            else
            {
                // Send normal response
                resp.serialize_to(response_wire);
                auto wr = co_await ssl.async_write_all(
                    const_buffer{response_wire.data(), response_wire.size()},
                    cancellation);
                if (!wr)
                    co_return;
            }
        }

        auto conn_hdr = parser.get_header("Connection");
        if (parser.version() == http_version::http_1_1)
        {
            keep_alive = (conn_hdr != "close");
        }
        else
        {
            keep_alive = false;
        }
    }
}
#endif // CNETMOD_HAS_SSL

// =============================================================================
// Middleware Chain Execution
// =============================================================================

auto server::execute_chain(request_context& ctx, handler_fn& handler,
    std::size_t idx) -> task<void>
{
    if (idx >= middlewares_.size())
    {
        co_await handler(ctx);
        co_return;
    }
    // next is a regular (non-coroutine) closure that directly returns the next
    // layer's task<void>
    next_fn next = [this, &ctx, &handler, idx]() -> task<void>
    {
        return execute_chain(ctx, handler, idx + 1);
    };
    co_await middlewares_[idx](ctx, next);
}

// =============================================================================
// Chunked Response Sending
// =============================================================================

auto server::send_chunked_response(io_context& io, socket& client,
    response& resp, cancel_token& cancellation) -> task<void>
{
    // Send headers (without body)
    auto body = std::move(resp.body());
    resp.set_body(std::string{}); // Clear body

    auto header_data = resp.serialize();
    auto wr = co_await async_write_all(
        io, client, const_buffer{header_data.data(), header_data.size()}, cancellation);
    if (!wr)
        co_return;

    // Send chunked body
    constexpr std::size_t CHUNK_SIZE = 8192;
    std::size_t offset = 0;

    while (offset < body.size())
    {
        auto chunk_size = std::min(CHUNK_SIZE, body.size() - offset);

        // Send chunk size (hex)
        auto size_str = std::format("{:x}\r\n", chunk_size);
        auto wr1 = co_await async_write_all(
            io, client, const_buffer{size_str.data(), size_str.size()}, cancellation);
        if (!wr1)
            co_return;

        // Send chunk data
        auto wr2 = co_await async_write_all(
            io, client, const_buffer{body.data() + offset, chunk_size}, cancellation);
        if (!wr2)
            co_return;

        // Send chunk ending \r\n
        auto wr3 = co_await async_write_all(
            io, client, const_buffer{"\r\n", 2}, cancellation);
        if (!wr3)
            co_return;

        offset += chunk_size;
    }

    // Send end chunk (0\r\n\r\n)
    auto wr_end =
        co_await async_write_all(
            io, client, const_buffer{"0\r\n\r\n", 5}, cancellation);
    (void)wr_end;
}

#ifdef CNETMOD_HAS_SSL
auto server::send_chunked_response_tls(io_context&, ssl_stream& ssl,
    response& resp, cancel_token& cancellation) -> task<void>
{
    // Send headers (without body)
    auto body = std::move(resp.body());
    resp.set_body(std::string{}); // Clear body

    auto header_data = resp.serialize();
    auto wr = co_await ssl.async_write_all(
        const_buffer{header_data.data(), header_data.size()}, cancellation);
    if (!wr)
        co_return;

    // Send chunked body
    constexpr std::size_t CHUNK_SIZE = 8192;
    std::size_t offset = 0;

    while (offset < body.size())
    {
        auto chunk_size = std::min(CHUNK_SIZE, body.size() - offset);

        // One TLS record per HTTP chunk. This preserves streaming latency while
        // avoiding three SSL_write calls for the size, payload and CRLF.
        auto record = std::format("{:x}\r\n", chunk_size);
        record.append(body.data() + offset, chunk_size);
        record += "\r\n";
        auto written = co_await ssl.async_write_all(
            const_buffer{record.data(), record.size()}, cancellation);
        if (!written)
            co_return;

        offset += chunk_size;
    }

    // Send end chunk (0\r\n\r\n)
    auto wr_end = co_await ssl.async_write_all(
        const_buffer{"0\r\n\r\n", 5}, cancellation);
    (void)wr_end;
}
#endif

} // namespace cnetmod::http
