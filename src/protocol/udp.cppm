module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.udp;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;

namespace cnetmod::udp {

// =============================================================================
// UDP Socket
// =============================================================================

/// UDP socket
/// Connectionless datagram communication
export class udp_socket {
public:
    explicit udp_socket(io_context& ctx)
        : ctx_(&ctx) {}

    ~udp_socket() = default;

    // Non-copyable
    udp_socket(const udp_socket&) = delete;
    auto operator=(const udp_socket&) -> udp_socket& = delete;

    // Movable
    udp_socket(udp_socket&&) noexcept = default;
    auto operator=(udp_socket&&) noexcept -> udp_socket& = default;

    /// Open and bind to specified endpoint
    [[nodiscard]] auto open(const endpoint& ep, const socket_options& opts = {})
        -> std::expected<void, std::error_code>;

    /// Open only (no bind, for sending)
    [[nodiscard]] auto open(address_family family = address_family::ipv4)
        -> std::expected<void, std::error_code>;

    /// Close
    void close() noexcept {
        socket_.close();
    }

    /// Check if open
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return socket_.is_open();
    }

    /// Get underlying socket
    [[nodiscard]] auto native_socket() noexcept -> socket& {
        return socket_;
    }

    /// Get associated io_context
    [[nodiscard]] auto context() noexcept -> io_context& {
        return *ctx_;
    }

private:
    io_context* ctx_;
    socket socket_;
};

} // namespace cnetmod::udp
