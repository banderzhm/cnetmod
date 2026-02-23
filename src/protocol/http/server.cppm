module;

#include <cnetmod/config.hpp>
#include <ctime>

export module cnetmod.protocol.http:server;

import std;
import :types;
import :parser;
import :request;
import :response;
import :router;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.file;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

#ifdef CNETMOD_HAS_NGHTTP2
import :stream_io;
import :h2_types;
import :h2_session;
#endif

namespace cnetmod::http {

// =============================================================================
// MIME Type Inference
// =============================================================================

export auto guess_mime_type(std::string_view ext) noexcept -> std::string_view {
    // Compare lowercase first letter is enough (extensions are usually short)
    if (ext.empty()) return "application/octet-stream";
    if (ext[0] == '.') ext.remove_prefix(1);

    // Text
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")   return "text/css; charset=utf-8";
    if (ext == "js")    return "application/javascript; charset=utf-8";
    if (ext == "json")  return "application/json; charset=utf-8";
    if (ext == "xml")   return "application/xml; charset=utf-8";
    if (ext == "txt")   return "text/plain; charset=utf-8";
    if (ext == "csv")   return "text/csv; charset=utf-8";
    if (ext == "md")    return "text/markdown; charset=utf-8";

    // Images
    if (ext == "png")   return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")   return "image/gif";
    if (ext == "svg")   return "image/svg+xml";
    if (ext == "ico")   return "image/x-icon";
    if (ext == "webp")  return "image/webp";
    if (ext == "bmp")   return "image/bmp";

    // Audio/Video
    if (ext == "mp3")   return "audio/mpeg";
    if (ext == "mp4")   return "video/mp4";
    if (ext == "webm")  return "video/webm";
    if (ext == "ogg")   return "audio/ogg";
    if (ext == "wav")   return "audio/wav";

    // Compressed/Binary
    if (ext == "pdf")   return "application/pdf";
    if (ext == "zip")   return "application/zip";
    if (ext == "gz" || ext == "gzip")  return "application/gzip";
    if (ext == "tar")   return "application/x-tar";
    if (ext == "wasm")  return "application/wasm";

    // Fonts
    if (ext == "woff")  return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf")   return "font/ttf";
    if (ext == "otf")   return "font/otf";

    return "application/octet-stream";
}

// =============================================================================
// Static File Serving
// =============================================================================

export struct static_file_options {
    std::filesystem::path root;
    std::string index_file = "index.html";
};

/// serve_dir coroutine implementation (non-exported) — Avoids MSVC 14.50 ICE with coroutine lambda serialization to IFC in exported functions
namespace detail {
auto serve_dir_body(const static_file_options& opts, request_context& ctx) -> task<void> {
    auto rel_path = ctx.wildcard();
    if (rel_path.empty()) rel_path = opts.index_file;

    auto rel_str = std::string(rel_path);
    if (rel_str.find("..") != std::string::npos) {
        ctx.text(status::forbidden, "403 Forbidden");
        co_return;
    }

    auto full_path = opts.root / rel_str;

    std::error_code ec;
    if (std::filesystem::is_directory(full_path, ec)) {
        full_path /= opts.index_file;
    }

    if (!std::filesystem::exists(full_path, ec)) {
        ctx.not_found();
        co_return;
    }

    auto f = file::open(full_path, open_mode::read);
    if (!f) {
        ctx.text(status::internal_server_error, "500 Cannot open file");
        co_return;
    }

    auto file_size_r = f->size();
    if (!file_size_r) {
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

    if (!range_hdr.empty() && range_hdr.starts_with("bytes=")) {
        auto spec = range_hdr.substr(6);
        auto dash = spec.find('-');
        if (dash != std::string_view::npos) {
            auto start_str = spec.substr(0, dash);
            auto end_str = spec.substr(dash + 1);
            if (!start_str.empty()) {
                std::from_chars(start_str.data(),
                    start_str.data() + start_str.size(), range_start);
            }
            if (!end_str.empty()) {
                std::from_chars(end_str.data(),
                    end_str.data() + end_str.size(), range_end);
            }
            if (range_start <= range_end && range_end < file_size) {
                is_range = true;
            }
        }
    }

    auto& resp = ctx.resp();
    if (is_range) {
        resp.set_status(status::partial_content);
        auto content_len = range_end - range_start + 1;
        resp.set_header("Content-Range",
            std::format("bytes {}-{}/{}", range_start, range_end, file_size));
        resp.set_header("Content-Length", std::to_string(content_len));
    } else {
        resp.set_status(status::ok);
        resp.set_header("Content-Length", std::to_string(file_size));
        range_start = 0;
        range_end = file_size - 1;
    }
    resp.set_header("Content-Type", mime);
    resp.set_header("Accept-Ranges", "bytes");

    auto header_data = resp.serialize();
    auto wr = co_await async_write(ctx.io_ctx(), ctx.raw_socket(),
        const_buffer{header_data.data(), header_data.size()});
    if (!wr) co_return;

    constexpr std::size_t CHUNK_SIZE = 65536;
    std::vector<std::byte> buf(CHUNK_SIZE);
    std::uint64_t offset = range_start;
    std::uint64_t remaining = range_end - range_start + 1;

    while (remaining > 0) {
        auto to_read = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, CHUNK_SIZE));
        auto rd = co_await async_file_read(ctx.io_ctx(), *f,
            mutable_buffer{buf.data(), to_read}, offset);
        if (!rd || *rd == 0) break;

        auto wf = co_await async_write(ctx.io_ctx(), ctx.raw_socket(),
            const_buffer{buf.data(), *rd});
        if (!wf) break;

        offset += *rd;
        remaining -= *rd;
    }

    resp.set_header("X-Streamed", "1");
    co_return;
}
} // namespace detail

/// Create static file serving handler (non-coroutine, delegates to detail::serve_dir_body)
/// Route should be registered in the form /prefix/*filepath
export auto serve_dir(static_file_options opts) -> handler_fn {
    auto opts_ptr = std::make_shared<static_file_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void> {
        return detail::serve_dir_body(*opts_ptr, ctx);
    };
}

// =============================================================================
// File Upload Saving
// =============================================================================

export struct upload_options {
    std::filesystem::path save_dir;
    std::string default_filename = "upload.bin";
    std::size_t max_size = 32 * 1024 * 1024;  // 32MB
};

/// save_upload coroutine implementation (non-exported) — Avoids MSVC 14.50 ICE with coroutine lambda serialization to IFC in exported functions
namespace detail {
auto save_upload_body(const upload_options& opts, request_context& ctx) -> task<void> {
    auto body = ctx.body();
    if (body.empty()) {
        ctx.text(status::bad_request, "Empty body");
        co_return;
    }

    if (body.size() > opts.max_size) {
        ctx.text(status::payload_too_large, "File too large");
        co_return;
    }

    std::error_code ec;
    std::filesystem::create_directories(opts.save_dir, ec);

    auto ct = ctx.get_header("Content-Type");
    auto ct_parsed = parse_content_type(ct);

    if (ct_parsed.mime == "multipart/form-data") {
        auto form_r = ctx.parse_form();
        if (!form_r) {
            ctx.text(status::bad_request,
                std::format("Multipart parse error: {}",
                            form_r.error().message()));
            co_return;
        }
        auto& form = **form_r;

        std::string json_files = "[";
        bool first = true;

        for (auto& ff : form.all_files()) {
            auto fname = ff.filename;
            if (fname.empty()) fname = opts.default_filename;
            if (fname.find("..") != std::string::npos ||
                fname.find('/') != std::string::npos ||
                fname.find('\\') != std::string::npos) {
                fname = opts.default_filename;
            }

            auto full_path = opts.save_dir / fname;
            auto f = file::open(full_path,
                open_mode::write | open_mode::create | open_mode::truncate);
            if (!f) continue;

            auto wr = co_await async_file_write(ctx.io_ctx(), *f,
                const_buffer{ff.data.data(), ff.data.size()});
            if (!wr) continue;

            if (!first) json_files += ",";
            json_files += std::format(
                R"({{"field":"{}","filename":"{}","size":{},"type":"{}"}})",
                ff.field_name, fname, ff.data.size(), ff.content_type);
            first = false;
        }
        json_files += "]";

        std::string json_fields = "[";
        first = true;
        for (auto& field : form.all_fields()) {
            if (!first) json_fields += ",";
            json_fields += std::format(
                R"({{"name":"{}","value":"{}"}})",
                field.name, field.value);
            first = false;
        }
        json_fields += "]";

        ctx.json(status::ok, std::format(
            R"({{"files":{},"fields":{},"file_count":{},"field_count":{}}})",
            json_files, json_fields,
            form.file_count(), form.field_count()));
        co_return;
    }

    // === raw body mode ===
    std::string filename = opts.default_filename;
    auto qs = ctx.query_string();
    auto name_pos = qs.find("name=");
    if (name_pos != std::string_view::npos) {
        auto start = name_pos + 5;
        auto end = qs.find('&', start);
        auto name = (end != std::string_view::npos)
            ? qs.substr(start, end - start) : qs.substr(start);
        if (!name.empty()) {
            auto name_str = std::string(name);
            if (name_str.find("..") == std::string::npos &&
                name_str.find('/') == std::string::npos &&
                name_str.find('\\') == std::string::npos) {
                filename = std::move(name_str);
            }
        }
    }

    auto full_path = opts.save_dir / filename;
    auto f = file::open(full_path,
        open_mode::write | open_mode::create | open_mode::truncate);
    if (!f) {
        ctx.text(status::internal_server_error, "Cannot create file");
        co_return;
    }

    auto wr = co_await async_file_write(ctx.io_ctx(), *f,
        const_buffer{body.data(), body.size()});
    if (!wr) {
        ctx.text(status::internal_server_error, "Write failed");
        co_return;
    }

    ctx.json(status::ok,
        std::format(R"({{"filename":"{}","size":{}}})", filename, body.size()));
    co_return;
}

/// 404 coroutine implementation (non-exported) — Avoids MSVC 14.50 ICE with handle_connection coroutine lambda
auto not_found_handler(request_context& ctx) -> task<void> {
    ctx.not_found();
    co_return;
}
} // namespace detail

/// Create file upload handler (non-coroutine, delegates to detail::save_upload_body)
/// Supports both multipart/form-data and raw body modes
export auto save_upload(upload_options opts) -> handler_fn {
    auto opts_ptr = std::make_shared<upload_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void> {
        return detail::save_upload_body(*opts_ptr, ctx);
    };
}

// =============================================================================
// http::server — Advanced HTTP Server
// =============================================================================

/// Cached HTTP Date header string (re-rendered once per second)
/// Thread-safe: uses atomic time_t comparison + shared_ptr swap
class date_cache {
public:
    [[nodiscard]] auto get() -> std::string {
        auto now = std::time(nullptr);
        auto cached = cached_time_.load(std::memory_order_relaxed);
        if (now != cached) {
            // Re-render
            std::tm gmt{};
#ifdef CNETMOD_PLATFORM_WINDOWS
            gmtime_s(&gmt, &now);
#else
            gmtime_r(&now, &gmt);
#endif
            char buf[30];
            std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
            auto s = std::string(buf);
            {
                std::lock_guard lk(mtx_);
                cached_str_ = std::move(s);
            }
            cached_time_.store(now, std::memory_order_relaxed);
        }
        std::lock_guard lk(mtx_);
        return cached_str_;
    }
private:
    std::atomic<std::time_t> cached_time_{0};
    mutable std::mutex mtx_;
    std::string cached_str_;
};

export class server {
public:
    /// Single-threaded mode: all connections handled on the same io_context
    explicit server(io_context& ctx)
        : ctx_(ctx) {}

    /// Multi-core mode: accept on sctx.accept_io(), connections dispatched to worker io_context
    explicit server(server_context& sctx)
        : ctx_(sctx.accept_io()), sctx_(&sctx) {}

#ifdef CNETMOD_HAS_SSL
    /// Configure TLS for this server (enables HTTPS + HTTP/2 via ALPN)
    /// The ssl_context must outlive the server.
    void set_ssl_context(ssl_context& ssl_ctx) {
        ssl_ctx_ = &ssl_ctx;
    }
#endif

    /// Listen on specified address and port
    auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>
    {
        auto addr_r = ip_address::from_string(host);
        if (!addr_r) return std::unexpected(addr_r.error());

        acc_ = std::make_unique<tcp::acceptor>(ctx_);
        auto ep = endpoint{*addr_r, port};
        auto r = acc_->open(ep, {.reuse_address = true});
        if (!r) return std::unexpected(r.error());

        host_ = std::string(host);
        port_ = port;
        return {};
    }

    /// Set router
    void set_router(router r) { router_ = std::move(r); }

    /// Add middleware
    void use(middleware_fn mw) { middlewares_.push_back(std::move(mw)); }

    /// Set max concurrent connections (0 = unlimited)
    void set_max_connections(std::size_t n) { max_connections_ = n; }

    /// Get current active connection count
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t {
        return active_connections_.load(std::memory_order_relaxed);
    }

    /// Run server (accept loop, coroutine)
    auto run() -> task<void> {
        running_ = true;
        while (running_) {
            auto r = co_await async_accept(ctx_, acc_->native_socket());
            if (!r) {
                if (!running_) break;
                continue;
            }

            // Connection limit check
            if (max_connections_ > 0 &&
                active_connections_.load(std::memory_order_relaxed) >= max_connections_) {
                // Reject: send 503 and close
                response resp(status::service_unavailable);
                resp.set_header("Connection", "close");
                resp.set_body(std::string_view{"503 Service Unavailable: too many connections"});
                auto data = resp.serialize();
                (void)co_await async_write(ctx_, *r,
                    const_buffer{data.data(), data.size()});
                r->close();
                continue;
            }

            if (sctx_) {
                // Multi-core mode: round-robin dispatch to worker io_context
                auto& worker = sctx_->next_worker_io();
                spawn_on(worker, handle_connection(
                    std::move(*r), worker));
            } else {
                // Single-threaded mode: handle on current io_context
                spawn(ctx_, handle_connection(
                    std::move(*r), ctx_));
            }
        }
    }

    /// Stop server
    void stop() {
        running_ = false;
        if (acc_) acc_->close();
    }

private:
    /// RAII guard for active connection counting
    struct conn_count_guard {
        std::atomic<std::size_t>& counter;
        conn_count_guard(std::atomic<std::size_t>& c) noexcept : counter(c) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
        ~conn_count_guard() {
            counter.fetch_sub(1, std::memory_order_relaxed);
        }
        conn_count_guard(const conn_count_guard&) = delete;
        auto operator=(const conn_count_guard&) -> conn_count_guard& = delete;
    };

    // =========================================================================
    // Connection Entry Point — Protocol Detection + Dispatch
    // =========================================================================

    /// Handle single connection: TLS handshake (if configured), protocol
    /// detection (ALPN / client preface), dispatch to HTTP/2 or HTTP/1.1.
    auto handle_connection(socket client, io_context& io) -> task<void> {
        conn_count_guard cg(active_connections_);

#ifdef CNETMOD_HAS_SSL
        // --- TLS path ---
        if (ssl_ctx_) {
            ssl_stream ssl(*ssl_ctx_, io, client);
            ssl.set_accept_state();
            auto hr = co_await ssl.async_handshake();
            if (!hr) { client.close(); co_return; }

#ifdef CNETMOD_HAS_NGHTTP2
            // Check ALPN-negotiated protocol
            auto alpn = ssl.get_alpn_selected();
            if (alpn == "h2") {
                // TCP_NODELAY reduces latency for HTTP/2 multiplexed frames
                (void)client.apply_options({.non_blocking = false, .no_delay = true});

                stream_io sio(io, client, ssl);
                http2_session h2(sio, router_, middlewares_, {},
                    [this]() { return date_cache_.get(); });
                co_await h2.run();
                client.close();
                co_return;
            }
#endif
            // HTTP/1.1 over TLS
            co_await handle_h1_tls(client, io, ssl);
            client.close();
            co_return;
        }
#endif

        // --- Cleartext path ---
#ifdef CNETMOD_HAS_NGHTTP2
        // Read initial data and check for HTTP/2 client connection preface
        std::array<std::byte, 8192> peek_buf{};
        auto peek_rd = co_await async_read(io, client,
            mutable_buffer{peek_buf.data(), peek_buf.size()});
        if (!peek_rd || *peek_rd == 0) { client.close(); co_return; }

        if (is_h2_client_preface(peek_buf.data(), *peek_rd)) {
            // TCP_NODELAY reduces latency for HTTP/2 multiplexed frames
            (void)client.apply_options({.non_blocking = false, .no_delay = true});

            // HTTP/2 cleartext (h2c via direct preface)
            stream_io sio(io, client);
            http2_session h2(sio, router_, middlewares_, {},
                [this]() { return date_cache_.get(); });
            co_await h2.run(peek_buf.data(), *peek_rd);
            client.close();
            co_return;
        }

        // HTTP/1.1 cleartext — feed already-read bytes to parser
        co_await handle_h1_clear(client, io,
            reinterpret_cast<const char*>(peek_buf.data()), *peek_rd);
#else
        co_await handle_h1_clear(client, io);
#endif
        client.close();
    }

    // =========================================================================
    // HTTP/1.1 Keep-Alive Loop (cleartext)
    // =========================================================================

    /// @param initial_data  Pre-read data to seed parser (from h2 preface detection)
    /// @param initial_len   Length of initial_data
    auto handle_h1_clear(socket& client, io_context& io,
                         const char* initial_data = nullptr,
                         std::size_t initial_len = 0) -> task<void>
    {
        bool keep_alive = true;

        while (keep_alive) {
            request_parser parser;
            std::array<std::byte, 8192> buf{};

            // Feed pre-read data on first iteration
            if (initial_data && initial_len > 0) {
                auto consumed = parser.consume(initial_data, initial_len);
                if (!consumed) co_return;
                initial_data = nullptr;
                initial_len = 0;
            }

            while (!parser.ready()) {
                auto rd = co_await async_read(io, client,
                    mutable_buffer{buf.data(), buf.size()});
                if (!rd || *rd == 0) co_return;

                auto consumed = parser.consume(
                    reinterpret_cast<const char*>(buf.data()), *rd);
                if (!consumed) co_return;
            }

            // Route matching
            auto uri = parser.uri();
            auto qpos = uri.find('?');
            auto path = (qpos != std::string_view::npos)
                ? uri.substr(0, qpos) : uri;

            auto mr = router_.match(parser.method(), path);

            response resp(status::ok);
            resp.set_header("Server", "cnetmod");
            resp.set_header("Date", date_cache_.get());

            route_params rp;
            handler_fn handler;

            if (mr) {
                handler = std::move(mr->handler);
                rp = std::move(mr->params);
            } else {
                handler = [](request_context& ctx) -> task<void> {
                    return detail::not_found_handler(ctx);
                };
            }

            request_context rctx(io, client, parser, resp, std::move(rp));
            co_await execute_chain(rctx, handler);

            if (resp.get_header("X-Streamed") != "1") {
                auto data = resp.serialize();
                auto wr = co_await async_write(io, client,
                    const_buffer{data.data(), data.size()});
                if (!wr) co_return;
            }

            auto conn_hdr = parser.get_header("Connection");
            if (parser.version() == http_version::http_1_1) {
                keep_alive = (conn_hdr != "close");
            } else {
                keep_alive = false;
            }
        }
    }

#ifdef CNETMOD_HAS_SSL
    // =========================================================================
    // HTTP/1.1 Keep-Alive Loop (TLS)
    // =========================================================================

    auto handle_h1_tls(socket& client, io_context& io,
                       ssl_stream& ssl) -> task<void>
    {
        bool keep_alive = true;

        while (keep_alive) {
            request_parser parser;
            std::array<std::byte, 8192> buf{};

            while (!parser.ready()) {
                auto rd = co_await ssl.async_read(
                    mutable_buffer{buf.data(), buf.size()});
                if (!rd || *rd == 0) co_return;

                auto consumed = parser.consume(
                    reinterpret_cast<const char*>(buf.data()), *rd);
                if (!consumed) co_return;
            }

            auto uri = parser.uri();
            auto qpos = uri.find('?');
            auto path = (qpos != std::string_view::npos)
                ? uri.substr(0, qpos) : uri;

            auto mr = router_.match(parser.method(), path);

            response resp(status::ok);
            resp.set_header("Server", "cnetmod");
            resp.set_header("Date", date_cache_.get());

            route_params rp;
            handler_fn handler;

            if (mr) {
                handler = std::move(mr->handler);
                rp = std::move(mr->params);
            } else {
                handler = [](request_context& ctx) -> task<void> {
                    return detail::not_found_handler(ctx);
                };
            }

            request_context rctx(io, client, parser, resp, std::move(rp));
            co_await execute_chain(rctx, handler);

            if (resp.get_header("X-Streamed") != "1") {
                auto data = resp.serialize();
                auto wr = co_await ssl.async_write(
                    const_buffer{data.data(), data.size()});
                if (!wr) co_return;
            }

            auto conn_hdr = parser.get_header("Connection");
            if (parser.version() == http_version::http_1_1) {
                keep_alive = (conn_hdr != "close");
            } else {
                keep_alive = false;
            }
        }
    }
#endif // CNETMOD_HAS_SSL

    /// Execute middleware chain, finally call handler
    /// Uses index-based recursive coroutine to avoid MSVC ICE with generic lambda + coroutine + std::function
    auto execute_chain(request_context& ctx, handler_fn& handler,
                       std::size_t idx = 0) -> task<void>
    {
        if (idx >= middlewares_.size()) {
            co_await handler(ctx);
            co_return;
        }
        // next is a regular (non-coroutine) closure that directly returns the next layer's task<void>
        next_fn next = [this, &ctx, &handler, idx]() -> task<void> {
            return execute_chain(ctx, handler, idx + 1);
        };
        co_await middlewares_[idx](ctx, next);
    }

    io_context& ctx_;
    server_context* sctx_ = nullptr;  // Non-null for multi-core mode
    std::unique_ptr<tcp::acceptor> acc_;
    router router_;
    std::vector<middleware_fn> middlewares_;
    std::string host_;
    std::uint16_t port_ = 0;
    bool running_ = false;
    std::size_t max_connections_ = 0;   // 0 = unlimited
    std::atomic<std::size_t> active_connections_{0};
    date_cache date_cache_;  // Cached Date header (re-rendered once/second)
#ifdef CNETMOD_HAS_SSL
    ssl_context* ssl_ctx_ = nullptr;   // Non-owning, for TLS support
#endif
};

} // namespace cnetmod::http
