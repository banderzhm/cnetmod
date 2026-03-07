/// cnetmod.protocol.modbus:udp_server — Modbus UDP Server Implementation
/// Full-featured async Modbus UDP server

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:udp_server;

import std;
import :types;
import :data_store;
import :request_handler;
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
// Modbus UDP Server
// =============================================================================

export class udp_server {
public:
    udp_server(io_context& ctx, data_store& store)
        : ctx_(ctx), handler_(store), running_(false) {}

    udp_server(const udp_server&) = delete;
    auto operator=(const udp_server&) -> udp_server& = delete;

    // ── Bind to address and port ──
    auto listen(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        host_ = std::string(host);
        port_ = port;
        
        // Parse IP address
        auto addr_result = ip_address::from_string(host);
        if (!addr_result) {
            co_return addr_result.error();
        }
        
        // Create UDP socket
        auto sock_result = socket::create(
            addr_result->is_v4() ? address_family::ipv4 : address_family::ipv6,
            socket_type::datagram
        );
        if (!sock_result) {
            co_return sock_result.error();
        }
        
        socket_ = std::move(*sock_result);
        
        // Set socket options
        socket_options opts;
        opts.reuse_address = true;
        opts.non_blocking = true;
        if (auto err = socket_.apply_options(opts); !err) {
            co_return err.error();
        }
        
        // Bind
        endpoint ep(*addr_result, port);
        if (auto err = socket_.bind(ep); !err) {
            co_return err.error();
        }
        
        co_return std::error_code{};
    }

    // ── Start receiving requests ──
    auto async_run() -> task<void> {
        running_ = true;
        
        while (running_ && socket_.is_open()) {
            std::vector<std::uint8_t> buffer(512);
            endpoint from_ep;
            
            mutable_buffer recv_buf{reinterpret_cast<std::byte*>(buffer.data()), buffer.size()};
            auto recv_result = co_await async_recvfrom(ctx_, socket_, recv_buf, from_ep);
            if (!recv_result) {
                if (running_) {
                    // Log error but continue
                    co_await async_sleep(ctx_, std::chrono::milliseconds(10));
                }
                continue;
            }

            buffer.resize(*recv_result);
            request_count_.fetch_add(1, std::memory_order_relaxed);

            // Parse request (reuse response parser)
            auto request_result = modbus_response::parse(buffer);
            if (!request_result) {
                continue;
            }

            // Convert to request
            modbus_request request;
            request.header = request_result->header;
            request.func_code = request_result->func_code;
            request.data = std::move(request_result->data);

            // Handle request
            auto response = handler_.handle(request);

            // Send response
            auto response_data = response.serialize();
            const_buffer send_buf{
                reinterpret_cast<const std::byte*>(response_data.data()),
                response_data.size()
            };
            co_await async_sendto(ctx_, socket_, send_buf, from_ep);
        }
    }

    // ── Stop server ──
    void stop() {
        running_ = false;
        socket_.close();
    }

    // ── Get server info ──
    auto is_running() const -> bool { return running_; }
    auto get_host() const -> const std::string& { return host_; }
    auto get_port() const -> std::uint16_t { return port_; }
    auto get_request_count() const -> std::size_t { return request_count_.load(); }

private:
    io_context& ctx_;
    socket socket_;
    request_handler handler_;
    bool running_;
    std::string host_;
    std::uint16_t port_ = 502;
    std::atomic<std::size_t> request_count_{0};
};

} // namespace cnetmod::modbus
