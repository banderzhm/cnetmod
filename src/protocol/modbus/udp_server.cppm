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
import cnetmod.io.udp_socket;
import cnetmod.coro.task;

namespace cnetmod::modbus {

// =============================================================================
// Modbus UDP Server
// =============================================================================

export class udp_server {
public:
    udp_server(io_context& ctx, data_store& store)
        : ctx_(ctx), socket_(ctx), handler_(store), running_(false) {}

    udp_server(const udp_server&) = delete;
    auto operator=(const udp_server&) -> udp_server& = delete;

    // ── Bind to address and port ──
    auto listen(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        host_ = std::string(host);
        port_ = port;
        co_return co_await socket_.bind(host, port);
    }

    // ── Start receiving requests ──
    auto async_run() -> task<void> {
        running_ = true;
        
        while (running_ && socket_.is_open()) {
            std::vector<std::uint8_t> buffer(512);
            std::string from_host;
            std::uint16_t from_port;
            
            auto recv_result = co_await socket_.receive_from(buffer, from_host, from_port);
            if (!recv_result) {
                if (running_) {
                    // Log error but continue
                }
                continue;
            }

            buffer.resize(*recv_result);
            request_count_.fetch_add(1, std::memory_order_relaxed);

            // Parse request
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
            co_await socket_.send_to(response_data, from_host, from_port);
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
    udp_socket socket_;
    request_handler handler_;
    bool running_;
    std::string host_;
    std::uint16_t port_ = 502;
    std::atomic<std::size_t> request_count_{0};
};

} // namespace cnetmod::modbus
