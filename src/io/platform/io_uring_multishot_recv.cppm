module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
#include <liburing.h>
#endif

export module cnetmod.io.platform.io_uring_multishot_recv;

import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.io.platform.io_uring;
import cnetmod.io.platform.io_uring_buffer_ring;

namespace cnetmod {

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) && \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)

export class io_uring_multishot_recv;

/// Owns one kernel-selected buffer until destruction or release().
export class io_uring_received_buffer {
public:
    io_uring_received_buffer() noexcept = default;
    ~io_uring_received_buffer();

    io_uring_received_buffer(const io_uring_received_buffer&) = delete;
    auto operator=(const io_uring_received_buffer&) -> io_uring_received_buffer& = delete;
    io_uring_received_buffer(io_uring_received_buffer&& other) noexcept;
    auto operator=(io_uring_received_buffer&& other) noexcept -> io_uring_received_buffer&;

    [[nodiscard]] auto data() const noexcept -> const_buffer;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    void release() noexcept;

private:
    friend class io_uring_multishot_recv;
    io_uring_received_buffer(io_uring_multishot_recv* owner,
                             std::uint16_t buffer_id,
                             std::size_t size) noexcept;

    io_uring_multishot_recv* owner_{};
    std::uint16_t buffer_id_{};
    std::size_t size_{};
};

/// Persistent recv operation backed by an io_uring provided-buffer ring.
/// Exactly one async_next() waiter is permitted.  co_await async_stop() before
/// destroying the stream, so the kernel no longer holds its operation state.
export class io_uring_multishot_recv {
public:
    io_uring_multishot_recv(io_uring_context& context, socket& socket,
                            io_uring_buffer_ring& buffers);
    ~io_uring_multishot_recv();

    io_uring_multishot_recv(const io_uring_multishot_recv&) = delete;
    auto operator=(const io_uring_multishot_recv&) -> io_uring_multishot_recv& = delete;

    auto async_next() -> task<std::expected<io_uring_received_buffer, std::error_code>>;
    auto async_stop() -> task<std::expected<void, std::error_code>>;

private:
    friend class io_uring_received_buffer;

    struct ready_buffer {
        std::uint16_t buffer_id;
        std::size_t size;
    };

    static void on_completion(uring_overlapped&, int32_t, std::uint32_t);
    void handle_completion(int32_t result, std::uint32_t flags);
    auto start() -> std::expected<void, std::error_code>;
    void release_buffer(std::uint16_t buffer_id) noexcept;

    io_uring_context* context_{};
    int socket_fd_ = -1;
    io_uring_buffer_ring* buffers_{};
    uring_overlapped operation_{};
    std::deque<ready_buffer> ready_;
    uring_overlapped* next_waiter_{};
    uring_overlapped* stop_waiter_{};
    int terminal_result_{};
    bool active_ = false;
    bool stopping_ = false;
};

#endif

} // namespace cnetmod
