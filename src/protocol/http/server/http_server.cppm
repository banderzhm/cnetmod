module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:server;

import std;
import cnetmod.protocol.http.semantics;
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
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.wait_group;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

import cnetmod.protocol.http.v2.session;

namespace cnetmod::http {

// =============================================================================
// Static File Serving
// =============================================================================

export struct static_file_options
{
    std::filesystem::path root;
    std::string index_file = "index.html";
    bool serve_precompressed = true;
    /// Files no larger than this are retained in a disk-coherent memory cache.
    /// Set to zero to disable body caching.
    std::size_t cache_max_file_size = 1024U * 1024U;
    /// Cached metadata is revalidated after this interval. A zero interval
    /// checks the mounted directory on every request.
    std::chrono::milliseconds revalidate_interval{500};
};

export auto serve_dir(static_file_options opts) -> handler_fn;

// =============================================================================
// File Upload Saving
// =============================================================================

export struct upload_options
{
    std::filesystem::path save_dir;
    std::string default_filename = "upload.bin";
    std::size_t max_size = 32 * 1024 * 1024; // 32MB
};

export auto save_upload(upload_options opts) -> handler_fn;

export struct response_header_options
{
    bool emit_server = true;
    bool emit_date = true;
};

/// Transfer an HTTP response into the HTTP/2 transport representation.
export auto take_h2_response(response& output) -> v2::server_response;

// =============================================================================
// Date Cache
// =============================================================================

class date_cache
{
public:
    /// Snapshot the RFC 9110 IMF-fixdate into caller-owned stack storage.
    /// Keeping the fixed-width representation out of std::string avoids a
    /// heap allocation for every normal server response.
    [[nodiscard]] auto get() -> std::array<char, 29U>;

private:
    static constexpr std::size_t date_size = 29U;
    static constexpr std::size_t word_count = 4U;

    /// The date is a read-mostly value shared by every server worker.  A
    /// reader must not perform a contended read-modify-write merely to copy
    /// it: that would bounce one cache line between all workers on every
    /// response.  Writers publish four atomic words under this sequence;
    /// refreshes happen at most once per second.
    std::atomic<std::time_t> cached_time_{0};
    std::atomic<std::uint32_t> sequence_{};
    std::atomic_flag refreshing_{};
    alignas(64) std::array<std::atomic<std::uint64_t>, word_count> words_{};
};

// =============================================================================
// http::server — Advanced HTTP Server
// =============================================================================

export class server
{
public:
    /// Single-threaded mode
    explicit server(io_context& ctx);

    /// Multi-core mode
    explicit server(server_context& sctx);
    ~server();

#ifdef CNETMOD_HAS_SSL
    /// Configure TLS
    void set_ssl_context(ssl_context& ssl_ctx);
#endif

    /// Listen on specified address and port
    auto listen(std::string_view host, std::uint16_t port,
        socket_options opts = {.reuse_address = true}, int backlog = 4096)
        -> std::expected<void, std::error_code>;

    /**
     * @brief Returns the endpoint selected by the listening socket.
     *
     * This also exposes the operating-system-assigned port after listen() is
     * called with port zero, which avoids fixed-port races in embedded servers
     * and integration tests.
     *
     * @return The bound endpoint, or bad_descriptor when the server is not
     * listening.
     */
    [[nodiscard]] auto local_endpoint()
        -> std::expected<cnetmod::endpoint, std::error_code>;

    /// Set router
    void set_router(router r);
    /// Optional protocol adapter for complete buffered HTTP/2 requests.
    /// Middleware and streaming requests retain normal HTTP dispatch.
    void set_h2_sync_adapter(v2::sync_server_handler handler,
        v2::buffered_request_filter filter);

    /**
     * @brief Registers a middleware, ignoring empty optional components.
     *
     * Empty components do not occupy a pipeline slot or add request-time work.
     */
    void use(middleware_fn mw);

    /// Set max concurrent connections
    void set_max_connections(std::size_t n);

    /// Configure headers generated by the server before route execution.
    void set_response_header_options(response_header_options options) noexcept;

    /// Get current active connection count
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t;

    /// Run server (accept loop)
    auto run() -> task<void>;

    /**
     * @brief Stops admission and cancels the pending accept operation.
     *
     * Call on the accept event loop. Keep that loop running until run() has
     * completed; active connections have their own completion lifetime.
     */
    void stop();

    /**
     * @brief Interrupts socket I/O for current and queued connections.
     *
     * Call stop() first to end admission. This is a terminal operation for
     * this server instance. Keep worker event loops alive until connections
     * finish; this does not forcibly destroy handlers awaiting non-socket work.
     */
    void abort_connections() noexcept;

private:
    struct conn_count_guard;
    struct connection_registration;
    struct recv_service_state;

    void dispatch_connection(socket client);
    auto run_accept_shard(tcp::acceptor& listener, io_context& owner,
        cancel_token& cancellation, async_wait_group& completed) -> task<void>;
    auto handle_connection(socket client, io_context& io, conn_count_guard ownership) -> task<void>;

    auto handle_h1_clear(socket& client, io_context& io,
        std::unique_ptr<std::array<std::byte, 8192>> initial_buffer,
        std::size_t initial_len,
        cancel_token& cancellation) -> task<void>;

    auto handle_h2(socket& client, io_context& io,
        std::span<const std::byte> initial,
        cancel_token& cancellation, cancel_token& write_cancellation) -> task<void>;

    auto make_h2_streaming_handler(io_context& io, socket& client)
        -> v2::borrowed_streaming_server_handler;
    auto make_h2_sync_handler(io_context& io, socket& client)
        -> v2::sync_server_handler;
    auto make_h2_buffered_request_filter() -> v2::buffered_request_filter;

#ifdef CNETMOD_HAS_SSL
    auto handle_h1_tls(socket& client, io_context& io, ssl_stream& ssl,
        cancel_token& cancellation) -> task<void>;
    auto handle_h2_tls(socket& client, io_context& io, ssl_stream& ssl,
        cancel_token& cancellation, cancel_token& write_cancellation) -> task<void>;
#endif

    auto execute_chain(request_context& ctx, handler_fn& handler,
        std::size_t idx = 0) -> task<void>;

    auto send_chunked_response(io_context& io, socket& client, response& resp,
        cancel_token& cancellation) -> task<void>;
    auto send_body_source(io_context& io, socket& client, response& resp,
        cancel_token& cancellation) -> task<bool>;

#ifdef CNETMOD_HAS_SSL
    auto send_chunked_response_tls(io_context& io, ssl_stream& ssl,
        response& resp, cancel_token& cancellation) -> task<void>;
    auto send_body_source_tls(ssl_stream& ssl, response& resp,
        cancel_token& cancellation) -> task<bool>;
#endif

    io_context& ctx_;
    server_context* sctx_ = nullptr;
    std::unique_ptr<tcp::acceptor> acc_;
    std::vector<std::unique_ptr<tcp::acceptor>> worker_acceptors_;
    std::vector<std::unique_ptr<cancel_token>> worker_accept_cancellations_;
    std::unique_ptr<recv_service_state> recv_services_;
    router router_;
    v2::sync_server_handler h2_sync_adapter_;
    v2::buffered_request_filter h2_sync_filter_;
    std::vector<middleware_fn> middlewares_;
    std::string host_;
    std::uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    cancel_token accept_cancellation_;
    std::size_t max_connections_ = 0;
    std::atomic<std::size_t> active_connections_{0};
    concurrent_containers::atomic_rw_latch connections_latch_;
    connection_registration* connections_{};
    bool connections_aborted_{};
    date_cache date_cache_;
    response_header_options response_headers_;
#ifdef CNETMOD_HAS_SSL
    ssl_context* ssl_ctx_ = nullptr;
#endif
};

} // namespace cnetmod::http
