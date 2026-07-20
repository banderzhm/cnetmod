module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.awaitable;

import std;

namespace cnetmod {

// =============================================================================
// I/O awaitable base types
// =============================================================================

/// I/O operation completion callback type
export using io_callback = std::function<void(std::error_code, std::size_t)>;

/// General I/O operation awaitable
/// Suspends coroutine when co_await, resumes when I/O completes
export class io_awaitable {
public:
    using result_type = std::expected<std::size_t, std::error_code>;

    io_awaitable() noexcept = default;

    auto await_ready() const noexcept -> bool;

    void await_suspend(std::coroutine_handle<> caller) noexcept;

    auto await_resume() noexcept -> result_type;

    /// Called by I/O completion notification, resumes coroutine
    void complete(std::error_code ec, std::size_t bytes) noexcept;

    /// Set immediate completion (no suspension needed)
    void set_ready(std::error_code ec, std::size_t bytes) noexcept;

protected:
    std::coroutine_handle<> caller_;
    std::error_code error_;
    std::size_t bytes_transferred_ = 0;
    bool ready_ = false;
};

/// accept operation awaitable (returns native handle instead of byte count)
export class accept_awaitable {
public:
    using result_type = std::expected<int, std::error_code>;  // native fd/SOCKET

    accept_awaitable() noexcept = default;

    auto await_ready() const noexcept -> bool;

    void await_suspend(std::coroutine_handle<> caller) noexcept;

    auto await_resume() noexcept -> result_type;

    /// Called by I/O completion notification
    void complete(std::error_code ec, int fd) noexcept;

protected:
    std::coroutine_handle<> caller_;
    std::error_code error_;
    int accepted_fd_ = -1;
    bool ready_ = false;
};

/// connect operation awaitable (no return value, only error_code)
export class connect_awaitable {
public:
    using result_type = std::expected<void, std::error_code>;

    connect_awaitable() noexcept = default;

    auto await_ready() const noexcept -> bool;

    void await_suspend(std::coroutine_handle<> caller) noexcept;

    auto await_resume() noexcept -> result_type;

    /// Called by I/O completion notification
    void complete(std::error_code ec) noexcept;

protected:
    std::coroutine_handle<> caller_;
    std::error_code error_;
    bool ready_ = false;
};

} // namespace cnetmod
