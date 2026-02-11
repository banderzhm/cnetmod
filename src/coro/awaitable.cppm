module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.awaitable;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;

namespace cnetmod {

// =============================================================================
// I/O awaitable 基础类型
// =============================================================================

/// I/O 操作完成回调类型
export using io_callback = std::function<void(std::error_code, std::size_t)>;

/// 通用的 I/O 操作 awaitable
/// 当 co_await 时挂起协程，I/O 完成后恢复
export class io_awaitable {
public:
    using result_type = std::expected<std::size_t, std::error_code>;

    io_awaitable() noexcept = default;

    auto await_ready() const noexcept -> bool { return ready_; }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
    }

    auto await_resume() noexcept -> result_type {
        if (error_)
            return std::unexpected(error_);
        return bytes_transferred_;
    }

    /// 由 I/O 完成通知调用，恢复协程
    void complete(std::error_code ec, std::size_t bytes) noexcept {
        error_ = ec;
        bytes_transferred_ = bytes;
        ready_ = true;
        if (caller_)
            caller_.resume();
    }

    /// 设置立即完成（无需挂起）
    void set_ready(std::error_code ec, std::size_t bytes) noexcept {
        error_ = ec;
        bytes_transferred_ = bytes;
        ready_ = true;
    }

protected:
    std::coroutine_handle<> caller_;
    std::error_code error_;
    std::size_t bytes_transferred_ = 0;
    bool ready_ = false;
};

/// accept 操作的 awaitable（返回 native handle 而非字节数）
export class accept_awaitable {
public:
    using result_type = std::expected<int, std::error_code>;  // native fd/SOCKET

    accept_awaitable() noexcept = default;

    auto await_ready() const noexcept -> bool { return ready_; }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
    }

    auto await_resume() noexcept -> result_type {
        if (error_)
            return std::unexpected(error_);
        return accepted_fd_;
    }

    /// 由 I/O 完成通知调用
    void complete(std::error_code ec, int fd) noexcept {
        error_ = ec;
        accepted_fd_ = fd;
        ready_ = true;
        if (caller_)
            caller_.resume();
    }

protected:
    std::coroutine_handle<> caller_;
    std::error_code error_;
    int accepted_fd_ = -1;
    bool ready_ = false;
};

/// connect 操作的 awaitable（无返回值，只有 error_code）
export class connect_awaitable {
public:
    using result_type = std::expected<void, std::error_code>;

    connect_awaitable() noexcept = default;

    auto await_ready() const noexcept -> bool { return ready_; }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
    }

    auto await_resume() noexcept -> result_type {
        if (error_)
            return std::unexpected(error_);
        return {};
    }

    /// 由 I/O 完成通知调用
    void complete(std::error_code ec) noexcept {
        error_ = ec;
        ready_ = true;
        if (caller_)
            caller_.resume();
    }

protected:
    std::coroutine_handle<> caller_;
    std::error_code error_;
    bool ready_ = false;
};

} // namespace cnetmod
