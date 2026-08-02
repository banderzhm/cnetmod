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

/// Async read until delimiter is present in dynamic_buffer.
/// Returns bytes from buffer readable start through the delimiter; does not consume.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    std::string_view delimiter,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
    std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async read until delimiter is present in dynamic_buffer.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    std::string_view delimiter, cancel_token& token,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
    std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Async read until a single byte delimiter is present in dynamic_buffer.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    char delimiter,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
    std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async read until a single byte delimiter is present in dynamic_buffer.
export auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    char delimiter, cancel_token& token,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max(),
    std::size_t read_chunk_size = 4096)
    -> task<std::expected<std::size_t, std::error_code>>;

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
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async write all bytes in a buffer
export auto async_write_all(io_context& ctx, socket& sock, const_buffer buf,
    cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

#ifdef CNETMOD_PLATFORM_LINUX
/// Wait for a non-blocking socket to become readable without consuming bytes.
/// Used by the OpenSSL socket BIO path required for optional Linux kTLS.
export auto async_wait_readable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>;

/// Wait for a non-blocking socket to become writable without writing bytes.
export auto async_wait_writable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>;
#endif

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

/// Read an entire file into a string (open → stat → read → close).
/// Usage: auto text = co_await async_file_read_all(ctx, "config.json");
export auto async_file_read_all(io_context& ctx,
    const std::filesystem::path& path)
    -> task<std::expected<std::string, std::error_code>>;

/// Async file write
/// Usage: auto n = co_await async_file_write(ctx, f, buf, offset);
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
    std::uint64_t offset = 0)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable async file write
export auto async_file_write(io_context& ctx, file& f, const_buffer buf,
    std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Write a string to a file, create or truncate (open → write → close).
/// Usage: co_await async_file_write_all(ctx, "out.txt", content);
export auto async_file_write_all(io_context& ctx,
    const std::filesystem::path& path, std::string_view content)
    -> task<std::expected<void, std::error_code>>;

export using file_io_result =
    std::expected<std::size_t, std::error_code>;

/// One entry in a batched file read operation. The pointed-to file and buffer
/// must remain valid until async_file_read_batch() completes.
export struct file_read_request
{
    file* source = nullptr;
    mutable_buffer destination{};
    std::uint64_t offset = 0;
};

/// One entry in a batched file write operation. The pointed-to file and buffer
/// must remain valid until async_file_write_batch() completes.
export struct file_write_request
{
    file* destination = nullptr;
    const_buffer source{};
    std::uint64_t offset = 0;
};

/// Submit a batch of independent file reads. io_uring prepares as many SQEs as
/// fit in the ring and submits them together; other backends preserve the same
/// result ordering with their platform fallback.
export auto async_file_read_batch(
    io_context& ctx, std::span<const file_read_request> requests)
    -> task<std::vector<file_io_result>>;

/// Submit a batch of independent file writes.
export auto async_file_write_batch(
    io_context& ctx, std::span<const file_write_request> requests)
    -> task<std::vector<file_io_result>>;

export struct file_pipeline_options
{
    std::uint64_t offset = 0;
    std::uint64_t byte_count = std::numeric_limits<std::uint64_t>::max();
    std::size_t chunk_size = 256 * 1024;
};

/// Handler invoked for each file chunk. The buffer remains valid only until
/// the returned task completes.
export using file_chunk_handler = std::function<
    task<std::expected<void, std::error_code>>(
        const_buffer chunk, std::uint64_t offset)>;

/// Read and process a file using two alternating buffers. While the handler
/// processes the current chunk, the next chunk is read concurrently.
export auto async_file_read_pipeline(
    io_context& ctx, file& source, file_chunk_handler handler,
    file_pipeline_options options = {})
    -> task<std::expected<std::uint64_t, std::error_code>>;

/// Async file flush
/// Usage: co_await async_file_flush(ctx, f);
export auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>;

/// Cancellable async file flush
export auto async_file_flush(io_context& ctx, file& f, cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

/// Transfer a file range directly to a connected stream socket where the
/// active platform backend supports it. Stops at EOF or after byte_count.
export auto async_send_file(
    io_context& ctx, socket& sock, file& source,
    std::uint64_t offset = 0,
    std::uint64_t byte_count = std::numeric_limits<std::uint64_t>::max())
    -> task<std::expected<std::uint64_t, std::error_code>>;

/// Cancellable direct file-to-socket transfer.
export auto async_send_file(
    io_context& ctx, socket& sock, file& source,
    std::uint64_t offset, std::uint64_t byte_count, cancel_token& token)
    -> task<std::expected<std::uint64_t, std::error_code>>;

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
