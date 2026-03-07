/// cnetmod.protocol.modbus:tcp_client — Modbus TCP Client Implementation
/// Full-featured async Modbus TCP client

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:tcp_client;

import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.io.tcp_socket;
import cnetmod.coro.task;

namespace cnetmod::modbus {

// =============================================================================
// Modbus TCP Client
// =============================================================================

export class tcp_client {
public:
    explicit tcp_client(io_context& ctx) 
        : ctx_(ctx), socket_(ctx), transaction_id_(0) {}

    tcp_client(const tcp_client&) = delete;
    auto operator=(const tcp_client&) -> tcp_client& = delete;

    // ── Connect to Modbus TCP server ──
    auto connect(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        host_ = std::string(host);
        port_ = port;
        co_return co_await socket_.connect(host, port);
    }

    // ── Execute request and receive response ──
    auto execute(const modbus_request& request) 
        -> task<std::expected<modbus_response, std::error_code>> 
    {
        if (!socket_.is_open()) {
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        }

        // Serialize request
        auto data = request.serialize();
        
        // Send request
        auto send_result = co_await socket_.send(data);
        if (!send_result) {
            co_return std::unexpected(send_result.error());
        }

        // Receive MBAP header first (7 bytes)
        std::vector<std::uint8_t> header_buf(7);
        auto header_result = co_await socket_.receive(header_buf);
        if (!header_result || *header_result < 7) {
            co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
        }

        // Parse length from header
        std::uint16_t length = read_uint16_be(header_buf, 4);
        
        // Validate length
        if (length < 2 || length > 256) {
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        // Receive remaining data
        std::vector<std::uint8_t> full_buf(7 + length - 1);
        std::copy(header_buf.begin(), header_buf.end(), full_buf.begin());
        
        if (length > 1) {
            std::span<std::uint8_t> remaining(full_buf.data() + 7, length - 1);
            auto data_result = co_await socket_.receive(remaining);
            if (!data_result || *data_result < remaining.size()) {
                co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
            }
        }

        // Parse response
        co_return modbus_response::parse(full_buf);
    }

    // ── Execute with timeout ──
    auto execute_with_timeout(const modbus_request& request, 
                             std::chrono::steady_clock::duration timeout)
        -> task<std::expected<modbus_response, std::error_code>>
    {
        // TODO: Implement timeout mechanism
        co_return co_await execute(request);
    }

    // ── Reconnect ──
    auto reconnect() -> task<std::error_code> {
        close();
        co_return co_await connect(host_, port_);
    }

    // ── Close connection ──
    void close() {
        socket_.close();
    }

    // ── Check if connected ──
    auto is_open() const -> bool {
        return socket_.is_open();
    }

    // ── Get next transaction ID ──
    auto next_transaction_id() -> std::uint16_t {
        return transaction_id_++;
    }

    // ── Get connection info ──
    auto get_host() const -> const std::string& { return host_; }
    auto get_port() const -> std::uint16_t { return port_; }

private:
    io_context& ctx_;
    tcp_socket socket_;
    std::string host_;
    std::uint16_t port_ = 502;
    std::uint16_t transaction_id_;
};

} // namespace cnetmod::modbus
