module;

#include <cnetmod/config.hpp>
#include <ctime>

module cnetmod.protocol.http;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.file;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

#ifdef CNETMOD_HAS_NGHTTP2
// HTTP/2 相关导入在 server.cppm 中已处理
#endif

namespace cnetmod::http {

// =============================================================================
// MIME Type Inference Implementation
// =============================================================================

auto guess_mime_type(std::string_view ext) noexcept -> std::string_view {
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
// Static File Serving Implementation
// =============================================================================

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

auto serve_dir(static_file_options opts) -> handler_fn {
    auto opts_ptr = std::make_shared<static_file_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void> {
        return detail::serve_dir_body(*opts_ptr, ctx);
    };
}

// =============================================================================
// File Upload Implementation
// =============================================================================

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

    // Raw body mode
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

auto not_found_handler(request_context& ctx) -> task<void> {
    ctx.not_found();
    co_return;
}

} // namespace detail

auto save_upload(upload_options opts) -> handler_fn {
    auto opts_ptr = std::make_shared<upload_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void> {
        return detail::save_upload_body(*opts_ptr, ctx);
    };
}

// =============================================================================
// Date Cache Implementation
// =============================================================================

auto date_cache::get() -> std::string {
    auto now = std::time(nullptr);
    auto cached = cached_time_.load(std::memory_order_relaxed);
    if (now != cached) {
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

// =============================================================================
// Server Implementation
// =============================================================================

struct server::conn_count_guard {
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

server::server(io_context& ctx)
    : ctx_(ctx) {}

server::server(server_context& sctx)
    : ctx_(sctx.accept_io()), sctx_(&sctx) {}

#ifdef CNETMOD_HAS_SSL
void server::set_ssl_context(ssl_context& ssl_ctx) {
    ssl_ctx_ = &ssl_ctx;
}
#endif

auto server::listen(std::string_view host, std::uint16_t port)
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

void server::set_router(router r) { 
    router_ = std::move(r); 
}

void server::use(middleware_fn mw) { 
    middlewares_.push_back(std::move(mw)); 
}

void server::set_max_connections(std::size_t n) { 
    max_connections_ = n; 
}

auto server::active_connections() const noexcept -> std::size_t {
    return active_connections_.load(std::memory_order_relaxed);
}

void server::stop() {
    running_ = false;
    if (acc_) acc_->close();
}

// =============================================================================
// Server Run Method
// =============================================================================

auto server::run() -> task<void> {
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

// =============================================================================
// Connection Handling
// =============================================================================

auto server::handle_connection(socket client, io_context& io) -> task<void> {
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

// =============================================================================
// HTTP/1.1 Cleartext Handler
// =============================================================================

auto server::handle_h1_clear(socket& client, io_context& io,
                              const char* initial_data,
                              std::size_t initial_len) -> task<void>
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

        // Check if chunked encoding is needed
        bool use_chunked = false;
        if (resp.get_header("X-Streamed") != "1") {
            // If no Content-Length and body not empty, use chunked
            if (resp.get_header("Content-Length").empty() && 
                !resp.body().empty() &&
                parser.version() == http_version::http_1_1) {
                use_chunked = true;
                resp.set_header("Transfer-Encoding", "chunked");
            }
            
            if (use_chunked) {
                // Send chunked response
                co_await send_chunked_response(io, client, resp);
            } else {
                // Send normal response
                auto data = resp.serialize();
                auto wr = co_await async_write(io, client,
                    const_buffer{data.data(), data.size()});
                if (!wr) co_return;
            }
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
// =============================================================================
// HTTP/1.1 TLS Handler
// =============================================================================

auto server::handle_h1_tls(socket& client, io_context& io,
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

        // Check if chunked encoding is needed
        bool use_chunked = false;
        if (resp.get_header("X-Streamed") != "1") {
            // If no Content-Length and body not empty, use chunked
            if (resp.get_header("Content-Length").empty() && 
                !resp.body().empty() &&
                parser.version() == http_version::http_1_1) {
                use_chunked = true;
                resp.set_header("Transfer-Encoding", "chunked");
            }
            
            if (use_chunked) {
                // Send chunked response (TLS)
                co_await send_chunked_response_tls(io, ssl, resp);
            } else {
                // Send normal response
                auto data = resp.serialize();
                auto wr = co_await ssl.async_write(
                    const_buffer{data.data(), data.size()});
                if (!wr) co_return;
            }
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

// =============================================================================
// Middleware Chain Execution
// =============================================================================

auto server::execute_chain(request_context& ctx, handler_fn& handler,
                            std::size_t idx) -> task<void>
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

// =============================================================================
// Chunked Response Sending
// =============================================================================

auto server::send_chunked_response(io_context& io, socket& client, response& resp)
    -> task<void>
{
    // Send headers (without body)
    auto body = std::move(resp.body());
    resp.set_body(std::string{});  // Clear body
    
    auto header_data = resp.serialize();
    auto wr = co_await async_write(io, client,
        const_buffer{header_data.data(), header_data.size()});
    if (!wr) co_return;
    
    // Send chunked body
    constexpr std::size_t CHUNK_SIZE = 8192;
    std::size_t offset = 0;
    
    while (offset < body.size()) {
        auto chunk_size = std::min(CHUNK_SIZE, body.size() - offset);
        
        // Send chunk size (hex)
        auto size_str = std::format("{:x}\r\n", chunk_size);
        auto wr1 = co_await async_write(io, client,
            const_buffer{size_str.data(), size_str.size()});
        if (!wr1) co_return;
        
        // Send chunk data
        auto wr2 = co_await async_write(io, client,
            const_buffer{body.data() + offset, chunk_size});
        if (!wr2) co_return;
        
        // Send chunk ending \r\n
        auto wr3 = co_await async_write(io, client,
            const_buffer{"\r\n", 2});
        if (!wr3) co_return;
        
        offset += chunk_size;
    }
    
    // Send end chunk (0\r\n\r\n)
    auto wr_end = co_await async_write(io, client,
        const_buffer{"0\r\n\r\n", 5});
    (void)wr_end;
}

#ifdef CNETMOD_HAS_SSL
auto server::send_chunked_response_tls(io_context& io, ssl_stream& ssl, response& resp)
    -> task<void>
{
    // Send headers (without body)
    auto body = std::move(resp.body());
    resp.set_body(std::string{});  // Clear body
    
    auto header_data = resp.serialize();
    auto wr = co_await ssl.async_write(
        const_buffer{header_data.data(), header_data.size()});
    if (!wr) co_return;
    
    // Send chunked body
    constexpr std::size_t CHUNK_SIZE = 8192;
    std::size_t offset = 0;
    
    while (offset < body.size()) {
        auto chunk_size = std::min(CHUNK_SIZE, body.size() - offset);
        
        // Send chunk size (hex)
        auto size_str = std::format("{:x}\r\n", chunk_size);
        auto wr1 = co_await ssl.async_write(
            const_buffer{size_str.data(), size_str.size()});
        if (!wr1) co_return;
        
        // Send chunk data
        auto wr2 = co_await ssl.async_write(
            const_buffer{body.data() + offset, chunk_size});
        if (!wr2) co_return;
        
        // Send chunk ending \r\n
        auto wr3 = co_await ssl.async_write(
            const_buffer{"\r\n", 2});
        if (!wr3) co_return;
        
        offset += chunk_size;
    }
    
    // Send end chunk (0\r\n\r\n)
    auto wr_end = co_await ssl.async_write(
        const_buffer{"0\r\n\r\n", 5});
    (void)wr_end;
}
#endif

} // namespace cnetmod::http
