module cnetmod.coro.awaitable;

import std;

namespace cnetmod
{
    auto io_awaitable::await_ready() const noexcept -> bool
    {
        return ready_;
    }

    void io_awaitable::await_suspend(std::coroutine_handle<> caller) noexcept
    {
        caller_ = caller;
    }

    auto io_awaitable::await_resume() noexcept -> result_type
    {
        if (error_) return std::unexpected(error_);
        return bytes_transferred_;
    }

    void io_awaitable::complete(std::error_code error, std::size_t bytes) noexcept
    {
        error_ = error;
        bytes_transferred_ = bytes;
        ready_ = true;
        if (caller_) caller_.resume();
    }

    void io_awaitable::set_ready(std::error_code error, std::size_t bytes) noexcept
    {
        error_ = error;
        bytes_transferred_ = bytes;
        ready_ = true;
    }

    auto accept_awaitable::await_ready() const noexcept -> bool
    {
        return ready_;
    }

    void accept_awaitable::await_suspend(std::coroutine_handle<> caller) noexcept
    {
        caller_ = caller;
    }

    auto accept_awaitable::await_resume() noexcept -> result_type
    {
        if (error_) return std::unexpected(error_);
        return accepted_fd_;
    }

    void accept_awaitable::complete(std::error_code error, int descriptor) noexcept
    {
        error_ = error;
        accepted_fd_ = descriptor;
        ready_ = true;
        if (caller_) caller_.resume();
    }

    auto connect_awaitable::await_ready() const noexcept -> bool
    {
        return ready_;
    }

    void connect_awaitable::await_suspend(std::coroutine_handle<> caller) noexcept
    {
        caller_ = caller;
    }

    auto connect_awaitable::await_resume() noexcept -> result_type
    {
        if (error_) return std::unexpected(error_);
        return {};
    }

    void connect_awaitable::complete(std::error_code error) noexcept
    {
        error_ = error;
        ready_ = true;
        if (caller_) caller_.resume();
    }
} // namespace cnetmod