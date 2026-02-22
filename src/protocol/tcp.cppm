module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.tcp;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;

namespace cnetmod::tcp {

// =============================================================================
// TCP Acceptor
// =============================================================================

/// TCP connection acceptor
/// Used to listen on a port and accept incoming connections
export class acceptor {
public:
    explicit acceptor(io_context& ctx)
        : ctx_(&ctx) {}

    ~acceptor() = default;

    // Non-copyable
    acceptor(const acceptor&) = delete;
    auto operator=(const acceptor&) -> acceptor& = delete;

    // Movable
    acceptor(acceptor&&) noexcept = default;
    auto operator=(acceptor&&) noexcept -> acceptor& = default;

    /// Open and bind to specified endpoint
    [[nodiscard]] auto open(const endpoint& ep, const socket_options& opts = {})
        -> std::expected<void, std::error_code>;

    /// Close acceptor
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

// =============================================================================
// TCP Connection
// =============================================================================

/// TCP connection
/// Represents an established TCP connection
export class connection {
public:
    explicit connection(io_context& ctx)
        : ctx_(&ctx) {}

    connection(io_context& ctx, socket sock)
        : ctx_(&ctx), socket_(std::move(sock)) {}

    ~connection() = default;

    // Non-copyable
    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;

    // Movable
    connection(connection&&) noexcept = default;
    auto operator=(connection&&) noexcept -> connection& = default;

    /// Get remote endpoint
    [[nodiscard]] auto remote_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// Get local endpoint
    [[nodiscard]] auto local_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// Close connection
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

} // namespace cnetmod::tcp
