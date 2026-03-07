/// cnetmod.protocol.modbus:rtu_server — Modbus RTU Server Implementation
/// Full-featured async Modbus RTU (Serial) server

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:rtu_server;

import std;
import :types;
import :data_store;
import :request_handler;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.core.serial_port;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

namespace cnetmod::modbus {

// =============================================================================
// RTU Server Configuration
// =============================================================================

export struct rtu_server_config {
    std::string port_name;           // e.g., "COM1" (Windows) or "/dev/ttyUSB0" (Linux)
    std::uint32_t baudrate = 9600;   // 9600, 19200, 38400, 57600, 115200
    std::uint8_t data_bits = 8;      // 7 or 8
    stop_bits stop = stop_bits::one; // 1 or 2
    parity par = parity::none;       // None, Even, Odd
    std::uint8_t unit_id = 1;        // Server unit ID (slave address)
    
    // Modbus RTU timing
    std::chrono::microseconds char_timeout = std::chrono::microseconds(1500);  // 1.5 char times
    std::chrono::microseconds frame_delay = std::chrono::microseconds(3500);   // 3.5 char times
    
    // Convert to serial_config
    auto to_serial_config() const -> serial_config {
        serial_config cfg;
        cfg.baud_rate = baudrate;
        cfg.data_bits = data_bits;
        cfg.stop = stop;
        cfg.par = par;
        cfg.flow = flow_control::none;  // Modbus RTU doesn't use flow control
        cfg.read_timeout_ms = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(char_timeout).count()
        );
        cfg.write_timeout_ms = 1000;
        return cfg;
    }
};

// =============================================================================
// Modbus RTU Server
// =============================================================================

export class rtu_server {
public:
    rtu_server(io_context& ctx, data_store& store)
        : ctx_(ctx), handler_(store) {}

    rtu_server(const rtu_server&) = delete;
    auto operator=(const rtu_server&) -> rtu_server& = delete;

    // ── Start server ──
    auto start(const rtu_server_config& config) -> task<std::error_code> {
        config_ = config;
        
        // Open serial port
        auto result = serial_port::open(config.port_name, config.to_serial_config());
        if (!result) {
            co_return result.error();
        }
        
        serial_ = std::move(*result);
        running_ = true;
        
        // Start receive loop
        co_await receive_loop();
        
        co_return std::error_code{};
    }

    // ── Stop server ──
    void stop() {
        running_ = false;
        serial_.close();
    }

    // ── Check if running ──
    auto is_running() const -> bool {
        return running_ && serial_.is_open();
    }

    // ── Get configuration ──
    auto get_config() const -> const rtu_server_config& {
        return config_;
    }

private:
    io_context& ctx_;
    request_handler handler_;
    serial_port serial_;
    rtu_server_config config_;
    bool running_ = false;

    // Main receive loop
    auto receive_loop() -> task<void> {
        std::vector<std::uint8_t> buffer(256);
        
        while (running_) {
            // Wait for frame delay before receiving
            co_await async_sleep(ctx_, config_.frame_delay);
            
            // Receive request frame
            std::size_t total_received = 0;
            auto start_time = std::chrono::steady_clock::now();
            
            while (total_received < buffer.size()) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                auto timeout = std::chrono::seconds(5);  // Overall timeout
                
                if (elapsed >= timeout) {
                    break;
                }
                
                auto remaining_timeout = timeout - elapsed;
                std::span<std::uint8_t> buf_span(buffer.data() + total_received, 
                                                buffer.size() - total_received);
                
                // Read with timeout
                auto recv_result = co_await async_read_with_timeout(buf_span, remaining_timeout);
                
                if (!recv_result) {
                    if (total_received >= 5) {  // Minimum valid frame
                        break;
                    }
                    // Timeout or error, continue waiting
                    continue;
                }
                
                total_received += *recv_result;
                
                // Check if we have a complete frame
                if (total_received >= 5) {
                    // Wait for char timeout to ensure frame is complete
                    co_await async_sleep(ctx_, config_.char_timeout);
                    
                    // Try to receive more, if nothing comes, frame is complete
                    std::uint8_t dummy;
                    auto check_result = co_await async_read_with_timeout(
                        std::span(&dummy, 1), 
                        config_.char_timeout
                    );
                    
                    if (!check_result || *check_result == 0) {
                        break;
                    }
                    buffer[total_received++] = dummy;
                }
            }
            
            if (total_received < 5) {
                continue;  // Invalid frame, wait for next
            }
            
            buffer.resize(total_received);
            
            // Parse request (convert from response parse result)
            auto parse_result = parse_rtu_frame(buffer);
            if (!parse_result) {
                buffer.resize(256);
                continue;  // Invalid frame, wait for next
            }
            
            // Convert parsed response to request
            modbus_request request;
            request.header = parse_result->header;
            request.func_code = parse_result->func_code;
            request.data = std::move(parse_result->data);
            
            // Check if this request is for us
            if (request.header.unit_id != config_.unit_id && request.header.unit_id != 0) {
                buffer.resize(256);
                continue;  // Not for us, ignore
            }
            
            // Handle request
            auto response = handler_.handle(request);
            
            // Send response (only if not broadcast)
            if (request.header.unit_id != 0) {
                // Serialize response to RTU frame
                auto response_frame = serialize_rtu_frame(request);
                
                // Wait for frame delay before sending
                co_await async_sleep(ctx_, config_.frame_delay);
                
                auto send_result = co_await async_write_serial(response_frame);
                // Ignore send errors, continue serving
            }
            
            buffer.resize(256);
        }
    }

    // Async write wrapper for serial port (uses io_context scheduler)
    auto async_write_serial(std::span<const std::uint8_t> data)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        const_buffer buf{reinterpret_cast<const std::byte*>(data.data()), data.size()};
        co_return co_await async_serial_write(ctx_, serial_, buf);
    }
    
    // Async read with timeout wrapper for serial port (uses io_context scheduler)
    auto async_read_with_timeout(std::span<std::uint8_t> buffer,
                                 std::chrono::steady_clock::duration timeout)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        // Create cancellation token for timeout
        cancel_token token;
        
        // Start timeout timer
        auto timeout_task = [](io_context& ctx, std::chrono::steady_clock::duration dur, 
                              cancel_token& tok) -> task<void> {
            co_await async_timer_wait(ctx, dur);
            tok.cancel();
        }(ctx_, timeout, token);
        
        // Start read operation with cancellation support
        mutable_buffer buf{reinterpret_cast<std::byte*>(buffer.data()), buffer.size()};
        auto result = co_await async_serial_read(ctx_, serial_, buf, token);
        
        // Cancel timeout if read completed first
        token.cancel();
        
        co_return result;
    }
};

} // namespace cnetmod::modbus
