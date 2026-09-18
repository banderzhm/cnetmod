module;

#include <cnetmod/config.hpp>

export module cnetmod.executor.async_op;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.buffer_pool;
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
// These are low-level coroutine interfaces used directly with co_await.

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
export auto async_wait_readable(io_context& ctx, socket& sock,
    cancel_token& token) -> task<std::expected<void, std::error_code>>;

/// Wait for a non-blocking socket to become writable without writing bytes.
export auto async_wait_writable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>;
export auto async_wait_writable(io_context& ctx, socket& sock,
    cancel_token& token) -> task<std::expected<void, std::error_code>>;
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

#ifdef CNETMOD_HAS_IOCP
/// Submit on the socket's IOCP while resuming the awaiting coroutine on its
/// connection-affine executor. This is used by shared UDP listeners whose
/// protocol state is sharded across worker contexts.
export auto async_sendto_on(io_context& socket_context,
    io_context& resume_context, socket& sock, const_buffer buf,
    const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>;
#endif

/// Cancellable async sendto
export auto async_sendto(io_context& ctx, socket& sock,
    const_buffer buf, const endpoint& peer,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

// =============================================================================
// Batched UDP I/O
// =============================================================================
//
// QUIC listeners commonly receive a burst of independent datagrams after a
// single readiness notification.  These operations preserve datagram
// boundaries and use the platform batch facility where one exists
// (recvmmsg/sendmmsg on Linux).  The other backends retain the same API and
// submit their datagrams through their native asynchronous transport.

/// Move-only datagram storage used by batched UDP receive APIs.
///
/// UDP/QUIC receivers keep a bounded cache of full-size receive buffers.  A
/// completed datagram transfers the lease all the way to the consumer instead
/// of allocating a vector for every packet.  Payloads larger than the pool
/// block retain the same interface and use an isolated heap buffer; this is a
/// correctness fallback, not part of the usual QUIC packet path.
export class udp_datagram_buffer
{
public:
    using value_type = std::byte;
    using size_type = std::size_t;
    using iterator = std::byte*;
    using const_iterator = const std::byte*;

    udp_datagram_buffer() = default;
    explicit udp_datagram_buffer(size_type capacity);

    udp_datagram_buffer(const udp_datagram_buffer&) = delete;
    auto operator=(const udp_datagram_buffer&) -> udp_datagram_buffer& = delete;
    udp_datagram_buffer(udp_datagram_buffer&&) noexcept = default;
    auto operator=(udp_datagram_buffer&&) noexcept -> udp_datagram_buffer& = default;

    [[nodiscard]] auto data() noexcept -> std::byte*;
    [[nodiscard]] auto data() const noexcept -> const std::byte*;
    [[nodiscard]] auto size() const noexcept -> size_type;
    [[nodiscard]] auto capacity() const noexcept -> size_type;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto is_pooled() const noexcept -> bool;
    [[nodiscard]] auto begin() noexcept -> iterator;
    [[nodiscard]] auto begin() const noexcept -> const_iterator;
    [[nodiscard]] auto end() noexcept -> iterator;
    [[nodiscard]] auto end() const noexcept -> const_iterator;
    [[nodiscard]] auto front() noexcept -> std::byte&;
    [[nodiscard]] auto front() const noexcept -> const std::byte&;
    [[nodiscard]] auto operator[](size_type index) noexcept -> std::byte&;
    [[nodiscard]] auto operator[](size_type index) const noexcept -> const std::byte&;

    /// Changes the visible payload length without reallocating the pooled
    /// lease. Expanding a heap-backed buffer keeps byte_buffer semantics.
    void resize(size_type size);

    template <std::forward_iterator Iterator,
        std::sentinel_for<Iterator> Sentinel>
    void assign(Iterator first, Sentinel last)
    {
        const auto count = static_cast<size_type>(std::ranges::distance(first, last));
        resize(count);
        std::ranges::copy(first, last, begin());
    }

    operator std::span<const std::byte>() const noexcept;
    operator std::span<std::byte>() noexcept;

private:
    pooled_buffer pooled_;
    byte_buffer heap_;
    size_type size_{};
};

namespace detail {

    /// Deliberately process-lifetime storage: datagram leases can outlive an I/O
    /// context while they are queued in a connection inbox.  Leaking this bounded
    /// cache on process shutdown prevents static-destruction ordering from racing
    /// a final cross-worker lease return.
    inline auto udp_datagram_pool() noexcept -> buffer_pool&
    {
        static auto* const pool = new buffer_pool{65536U, 512U};
        return *pool;
    }

} // namespace detail

inline udp_datagram_buffer::udp_datagram_buffer(size_type requested_capacity)
{
    auto& pool = detail::udp_datagram_pool();
    if (requested_capacity <= pool.block_size())
    {
        pooled_ = pool.acquire();
        size_ = requested_capacity;
        return;
    }
    heap_.resize(requested_capacity);
    size_ = requested_capacity;
}

inline auto udp_datagram_buffer::data() noexcept -> std::byte*
{
    return pooled_.valid() ? static_cast<std::byte*>(pooled_.data()) : heap_.data();
}

inline auto udp_datagram_buffer::data() const noexcept -> const std::byte*
{
    return pooled_.valid() ? static_cast<const std::byte*>(pooled_.data()) : heap_.data();
}

inline auto udp_datagram_buffer::size() const noexcept -> size_type
{
    return size_;
}

inline auto udp_datagram_buffer::capacity() const noexcept -> size_type
{
    return pooled_.valid() ? pooled_.size() : heap_.capacity();
}

inline auto udp_datagram_buffer::empty() const noexcept -> bool
{
    return size_ == 0U;
}

inline auto udp_datagram_buffer::is_pooled() const noexcept -> bool
{
    return pooled_.valid();
}

inline auto udp_datagram_buffer::begin() noexcept -> iterator
{
    return data();
}

inline auto udp_datagram_buffer::begin() const noexcept -> const_iterator
{
    return data();
}

inline auto udp_datagram_buffer::end() noexcept -> iterator
{
    return data() + size_;
}

inline auto udp_datagram_buffer::end() const noexcept -> const_iterator
{
    return data() + size_;
}

inline auto udp_datagram_buffer::front() noexcept -> std::byte&
{
    return *data();
}

inline auto udp_datagram_buffer::front() const noexcept -> const std::byte&
{
    return *data();
}

inline auto udp_datagram_buffer::operator[](size_type index) noexcept -> std::byte&
{
    return data()[index];
}

inline auto udp_datagram_buffer::operator[](size_type index) const noexcept -> const std::byte&
{
    return data()[index];
}

inline void udp_datagram_buffer::resize(size_type requested_size)
{
    if (pooled_.valid())
    {
        if (requested_size > pooled_.size())
            throw std::length_error("udp datagram exceeds pooled receive block");
        size_ = requested_size;
        return;
    }
    heap_.resize(requested_size);
    size_ = requested_size;
}

inline udp_datagram_buffer::operator std::span<const std::byte>() const noexcept
{
    return {data(), size_};
}

inline udp_datagram_buffer::operator std::span<std::byte>() noexcept
{
    return {data(), size_};
}

export struct udp_received_datagram
{
    udp_datagram_buffer bytes;
    endpoint peer;
};

export struct udp_send_datagram
{
    const_buffer bytes;
    endpoint peer;
};

/// Receive at least one UDP datagram, then drain up to max_datagrams packets.
/// max_datagrams and max_datagram_size must both be non-zero.
export auto async_recvfrom_batch(io_context& ctx, socket& sock,
    std::size_t max_datagrams, std::size_t max_datagram_size)
    -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>;

/// Cancellable counterpart. Cancellation interrupts the caller's wait for a
/// batch; registered-I/O receive rings remain posted for the listener and are
/// reclaimed only by socket close.
export auto async_recvfrom_batch(io_context& ctx, socket& sock,
    std::size_t max_datagrams, std::size_t max_datagram_size, cancel_token& token)
    -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>;

/// Submit a bounded batch of UDP datagrams.  The result is the number accepted
/// by the operating system; a partial result is normal under UDP backpressure.
export auto async_sendto_batch(io_context& ctx, socket& sock,
    std::span<const udp_send_datagram> datagrams)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Cancellable batch submission. A partial successful count is preserved when
/// cancellation arrives after the operating system accepted earlier packets.
export auto async_sendto_batch(io_context& ctx, socket& sock,
    std::span<const udp_send_datagram> datagrams, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>;

/// Pre-initialize the platform UDP backend selected by `socket_options::registered_io`.
/// On Windows this creates the RIO queues before a listener becomes visible to
/// callers, so an unavailable RIO provider can be replaced by a normal IOCP
/// socket during `udp_socket::open` rather than failing on the first packet.
/// Other platforms have no equivalent eager setup and return success.
export auto prepare_async_datagram_io(io_context& ctx, socket& sock,
    std::size_t max_datagram_size = 65536U, std::size_t receive_depth = 64U)
    -> std::expected<void, std::error_code>;

#ifndef CNETMOD_HAS_IOCP
inline auto prepare_async_datagram_io(io_context& ctx, socket& sock,
    std::size_t max_datagram_size, std::size_t receive_depth)
    -> std::expected<void, std::error_code>
{
    // epoll, io_uring and kqueue initialize UDP work lazily on the first
    // operation. Keep the same cross-platform API without adding setup work.
    (void)ctx;
    (void)sock;
    (void)max_datagram_size;
    (void)receive_depth;
    return {};
}
#endif

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

/**
 * @brief Removes a file without blocking the calling event loop.
 *
 * Removing a path that does not exist succeeds. Directory removal is not part
 * of this API. Platform backends resume the coroutine on the supplied context.
 */
export auto async_file_remove(io_context& ctx,
    const std::filesystem::path& path)
    -> task<std::expected<void, std::error_code>>;

/**
 * @brief Removes a file with best-effort cancellation.
 *
 * Cancellation can prevent work before submission but cannot undo a removal
 * that has already completed in a platform worker.
 */
export auto async_file_remove(io_context& ctx,
    const std::filesystem::path& path, cancel_token& token)
    -> task<std::expected<void, std::error_code>>;

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

/**
 * @brief Reads an entire file with cancellation between every operation.
 */
export auto async_file_read_all(io_context& ctx,
    const std::filesystem::path& path, cancel_token& token)
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

/**
 * @brief Writes an entire string with cancellation between every operation.
 */
export auto async_file_write_all(io_context& ctx,
    const std::filesystem::path& path, std::string_view content,
    cancel_token& token)
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
