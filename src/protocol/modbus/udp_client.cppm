/// cnetmod.protocol.modbus:udp_client — Modbus UDP Client Implementation
/// Full-featured async Modbus UDP client

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:udp_client;

import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.protocol.udp;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

namespace cnetmod::modbus {

// =============================================================================
// Modbus UDP Client
// =============================================================================

export class udp_client {
public:
    explicit udp_client(io_context& ctx) 
        : ctx_(ctx), transaction_id_(0) {}

    udp_client(const udp_client&) = delete;
    auto operator=(const udp_client&) -> udp_client& = delete;

    // ── Connect (setup remote endpoint) ──
    auto connect(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        remote_host_ = std::string(host);
        remote_port_ = port;
        
        // Parse remote IP address
        auto addr_result = ip_address::from_string(host);
        if (!addr_result) {
            co_return addr_result.error();
        }
        
        remote_endpoint_ = endpoint(*addr_result, port);
        
        // Create UDP socket
        auto sock_result = socket::create(
            addr_result->is_v4() ? address_family::ipv4 : address_family::ipv6,
            socket_type::datagram
        );
        if (!sock_result) {
            co_return sock_result.error();
        }
        
        socket_ = std::move(*sock_result);
        
        // Set non-blocking
        if (auto err = socket_.set_non_blocking(true); !err) {
            co_return err.error();
        }
        
        // Bind to any local address
        endpoint local_ep(
            addr_result->is_v4() ? ip_address(ipv4_address::any()) : ip_address(ipv6_address::any()),
            0
        );
        if (auto err = socket_.bind(local_ep); !err) {
            socket_.close();
            co_return err.error();
        }
        
        co_return std::error_code{};
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
        const_buffer send_buf{reinterpret_cast<const std::byte*>(data.data()), data.size()};
        auto send_result = co_await async_sendto(ctx_, socket_, send_buf, remote_endpoint_);
        if (!send_result) {
            co_return std::unexpected(send_result.error());
        }

        // Receive response
        std::vector<std::uint8_t> buffer(512);
        endpoint from_ep;
        mutable_buffer recv_buf{reinterpret_cast<std::byte*>(buffer.data()), buffer.size()};
        auto recv_result = co_await async_recvfrom(ctx_, socket_, recv_buf, from_ep);
        if (!recv_result) {
            co_return std::unexpected(recv_result.error());
        }

        // Validate source (optional - UDP can receive from anywhere)
        // For strict Modbus, you might want to check from_ep matches remote_endpoint_

        buffer.resize(*recv_result);
        co_return modbus_response::parse(buffer);
    }

    // ── Execute with retry ──
    auto execute_with_retry(const modbus_request& request, int max_retries = 3)
        -> task<std::expected<modbus_response, std::error_code>>
    {
        for (int i = 0; i < max_retries; ++i) {
            auto result = co_await execute(request);
            if (result) {
                co_return result;
            }
            
            // Wait before retry
            if (i < max_retries - 1) {
                co_await async_sleep(ctx_, std::chrono::milliseconds(100 * (i + 1)));
            }
        }
        
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    }

    // ── Close socket ──
    void close() {
        socket_.close();
    }

    // ── Check if open ──
    auto is_open() const -> bool {
        return socket_.is_open();
    }

    // ── Get next transaction ID ──
    auto next_transaction_id() -> std::uint16_t {
        return transaction_id_++;
    }

    // ── Get connection info ──
    auto get_remote_host() const -> const std::string& { return remote_host_; }
    auto get_remote_port() const -> std::uint16_t { return remote_port_; }

private:
    io_context& ctx_;
    socket socket_;
    endpoint remote_endpoint_;
    std::string remote_host_;
    std::uint16_t remote_port_ = 502;
    std::uint16_t transaction_id_;
};

} // namespace cnetmod::modbus
