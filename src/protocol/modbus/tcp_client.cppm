/// cnetmod.protocol.modbus:tcp_client — Modbus TCP Client Implementation
/// Full-featured async Modbus TCP client

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:tcp_client;

import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

namespace cnetmod::modbus {

// =============================================================================
// Modbus TCP Client
// =============================================================================

export class tcp_client {
public:
    explicit tcp_client(io_context& ctx) 
        : ctx_(ctx), transaction_id_(0) {}

    tcp_client(const tcp_client&) = delete;
    auto operator=(const tcp_client&) -> tcp_client& = delete;

    // ── Connect to Modbus TCP server ──
    auto connect(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        host_ = std::string(host);
        port_ = port;
        
        auto connect_r = co_await async_connect_happy_eyeballs(ctx_, host, port);
        if (!connect_r) {
            co_return connect_r.error();
        }
        socket_ = std::move(connect_r->sock);
        
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
        auto send_result = co_await async_write_all(ctx_, socket_, send_buf);
        if (!send_result) {
            co_return std::unexpected(send_result.error());
        }

        // Receive MBAP header first (7 bytes)
        std::vector<std::uint8_t> header_buf(7);
        mutable_buffer header_mbuf{reinterpret_cast<std::byte*>(header_buf.data()), 7};
        auto header_result = co_await read_exact(header_mbuf);
        if (!header_result) {
            co_return std::unexpected(header_result.error());
        }

        // Parse length from header
        std::uint16_t length = read_uint16_be(header_buf, 4);
        
        // Validate length
        if (length < 2 || length > 256) {
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        // Receive remaining data (length - 1 because unit_id is already in header)
        std::vector<std::uint8_t> full_buf(7 + length - 1);
        std::copy(header_buf.begin(), header_buf.end(), full_buf.begin());
        
        if (length > 1) {
            mutable_buffer data_mbuf{
                reinterpret_cast<std::byte*>(full_buf.data() + 7), 
                static_cast<std::size_t>(length - 1)
            };
            auto data_result = co_await read_exact(data_mbuf);
            if (!data_result) {
                co_return std::unexpected(data_result.error());
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
        cancel_token token;
        co_return co_await with_timeout(
            ctx_, timeout, execute_with_cancel(request, token), token);
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
    socket socket_;
    std::string host_;
    std::uint16_t port_ = 502;
    std::uint16_t transaction_id_;

    auto read_exact(mutable_buffer buf)
        -> task<std::expected<void, std::error_code>>
    {
        auto* data = static_cast<std::byte*>(buf.data);
        std::size_t got = 0;
        while (got < buf.size) {
            auto r = co_await async_read(ctx_, socket_,
                mutable_buffer{data + got, buf.size - got});
            if (!r) {
                co_return std::unexpected(r.error());
            }
            if (*r == 0) {
                co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
            }
            got += *r;
        }
        co_return {};
    }

    auto read_exact(mutable_buffer buf, cancel_token& token)
        -> task<std::expected<void, std::error_code>>
    {
        auto* data = static_cast<std::byte*>(buf.data);
        std::size_t got = 0;
        while (got < buf.size) {
            auto r = co_await async_read(ctx_, socket_,
                mutable_buffer{data + got, buf.size - got}, token);
            if (!r) {
                co_return std::unexpected(r.error());
            }
            if (*r == 0) {
                co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
            }
            got += *r;
        }
        co_return {};
    }

    // ── Execute with cancellation support ──
    auto execute_with_cancel(const modbus_request& request, cancel_token& token) 
        -> task<std::expected<modbus_response, std::error_code>> 
    {
        if (!socket_.is_open()) {
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        }

        // Serialize request
        auto data = request.serialize();
        
        // Send request
        const_buffer send_buf{reinterpret_cast<const std::byte*>(data.data()), data.size()};
        auto send_result = co_await async_write_all(ctx_, socket_, send_buf, token);
        if (!send_result) {
            co_return std::unexpected(send_result.error());
        }

        // Receive MBAP header first (7 bytes)
        std::vector<std::uint8_t> header_buf(7);
        mutable_buffer header_mbuf{reinterpret_cast<std::byte*>(header_buf.data()), 7};
        auto header_result = co_await read_exact(header_mbuf, token);
        if (!header_result) {
            co_return std::unexpected(header_result.error());
        }

        // Parse length from header
        std::uint16_t length = read_uint16_be(header_buf, 4);
        
        // Validate length
        if (length < 2 || length > 256) {
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        // Receive remaining data (length - 1 because unit_id is already in header)
        std::vector<std::uint8_t> full_buf(7 + length - 1);
        std::copy(header_buf.begin(), header_buf.end(), full_buf.begin());
        
        if (length > 1) {
            mutable_buffer data_mbuf{
                reinterpret_cast<std::byte*>(full_buf.data() + 7), 
                static_cast<std::size_t>(length - 1)
            };
            auto data_result = co_await read_exact(data_mbuf, token);
            if (!data_result) {
                co_return std::unexpected(data_result.error());
            }
        }

        // Parse response
        co_return modbus_response::parse(full_buf);
    }
};

} // namespace cnetmod::modbus
