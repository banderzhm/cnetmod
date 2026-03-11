module;

export module cnetmod.protocol.socks5:client;

import std;
import :types;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::socks5 {

// =============================================================================
// SOCKS5 Client
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) : ctx_(ctx) {}
    
    /// Connect to SOCKS5 proxy server
    [[nodiscard]] auto connect(std::string_view proxy_host, std::uint16_t proxy_port)
        -> task<std::expected<void, std::error_code>>;
    
    /// Authenticate with username/password
    [[nodiscard]] auto authenticate(std::string_view username, std::string_view password)
        -> task<std::expected<void, std::error_code>>;
    
    /// Connect to target through SOCKS5 proxy
    [[nodiscard]] auto connect_target(std::string_view target_host, std::uint16_t target_port)
        -> task<std::expected<void, std::error_code>>;
    
    /// Get the underlying socket (after successful connection)
    [[nodiscard]] auto& socket() { return sock_; }
    [[nodiscard]] auto& socket() const { return sock_; }
    
    /// Release socket ownership (for transferring to other protocols)
    [[nodiscard]] auto release_socket() -> cnetmod::socket {
        return std::move(sock_);
    }
    
    /// Close connection
    void close() {
        if (sock_.is_open()) {
            sock_.close();
        }
    }

private:
    io_context& ctx_;
    cnetmod::socket sock_;
    auth_method selected_auth_ = auth_method::no_auth;
};

} // namespace cnetmod::socks5
