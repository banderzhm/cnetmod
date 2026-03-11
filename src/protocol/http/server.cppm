module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:server;

import std;
import :types;
import :parser;
import :request;
import :response;
import :router;
import :cookie;
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

export auto guess_mime_type(std::string_view ext) noexcept -> std::string_view;

// =============================================================================
// Static File Serving
// =============================================================================

export struct static_file_options {
    std::filesystem::path root;
    std::string index_file = "index.html";
};

export auto serve_dir(static_file_options opts) -> handler_fn;

// =============================================================================
// File Upload Saving
// =============================================================================

export struct upload_options {
    std::filesystem::path save_dir;
    std::string default_filename = "upload.bin";
    std::size_t max_size = 32 * 1024 * 1024;  // 32MB
};

export auto save_upload(upload_options opts) -> handler_fn;

// =============================================================================
// Date Cache
// =============================================================================

class date_cache {
public:
    [[nodiscard]] auto get() -> std::string;
private:
    std::atomic<std::time_t> cached_time_{0};
    mutable std::mutex mtx_;
    std::string cached_str_;
};

// =============================================================================
// http::server — Advanced HTTP Server
// =============================================================================

export class server {
public:
    /// Single-threaded mode
    explicit server(io_context& ctx);

    /// Multi-core mode
    explicit server(server_context& sctx);

#ifdef CNETMOD_HAS_SSL
    /// Configure TLS
    void set_ssl_context(ssl_context& ssl_ctx);
#endif

    /// Listen on specified address and port
    auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>;

    /// Set router
    void set_router(router r);

    /// Add middleware
    void use(middleware_fn mw);

    /// Set max concurrent connections
    void set_max_connections(std::size_t n);

    /// Get current active connection count
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t;

    /// Run server (accept loop)
    auto run() -> task<void>;

    /// Stop server
    void stop();

private:
    struct conn_count_guard;

    auto handle_connection(socket client, io_context& io) -> task<void>;
    
    auto handle_h1_clear(socket& client, io_context& io,
                         const char* initial_data = nullptr,
                         std::size_t initial_len = 0) -> task<void>;

#ifdef CNETMOD_HAS_SSL
    auto handle_h1_tls(socket& client, io_context& io,
                       ssl_stream& ssl) -> task<void>;
#endif

    auto execute_chain(request_context& ctx, handler_fn& handler,
                       std::size_t idx = 0) -> task<void>;
    
    auto send_chunked_response(io_context& io, socket& client, response& resp)
        -> task<void>;

#ifdef CNETMOD_HAS_SSL
    auto send_chunked_response_tls(io_context& io, ssl_stream& ssl, response& resp)
        -> task<void>;
#endif

    io_context& ctx_;
    server_context* sctx_ = nullptr;
    std::unique_ptr<tcp::acceptor> acc_;
    router router_;
    std::vector<middleware_fn> middlewares_;
    std::string host_;
    std::uint16_t port_ = 0;
    bool running_ = false;
    std::size_t max_connections_ = 0;
    std::atomic<std::size_t> active_connections_{0};
    date_cache date_cache_;
#ifdef CNETMOD_HAS_SSL
    ssl_context* ssl_ctx_ = nullptr;
#endif
};

} // namespace cnetmod::http
