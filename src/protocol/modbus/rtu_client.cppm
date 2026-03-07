/// cnetmod.protocol.modbus:rtu_client — Modbus RTU Client Implementation
/// Full-featured async Modbus RTU (Serial) client

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:rtu_client;

import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;

namespace cnetmod::modbus {

// =============================================================================
// Serial Port Configuration
// =============================================================================

export struct serial_config {
    std::string port_name;           // e.g., "COM1" (Windows) or "/dev/ttyUSB0" (Linux)
    std::uint32_t baudrate = 9600;   // 9600, 19200, 38400, 57600, 115200
    std::uint8_t data_bits = 8;      // 7 or 8
    std::uint8_t stop_bits = 1;      // 1 or 2
    char parity = 'N';               // 'N' (None), 'E' (Even), 'O' (Odd)
    
    // Modbus RTU timing
    std::chrono::microseconds char_timeout = std::chrono::microseconds(1500);  // 1.5 char times
    std::chrono::microseconds frame_delay = std::chrono::microseconds(3500);   // 3.5 char times
};

// =============================================================================
// Serial Port Interface (Platform-specific implementation needed)
// =============================================================================

class serial_port {
public:
    explicit serial_port(io_context& ctx) : ctx_(ctx) {}
    
    virtual ~serial_port() = default;
    
    virtual auto open(const serial_config& config) -> task<std::error_code> = 0;
    virtual auto close() -> void = 0;
    virtual auto is_open() const -> bool = 0;
    
    virtual auto send(std::span<const std::uint8_t> data) 
        -> task<std::expected<std::size_t, std::error_code>> = 0;
    
    virtual auto receive(std::span<std::uint8_t> buffer, 
                        std::chrono::steady_clock::duration timeout)
        -> task<std::expected<std::size_t, std::error_code>> = 0;
    
    virtual auto flush() -> task<void> = 0;

protected:
    io_context& ctx_;
};

// =============================================================================
// Modbus RTU Client
// =============================================================================

export class rtu_client {
public:
    explicit rtu_client(io_context& ctx) 
        : ctx_(ctx), serial_(nullptr) {}

    rtu_client(const rtu_client&) = delete;
    auto operator=(const rtu_client&) -> rtu_client& = delete;

    // ── Open serial port ──
    auto open(const serial_config& config) -> task<std::error_code> {
        config_ = config;
        
        // TODO: Create platform-specific serial port implementation
        // For now, return not_supported
        co_return std::make_error_code(std::errc::not_supported);
        
        // Future implementation:
        // serial_ = create_serial_port(ctx_);
        // co_return co_await serial_->open(config);
    }

    // ── Execute request and receive response ──
    auto execute(const modbus_request& request) 
        -> task<std::expected<modbus_response, std::error_code>> 
    {
        if (!serial_ || !serial_->is_open()) {
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        }

        // Serialize RTU frame: [unit_id][func_code][data][crc16_le]
        auto frame = serialize_rtu_frame(request);
        
        // Flush receive buffer
        co_await serial_->flush();
        
        // Wait for frame delay (3.5 char times)
        co_await async_sleep(ctx_, config_.frame_delay);
        
        // Send request
        auto send_result = co_await serial_->send(frame);
        if (!send_result) {
            co_return std::unexpected(send_result.error());
        }

        // Calculate response timeout
        auto response_timeout = calculate_response_timeout(request);
        
        // Receive response
        std::vector<std::uint8_t> response_buf(256);
        std::size_t total_received = 0;
        auto start_time = std::chrono::steady_clock::now();
        
        while (total_received < response_buf.size()) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= response_timeout) {
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            }
            
            auto remaining_timeout = response_timeout - elapsed;
            std::span<std::uint8_t> buf_span(response_buf.data() + total_received, 
                                            response_buf.size() - total_received);
            
            auto recv_result = co_await serial_->receive(buf_span, remaining_timeout);
            if (!recv_result) {
                if (total_received >= 5) {  // Minimum valid frame
                    break;
                }
                co_return std::unexpected(recv_result.error());
            }
            
            total_received += *recv_result;
            
            // Check if we have a complete frame
            if (total_received >= 5) {
                // Wait for char timeout to ensure frame is complete
                co_await async_sleep(ctx_, config_.char_timeout);
                
                // Try to receive more, if nothing comes, frame is complete
                std::uint8_t dummy;
                auto check_result = co_await serial_->receive(std::span(&dummy, 1), 
                                                             config_.char_timeout);
                if (!check_result || *check_result == 0) {
                    break;
                }
                response_buf[total_received++] = dummy;
            }
        }

        response_buf.resize(total_received);
        
        // Parse RTU frame
        co_return parse_rtu_frame(response_buf);
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
                co_await async_sleep(ctx_, std::chrono::milliseconds(100));
            }
        }
        
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));
    }

    // ── Close serial port ──
    void close() {
        if (serial_) {
            serial_->close();
        }
    }

    // ── Check if open ──
    auto is_open() const -> bool {
        return serial_ && serial_->is_open();
    }

    // ── Get configuration ──
    auto get_config() const -> const serial_config& {
        return config_;
    }

private:
    io_context& ctx_;
    std::unique_ptr<serial_port> serial_;
    serial_config config_;

    // Calculate response timeout based on request size and baudrate
    auto calculate_response_timeout(const modbus_request& request) const 
        -> std::chrono::steady_clock::duration 
    {
        // Estimate: request size + response size + processing time
        // For RTU: ~10 bits per byte (start + 8 data + parity + stop)
        std::size_t estimated_bytes = 256;  // Conservative estimate
        auto bits = estimated_bytes * 10;
        auto transmission_time = std::chrono::microseconds(bits * 1000000 / config_.baudrate);
        auto processing_time = std::chrono::milliseconds(100);
        
        return transmission_time + processing_time + config_.frame_delay * 2;
    }
};

// =============================================================================
// Platform-specific Serial Port Implementations
// =============================================================================

#ifdef _WIN32
// Windows serial port implementation
class windows_serial_port : public serial_port {
public:
    using serial_port::serial_port;
    
    auto open(const serial_config& config) -> task<std::error_code> override {
        // TODO: Implement Windows serial port using CreateFile/ReadFile/WriteFile
        co_return std::make_error_code(std::errc::not_supported);
    }
    
    auto close() -> void override {
        // TODO: Implement
    }
    
    auto is_open() const -> bool override {
        return false;
    }
    
    auto send(std::span<const std::uint8_t> data) 
        -> task<std::expected<std::size_t, std::error_code>> override {
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    
    auto receive(std::span<std::uint8_t> buffer, 
                std::chrono::steady_clock::duration timeout)
        -> task<std::expected<std::size_t, std::error_code>> override {
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    
    auto flush() -> task<void> override {
        co_return;
    }
};
#endif

#ifdef __linux__
// Linux serial port implementation
class linux_serial_port : public serial_port {
public:
    using serial_port::serial_port;
    
    auto open(const serial_config& config) -> task<std::error_code> override {
        // TODO: Implement Linux serial port using termios
        co_return std::make_error_code(std::errc::not_supported);
    }
    
    auto close() -> void override {
        // TODO: Implement
    }
    
    auto is_open() const -> bool override {
        return false;
    }
    
    auto send(std::span<const std::uint8_t> data) 
        -> task<std::expected<std::size_t, std::error_code>> override {
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    
    auto receive(std::span<std::uint8_t> buffer, 
                std::chrono::steady_clock::duration timeout)
        -> task<std::expected<std::size_t, std::error_code>> override {
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    
    auto flush() -> task<void> override {
        co_return;
    }
};
#endif

} // namespace cnetmod::modbus
