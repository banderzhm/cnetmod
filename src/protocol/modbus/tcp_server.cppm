/// cnetmod.protocol.modbus:tcp_server — Modbus TCP Server Implementation
/// Full-featured async Modbus TCP server

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:tcp_server;

import std;
import :types;
import :data_store;
import :request_handler;
import cnetmod.io.io_context;
import cnetmod.io.tcp_socket;
import cnetmod.io.tcp_acceptor;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

namespace cnetmod::modbus {

// =============================================================================
// Modbus TCP Server
// =============================================================================

export class tcp_server {
public:
    tcp_server(io_context& ctx, data_store& store)
        : ctx_(ctx), acceptor_(ctx), handler_(store), running_(false) {}

    tcp_server(const tcp_server&) = delete;
    auto operator=(const tcp_server&) -> tcp_server& = delete;

    // ── Listen on address and port ──
    auto listen(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        host_ = std::string(host);
        port_ = port;
        co_return co_await acceptor_.listen(host, port);
    }

    // ── Start accepting connections ──
    auto async_run() -> task<void> {
        running_ = true;
        
        while (running_) {
            auto socket_result = co_await acceptor_.accept();
            if (!socket_result) {
                if (running_) {
                    // Log error but continue
                }
                break;
            }
            
            // Spawn client handler
            spawn(ctx_, handle_client(std::move(*socket_result)));
        }
    }

    // ── Stop server ──
    void stop() {
        running_ = false;
        acceptor_.close();
    }

    // ── Get server info ──
    auto is_running() const -> bool { return running_; }
    auto get_host() const -> const std::string& { return host_; }
    auto get_port() const -> std::uint16_t { return port_; }
    auto get_client_count() const -> std::size_t { return client_count_.load(); }

private:
    io_context& ctx_;
    tcp_acceptor acceptor_;
    request_handler handler_;
    bool running_;
    std::string host_;
    std::uint16_t port_ = 502;
    std::atomic<std::size_t> client_count_{0};

    // ── Handle individual client connection ──
    auto handle_client(tcp_socket socket) -> task<void> {
        client_count_.fetch_add(1, std::memory_order_relaxed);
        
        while (socket.is_open() && running_) {
            // Read MBAP header (7 bytes)
            std::vector<std::uint8_t> header_buf(7);
            auto header_result = co_await socket.receive(header_buf);
            if (!header_result || *header_result < 7) {
                break;
            }

            // Parse length from header
            std::uint16_t length = read_uint16_be(header_buf, 4);
            
            // Validate length
            if (length < 2 || length > 256) {
                break;
            }

            // Read PDU
            std::vector<std::uint8_t> full_buf(7 + length - 1);
            std::copy(header_buf.begin(), header_buf.end(), full_buf.begin());
            
            if (length > 1) {
                std::span<std::uint8_t> remaining(full_buf.data() + 7, length - 1);
                auto data_result = co_await socket.receive(remaining);
                if (!data_result || *data_result < remaining.size()) {
                    break;
                }
            }

            // Parse request
            auto request_result = modbus_response::parse(full_buf);
            if (!request_result) {
                break;
            }

            // Convert response to request (reuse parsing logic)
            modbus_request request;
            request.header = request_result->header;
            request.func_code = request_result->func_code;
            request.data = std::move(request_result->data);

            // Handle request
            auto response = handler_.handle(request);

            // Send response
            auto response_data = response.serialize();
            auto send_result = co_await socket.send(response_data);
            if (!send_result) {
                break;
            }
        }
        
        client_count_.fetch_sub(1, std::memory_order_relaxed);
    }
};

} // namespace cnetmod::modbus
