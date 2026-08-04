export module cnetmod.test.quic_echo_server;

import std;
import cnetmod.core;
import cnetmod.protocol.quic;

namespace cnetmod::test {

/**
 * @brief Minimal QUIC echo server for handshake verification.
 *
 * This is a stripped-down implementation designed ONLY for Phase 2
 * handshake testing. It does NOT implement HTTP/3 semantics — just
 * raw QUIC stream echo.
 *
 * Usage:
 * @code
 *   auto server = quic_echo_server(ctx, ssl_ctx, port);
 *   co_await server.start();
 * @endcode
 */
class quic_echo_server {
public:
    quic_echo_server(
        io_context& ctx,
        ssl_context& ssl_ctx,
        std::uint16_t port);

    ~quic_echo_server();

    // Non-copyable, non-movable
    quic_echo_server(const quic_echo_server&) = delete;
    auto operator=(const quic_echo_server&) -> quic_echo_server& = delete;
    quic_echo_server(quic_echo_server&&) = delete;
    auto operator=(quic_echo_server&&) -> quic_echo_server& = delete;

    /**
     * @brief Start listening and accept connections.
     * @return std::expected<bool, std::error_code> true on successful start
     */
    auto start() -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Stop accepting new connections and close all active ones.
     */
    auto stop() -> task<void>;

    /**
     * @brief Check if server is running.
     */
    [[nodiscard]] auto is_running() const noexcept -> bool {
        return running_;
    }

    /**
     * @brief Get the number of active connections.
     */
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t {
        return active_connections_;
    }

private:
    /**
     * @brief Accept loop — listens for new QUIC connections.
     */
    auto accept_loop() -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Handle a single connection.
     * @param conn The QUIC connection
     * @param peer The peer endpoint
     */
    auto handle_connection(
        std::unique_ptr<quic_connection> conn,
        endpoint peer) -> task<void>;

    /**
     * @brief Process incoming stream data and echo it back.
     * @param conn      The QUIC connection
     * @param stream_id The stream ID
     * @param data      The received data
     */
    auto process_stream_data(
        quic_connection& conn,
        std::uint64_t stream_id,
        std::span<const std::byte> data) -> task<void>;

    io_context& ctx_;
    ssl_context& ssl_ctx_;
    std::uint16_t port_;
    std::unique_ptr<udp_socket> listen_socket_;
    bool running_{false};
    std::atomic<std::size_t> active_connections_{0};

    // Connection tracking (for graceful shutdown)
    std::unordered_map<std::uint64_t, std::unique_ptr<quic_connection>> connections_;
    std::mutex connections_mutex_;
};

} // namespace cnetmod::test
