module;

#include <cnetmod/config.hpp>

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

export class server {
public:
    /// Single-threaded mode: all connections handled on the same io_context
    explicit server(io_context& ctx)
        : ctx_(ctx) {}

    /// Multi-core mode: accept on sctx.accept_io(), connections dispatched to worker io_context
    explicit server(server_context& sctx)
        : ctx_(sctx.accept_io()), sctx_(&sctx) {}

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

    /// Run server (accept loop, coroutine)
    auto run() -> task<void> {
        running_ = true;
        while (running_) {
            auto r = co_await async_accept(ctx_, acc_->native_socket());
            if (!r) {
                if (!running_) break;
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
    /// Handle single connection (supports keep-alive)
    /// @param io The io_context bound to this connection (may be worker)
    auto handle_connection(socket client, io_context& io) -> task<void> {
        bool keep_alive = true;

        while (keep_alive) {
            // Parse request
            request_parser parser;
            std::array<std::byte, 8192> buf{};

            while (!parser.ready()) {
                auto rd = co_await async_read(io, client, mutable_buffer{buf.data(), buf.size()});
                if (!rd || *rd == 0) { client.close(); co_return; }

                auto consumed = parser.consume(
                    reinterpret_cast<const char*>(buf.data()), *rd);
                if (!consumed) { client.close(); co_return; }
            }

            // Extract path (remove query string)
            auto uri = parser.uri();
            auto qpos = uri.find('?');
            auto path = (qpos != std::string_view::npos)
                ? uri.substr(0, qpos) : uri;

            // Route matching
            auto mr = router_.match(parser.method(), path);

            response resp(status::ok);
            resp.set_header("Server", "cnetmod");

            route_params rp;
            handler_fn handler;

            if (mr) {
                handler = std::move(mr->handler);
                rp = std::move(mr->params);
            } else {
                // 404 — Non-coroutine lambda, delegates to detail::not_found_handler
                handler = [](request_context& ctx) -> task<void> {
                    return detail::not_found_handler(ctx);
                };
            }

            request_context rctx(io, client, parser, resp, std::move(rp));

            // Execute middleware chain + handler
            co_await execute_chain(rctx, handler);

            // Check if already streamed (X-Streamed marker)
            if (resp.get_header("X-Streamed") != "1") {
                // Normal response sending
                auto data = resp.serialize();
                auto wr = co_await async_write(io, client,
                    const_buffer{data.data(), data.size()});
                if (!wr) { client.close(); co_return; }
            }

            // Check keep-alive
            auto conn_hdr = parser.get_header("Connection");
            if (parser.version() == http_version::http_1_1) {
                keep_alive = (conn_hdr != "close");
            } else {
                keep_alive = false;
            }
        }

        client.close();
    }

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
};

} // namespace cnetmod::http
