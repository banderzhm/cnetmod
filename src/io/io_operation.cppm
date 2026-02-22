module;

#include <cnetmod/config.hpp>

export module cnetmod.io.io_operation;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;

namespace cnetmod {

// =============================================================================
// I/O Operation Type
// =============================================================================

/// I/O operation type
export enum class io_op_type {
    // Network operations
    accept,
    connect,
    read,
    write,
    close,

    // File operations
    file_read,
    file_write,
    file_flush,
};

// =============================================================================
// I/O Operation Result
// =============================================================================

/// I/O operation completion result
export struct io_result {
    std::error_code error;          // Error code
    std::size_t bytes_transferred;  // Number of bytes transferred

    [[nodiscard]] auto success() const noexcept -> bool {
        return !error;
    }

    explicit operator bool() const noexcept {
        return success();
    }
};

// =============================================================================
// I/O Operation Base Class
// =============================================================================

/// Async I/O operation abstract base class
/// Platform-specific implementations need to inherit from this class
export class io_operation {
public:
    virtual ~io_operation() = default;

    /// Get operation type
    [[nodiscard]] virtual auto type() const noexcept -> io_op_type = 0;

    /// Execute operation (called by io_context)
    virtual void execute() = 0;

    /// Complete operation (called by io_context when operation completes)
    /// @param result Operation result
    virtual void complete(io_result result) = 0;

protected:
    io_operation() = default;
};

} // namespace cnetmod
