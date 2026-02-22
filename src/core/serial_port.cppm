module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif

export module cnetmod.core.serial_port;

import std;
import cnetmod.core.error;
import cnetmod.core.file;  // file_handle_t, invalid_file_handle

namespace cnetmod {

// =============================================================================
// Serial port configuration enums
// =============================================================================

export enum class parity : std::uint8_t {
    none,
    odd,
    even,
    mark,
    space,
};

export enum class stop_bits : std::uint8_t {
    one,        // 1 stop bit
    one_half,   // 1.5 stop bits
    two,        // 2 stop bits
};

export enum class flow_control : std::uint8_t {
    none,
    hardware,   // RTS/CTS
    software,   // XON/XOFF
};

// =============================================================================
// Serial port configuration
// =============================================================================

export struct serial_config {
    std::uint32_t  baud_rate     = 9600;
    std::uint8_t   data_bits     = 8;       // 5, 6, 7, 8
    stop_bits      stop          = stop_bits::one;
    parity         par           = parity::none;
    flow_control   flow          = flow_control::none;

    // Read timeout (milliseconds, 0 = no timeout)
    std::uint32_t  read_timeout_ms  = 1000;
    // Write timeout (milliseconds, 0 = no timeout)
    std::uint32_t  write_timeout_ms = 1000;
};

// =============================================================================
// Serial Port class
// =============================================================================

/// Platform-independent serial port handle wrapper (RAII)
/// Supports async I/O (driven by io_context)
export class serial_port {
public:
    serial_port() noexcept = default;
    ~serial_port();

    // Non-copyable
    serial_port(const serial_port&) = delete;
    auto operator=(const serial_port&) -> serial_port& = delete;

    // Movable
    serial_port(serial_port&& other) noexcept;
    auto operator=(serial_port&& other) noexcept -> serial_port&;

    /// Open serial port
    /// Windows: name = "COM3", "COM4", ...
    /// Linux:   name = "/dev/ttyUSB0", "/dev/ttyS0", ...
    [[nodiscard]] static auto open(
        std::string_view name,
        const serial_config& config = {}
    ) -> std::expected<serial_port, std::error_code>;

    /// Close serial port
    void close() noexcept;

    /// Get native handle
    [[nodiscard]] auto native_handle() const noexcept -> file_handle_t {
        return handle_;
    }

    /// Release ownership (without closing)
    [[nodiscard]] auto release() noexcept -> file_handle_t {
        auto h = handle_;
        handle_ = invalid_file_handle;
        return h;
    }

    /// Check if opened
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return handle_ != invalid_file_handle;
    }

    explicit operator bool() const noexcept { return is_open(); }

    /// Get current configuration
    [[nodiscard]] auto config() const noexcept -> const serial_config& {
        return config_;
    }

private:
    explicit serial_port(file_handle_t handle, serial_config cfg) noexcept
        : handle_(handle), config_(std::move(cfg)) {}

    file_handle_t handle_ = invalid_file_handle;
    serial_config config_{};
};

} // namespace cnetmod
