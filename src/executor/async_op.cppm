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
// Async Network I/O Operations (Coroutine Version)
// =============================================================================
// Returns task<T>, call with co_await
// These are low-level coroutine interfaces, can be converted to stdexec sender via as_sender()

/// Async accept
/// Usage: auto conn = co_await async_accept(ctx, listener);
export auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>;

/// Cancellable async accept
export auto async_accept(io_context& ctx, socket& listener,
                         cancel_token& token)
    -> task<std::expected<socket, std::error_code>>;

/// Async connect
/// Usage: co_await async_connect(ctx, sock, endpoint);
export auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async connect
export auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
                          cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

/// Async read
/// Usage: auto n = co_await async_read(ctx, sock, buf);
export auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async read
export auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
                       cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

namespace detail {

inline auto find_delimiter(const_buffer buf, std::string_view delimiter) noexcept
    -> std::optional<std::size_t>
{
    if (delimiter.empty()) {
        return std::nullopt;
    }

    auto* first = static_cast<const std::byte*>(buf.data);
    auto* last = first + buf.size;
    auto* d_first = reinterpret_cast<const std::byte*>(delimiter.data());
    auto* d_last = d_first + delimiter.size();

    auto pos = std::search(first, last, d_first, d_last);
    if (pos == last) {
        return std::nullopt;
    }

    return static_cast<std::size_t>((pos - first) + delimiter.size());
}

inline auto async_read_until_impl(io_context& ctx, socket& sock, dynamic_buffer& buf,
                                  std::string delimiter,
                                  std::size_t max_bytes,
                                  std::size_t read_chunk_size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (delimiter.empty() || read_chunk_size == 0) {
        co_return std::unexpected(make_error_code(errc::invalid_argument));
    }

    while (true) {
        if (auto found = detail::find_delimiter(buf.data(), delimiter)) {
            co_return *found;
        }

        if (buf.readable_bytes() >= max_bytes) {
            co_return std::unexpected(make_error_code(errc::no_buffer_space));
        }

        auto to_read = std::min(read_chunk_size, max_bytes - buf.readable_bytes());
        auto dst = buf.prepare(to_read);
        auto r = co_await async_read(ctx, sock, dst);
        if (!r) {
            co_return std::unexpected(r.error());
        }
        if (*r == 0) {
            co_return std::unexpected(make_error_code(errc::end_of_file));
        }

        buf.commit(*r);
    }
}

inline auto async_read_until_impl(io_context& ctx, socket& sock, dynamic_buffer& buf,
                                  std::string delimiter, cancel_token& token,
                                  std::size_t max_bytes,
                                  std::size_t read_chunk_size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (delimiter.empty() || read_chunk_size == 0) {
        co_return std::unexpected(make_error_code(errc::invalid_argument));
    }

    while (true) {
        if (auto found = detail::find_delimiter(buf.data(), delimiter)) {
            co_return *found;
        }

        if (buf.readable_bytes() >= max_bytes) {
            co_return std::unexpected(make_error_code(errc::no_buffer_space));
        }

        auto to_read = std::min(read_chunk_size, max_bytes - buf.readable_bytes());
        auto dst = buf.prepare(to_read);
        auto r = co_await async_read(ctx, sock, dst, token);
        if (!r) {
            co_return std::unexpected(r.error());
        }
        if (*r == 0) {
            co_return std::unexpected(make_error_code(errc::end_of_file));
        }

        buf.commit(*r);
    }
}

} // namespace detail

/// Async read until delimiter is present in dynamic_buffer.
/// Returns bytes from buffer readable start through the delimiter; does not consume.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
                             std::string_view delimiter,
                             std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
                             std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, max_bytes, read_chunk_size);
}

/// Cancellable async read until delimiter is present in dynamic_buffer.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
                             std::string_view delimiter, cancel_token& token,
                             std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
                             std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, token, max_bytes, read_chunk_size);
}

/// Async read until a single byte delimiter is present in dynamic_buffer.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
                             char delimiter,
                             std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
                             std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, max_bytes, read_chunk_size);
}

/// Cancellable async read until a single byte delimiter is present in dynamic_buffer.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
                             char delimiter, cancel_token& token,
                             std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
                             std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, token, max_bytes, read_chunk_size);
}

/// Async write
/// Usage: auto n = co_await async_write(ctx, sock, buf);
export auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async write
export auto async_write(io_context& ctx, socket& sock, const_buffer buf,
                        cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async write all bytes in a buffer
/// Usage: co_await async_write_all(ctx, sock, buf);
export auto async_write_all(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<void, std::error_code>>
{
    auto* data = static_cast<const std::byte*>(buf.data);
    std::size_t written = 0;

    while (written < buf.size) {
        auto r = co_await async_write(ctx, sock,
            const_buffer{data + written, buf.size - written});
        if (!r) {
            co_return std::unexpected(r.error());
        }
        if (*r == 0) {
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        }
        written += *r;
    }

    co_return {};
}

/// Cancellable async write all bytes in a buffer
export auto async_write_all(io_context& ctx, socket& sock, const_buffer buf,
                            cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    auto* data = static_cast<const std::byte*>(buf.data);
    std::size_t written = 0;

    while (written < buf.size) {
        auto r = co_await async_write(ctx, sock,
            const_buffer{data + written, buf.size - written}, token);
        if (!r) {
            co_return std::unexpected(r.error());
        }
        if (*r == 0) {
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        }
        written += *r;
    }

    co_return {};
}

/// Async recvfrom — Receive UDP datagram and get sender address
/// Usage: auto n = co_await async_recvfrom(ctx, sock, buf, peer);
export auto async_recvfrom(io_context& ctx, socket& sock,
                           mutable_buffer buf, endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async recvfrom
export auto async_recvfrom(io_context& ctx, socket& sock,
                           mutable_buffer buf, endpoint& peer,
                           cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async sendto — Send UDP datagram to specified address
/// Usage: auto n = co_await async_sendto(ctx, sock, buf, peer);
export auto async_sendto(io_context& ctx, socket& sock,
                         const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async sendto
export auto async_sendto(io_context& ctx, socket& sock,
                         const_buffer buf, const endpoint& peer,
                         cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

// =============================================================================
// Async File I/O Operations (Coroutine Version)
// =============================================================================

/// Async file open
/// Usage: auto f = co_await async_file_open(ctx, path, mode);
export auto async_file_open(io_context& ctx,
                            const std::filesystem::path& path,
                            open_mode mode)
    -> task<std::expected<file, std::error_code>>;

/// Cancellable async file open
export auto async_file_open(io_context& ctx,
                            const std::filesystem::path& path,
                            open_mode mode,
                            cancel_token& token)
    -> task<std::expected<file, std::error_code>>;

/// Async file stat
/// Usage: auto st = co_await async_file_stat(ctx, path);
export auto async_file_stat(io_context& ctx,
                            const std::filesystem::path& path)
    -> task<std::expected<file_stat, std::error_code>>;

/// Cancellable async file stat
export auto async_file_stat(io_context& ctx,
                            const std::filesystem::path& path,
                            cancel_token& token)
    -> task<std::expected<file_stat, std::error_code>>;

/// Async file close (best-effort, underlying close has no error reporting)
/// Usage: co_await async_file_close(ctx, f);
export auto async_file_close(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async file close
export auto async_file_close(io_context& ctx, file& f, cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

/// Async file read
/// Usage: auto n = co_await async_file_read(ctx, f, buf, offset);
export auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                            std::uint64_t offset = 0)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async file read
export auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                            std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async file write
/// Usage: auto n = co_await async_file_write(ctx, f, buf, offset);
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                             std::uint64_t offset = 0)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async file write
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                             std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async file flush
/// Usage: co_await async_file_flush(ctx, f);
export auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async file flush
export auto async_file_flush(io_context& ctx, file& f, cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

// =============================================================================
// Async Serial Port I/O Operations (Coroutine Version)
// =============================================================================

/// Async serial port read
/// Usage: auto n = co_await async_serial_read(ctx, port, buf);
export auto async_serial_read(io_context& ctx, serial_port& port,
                              mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async serial port read
export auto async_serial_read(io_context& ctx, serial_port& port,
                              mutable_buffer buf, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async serial port write
/// Usage: auto n = co_await async_serial_write(ctx, port, buf);
export auto async_serial_write(io_context& ctx, serial_port& port,
                               const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async serial port write
export auto async_serial_write(io_context& ctx, serial_port& port,
                               const_buffer buf, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

// =============================================================================
// Async Timer Operations
// =============================================================================

/// Async wait for specified duration
/// Usage: co_await async_timer_wait(ctx, std::chrono::milliseconds(500));
export auto async_timer_wait(io_context& ctx,
                             std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async timer
export auto async_timer_wait(io_context& ctx,
                             std::chrono::steady_clock::duration duration,
                             cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

} // namespace cnetmod
