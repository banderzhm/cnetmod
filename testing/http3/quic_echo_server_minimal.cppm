export module cnetmod.test.quic_echo_server_minimal;

import std;
import cnetmod.core;
import cnetmod.protocol.quic;

namespace cnetmod::test {

/**
 * @brief Minimal QUIC echo server for Phase 2 testing.
 *
 * Stripped-down version that only handles:
 *   - TLS 1.3 handshake
 *   - Stream data echo (bidirectional)
 *   - Graceful connection closure
 *
 * Does NOT implement HTTP/3 semantics.
 * Intended exclusively for integration testing with aioquic.
 *
 * Usage:
 * @code
 *   auto server = quic_echo_server_minimal(ctx, ssl_ctx, 4433);
 *   co_await server.start();
 *   // ... run tests ...
 *   co_await server.stop();
 * @endcode
 */
class quic_echo_server_minimal {
public:
    quic_echo_server_minimal(
        io_context& ctx,
        ssl_context& ssl_ctx,
        std::uint16_t port);

    ~quic_echo_server_minimal();

    // Non-copyable, non-movable
    quic_echo_server_minimal(const quic_echo_server_minimal&) = delete;
    auto operator=(const quic_echo_server_minimal&) -> quic_echo_server_minimal& = delete;
    quic_echo_server_minimal(quic_echo_server_minimal&&) = delete;
    auto operator=(quic_echo_server_minimal&&) -> quic_echo_server_minimal& = delete;

    /**
     * @brief Start listening for incoming QUIC connections.
     * @return true on success, error_code on failure.
     */
    auto start() -> task<std::expected<bool, std::error_code>>;

    /**
     * @brief Stop accepting new connections and close all active ones.
     */
    auto stop() -> task<void>;

    /**
     * @brief Get the number of currently active connections.
     */
    [[nodiscard]] auto active_connections() const noexcept -> std::size_t;

    /**
     * @brief Check whether the server is currently running.
     */
    [[nodiscard]] auto is_running() const noexcept -> bool;

private:
    /**
     * @brief Main UDP receive loop — dispatches packets to connections.
     */
    auto accept_loop() -> task<void>;

    /**
     * @brief Handle a single QUIC connection lifecycle.
     * @param conn  Ownership-transferred connection pointer
     * @param peer  Remote endpoint address
     */
    auto handle_connection(
        std::unique_ptr<quic_connection> conn,
        endpoint peer) -> task<void>;

    /**
     * @brief Read from a stream and echo all data back.
     * @param conn      The parent QUIC connection
     * @param stream_id The stream to echo on
     */
    auto echo_stream_data(
        quic_connection& conn,
        std::uint64_t stream_id) -> task<void>;

    io_context& ctx_;
    ssl_context& ssl_ctx_;
    std::uint16_t port_;
    std::unique_ptr<udp_socket> listen_socket_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> active_connections_{0};

    // Active connection registry (keyed by local CID hash)
    std::unordered_map<std::uint64_t, std::unique_ptr<quic_connection>> connections_;
    std::mutex connections_mutex_;
};

} // namespace cnetmod::test
