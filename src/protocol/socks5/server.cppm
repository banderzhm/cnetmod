module;

export module cnetmod.protocol.socks5:server;

import std;
import :types;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

namespace cnetmod::socks5 {

// =============================================================================
// Authentication Handler
// =============================================================================

export using auth_handler = std::function<bool(std::string_view username, std::string_view password)>;

// =============================================================================
// SOCKS5 Server Configuration
// =============================================================================

export struct server_config {
    bool allow_no_auth = true;
    bool allow_username_password = false;
    auth_handler authenticator;
    std::size_t max_connections = 0;  // 0 = unlimited
};

// =============================================================================
// SOCKS5 Server
// =============================================================================

export class server {
public:
    /// Single-threaded mode
    explicit server(io_context& ctx, server_config config = {})
        : ctx_(ctx), config_(std::move(config)) {}
    
    /// Multi-core mode
    explicit server(server_context& sctx, server_config config = {})
        : ctx_(sctx.accept_io()), sctx_(&sctx), config_(std::move(config)) {}
    
    /// Listen on specified address and port
    [[nodiscard]] auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>;
    
    /// Run server (accept loop)
    auto run() -> task<void>;
    
    /// Stop server
    void stop();
    
    /// Get current active connection count
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t {
        return active_connections_.load(std::memory_order_relaxed);
    }

private:
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
    
    auto handle_connection(socket client, io_context& io) -> task<void>;
    auto handle_authentication(socket& client, io_context& io) -> task<std::expected<void, std::error_code>>;
    auto handle_request(socket& client, io_context& io) -> task<std::expected<void, std::error_code>>;
    auto relay_data(socket& client, socket& target, io_context& io) -> task<void>;
    
    io_context& ctx_;
    server_context* sctx_ = nullptr;
    server_config config_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    bool running_ = false;
    std::atomic<std::size_t> active_connections_{0};
};

} // namespace cnetmod::socks5
