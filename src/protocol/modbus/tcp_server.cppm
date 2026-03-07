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
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

namespace cnetmod::modbus {

// =============================================================================
// Modbus TCP Server
// =============================================================================

export class tcp_server {
public:
    tcp_server(io_context& ctx, data_store& store)
        : ctx_(ctx), handler_(store), running_(false) {}

    tcp_server(const tcp_server&) = delete;
    auto operator=(const tcp_server&) -> tcp_server& = delete;

    // ── Listen on address and port ──
    auto listen(std::string_view host, std::uint16_t port) -> task<std::error_code> {
        host_ = std::string(host);
        port_ = port;
        
        // Parse IP address
        auto addr_result = ip_address::from_string(host);
        if (!addr_result) {
            co_return addr_result.error();
        }
        
        // Create TCP socket
        auto sock_result = socket::create(
            addr_result->is_v4() ? address_family::ipv4 : address_family::ipv6,
            socket_type::stream
        );
        if (!sock_result) {
            co_return sock_result.error();
        }
        
        acceptor_ = std::move(*sock_result);
        
        // Set socket options
        socket_options opts;
        opts.reuse_address = true;
        opts.non_blocking = true;
        if (auto err = acceptor_.apply_options(opts); !err) {
            co_return err.error();
        }
        
        // Bind
        endpoint ep(*addr_result, port);
        if (auto err = acceptor_.bind(ep); !err) {
            co_return err.error();
        }
        
        // Listen
        if (auto err = acceptor_.listen(128); !err) {
            co_return err.error();
        }
        
        co_return std::error_code{};
    }

    // ── Start accepting connections ──
    auto async_run() -> task<void> {
        running_ = true;
        
        while (running_) {
            auto socket_result = co_await async_accept(ctx_, acceptor_);
            if (!socket_result) {
                if (running_) {
                    // Log error but continue
                    co_await async_sleep(ctx_, std::chrono::milliseconds(100));
                }
                continue;
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
    socket acceptor_;
    request_handler handler_;
    bool running_;
    std::string host_;
    std::uint16_t port_ = 502;
    std::atomic<std::size_t> client_count_{0};

    // ── Handle individual client connection ──
    auto handle_client(socket client_socket) -> task<void> {
        client_count_.fetch_add(1, std::memory_order_relaxed);
        
        while (client_socket.is_open() && running_) {
            // Read MBAP header (7 bytes)
            std::vector<std::uint8_t> header_buf(7);
            mutable_buffer header_mbuf{reinterpret_cast<std::byte*>(header_buf.data()), 7};
            auto header_result = co_await async_read(ctx_, client_socket, header_mbuf);
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
                mutable_buffer data_mbuf{
                    reinterpret_cast<std::byte*>(full_buf.data() + 7),
                    static_cast<std::size_t>(length - 1)
                };
                auto data_result = co_await async_read(ctx_, client_socket, data_mbuf);
                if (!data_result || *data_result < static_cast<std::size_t>(length - 1)) {
                    break;
                }
            }

            // Parse request (reuse response parser)
            auto request_result = modbus_response::parse(full_buf);
            if (!request_result) {
                break;
            }

            // Convert response to request
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
            auto send_result = co_await async_write(ctx_, client_socket, send_buf);
            if (!send_result) {
                break;
            }
        }
        
        client_socket.close();
        client_count_.fetch_sub(1, std::memory_order_relaxed);
    }
};

} // namespace cnetmod::modbus
