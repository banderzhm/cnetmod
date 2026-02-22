module;

#include <cnetmod/config.hpp>

export module cnetmod.executor.async_op;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.file;
import cnetmod.core.serial_port;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.awaitable;
import cnetmod.coro.cancel;

namespace cnetmod {

// =============================================================================
// Async I/O Operations (Coroutine Version)
// =============================================================================
// Returns task<T>, call with co_await
// These are low-level coroutine interfaces, can be converted to stdexec sender via as_sender()

/// Async accept
/// Usage: auto conn = co_await async_accept(ctx, listener);
export auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>;

/// Async connect
/// Usage: co_await async_connect(ctx, sock, endpoint);
export auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<std::expected<void, std::error_code>>;

/// Async read
/// Usage: auto n = co_await async_read(ctx, sock, buf);
export auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async write
/// Usage: auto n = co_await async_write(ctx, sock, buf);
export auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async recvfrom — Receive UDP datagram and get sender address
/// Usage: auto n = co_await async_recvfrom(ctx, sock, buf, peer);
export auto async_recvfrom(io_context& ctx, socket& sock,
                           mutable_buffer buf, endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async sendto — Send UDP datagram to specified address
/// Usage: auto n = co_await async_sendto(ctx, sock, buf, peer);
export auto async_sendto(io_context& ctx, socket& sock,
                         const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>;

// =============================================================================
// Async File I/O Operations (Coroutine Version)
// =============================================================================

/// Async file read
/// Usage: auto n = co_await async_file_read(ctx, f, buf, offset);
export auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                            std::uint64_t offset = 0)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async file write
/// Usage: auto n = co_await async_file_write(ctx, f, buf, offset);
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                             std::uint64_t offset = 0)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async file flush
/// Usage: co_await async_file_flush(ctx, f);
export auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>;

// =============================================================================
// Async Serial Port I/O Operations (Coroutine Version)
// =============================================================================

/// Async serial port read
/// Usage: auto n = co_await async_serial_read(ctx, port, buf);
export auto async_serial_read(io_context& ctx, serial_port& port,
                              mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async serial port write
/// Usage: auto n = co_await async_serial_write(ctx, port, buf);
export auto async_serial_write(io_context& ctx, serial_port& port,
                               const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

// =============================================================================
// Async Timer Operations
// =============================================================================

/// Async wait for specified duration
/// Usage: co_await async_timer_wait(ctx, std::chrono::milliseconds(500));
export auto async_timer_wait(io_context& ctx,
                             std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>;

// =============================================================================
// Cancellable Version — Accept cancel_token& parameter
// =============================================================================
// Same semantics as above interfaces, but can cancel pending operations via cancel_token::cancel().
// Cancelled operations return errc::operation_aborted.

/// Cancellable async accept
export auto async_accept(io_context& ctx, socket& listener,
                         cancel_token& token)
    -> task<std::expected<socket, std::error_code>>;

/// Cancellable async connect
export auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
                          cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async read
export auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
                       cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async write
export auto async_write(io_context& ctx, socket& sock, const_buffer buf,
                        cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async recvfrom
export auto async_recvfrom(io_context& ctx, socket& sock,
                           mutable_buffer buf, endpoint& peer,
                           cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async sendto
export auto async_sendto(io_context& ctx, socket& sock,
                         const_buffer buf, const endpoint& peer,
                         cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async file read
export auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                            std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async file write
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                             std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async serial port read
export auto async_serial_read(io_context& ctx, serial_port& port,
                              mutable_buffer buf, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async serial port write
export auto async_serial_write(io_context& ctx, serial_port& port,
                               const_buffer buf, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async timer
export auto async_timer_wait(io_context& ctx,
                             std::chrono::steady_clock::duration duration,
                             cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

} // namespace cnetmod
