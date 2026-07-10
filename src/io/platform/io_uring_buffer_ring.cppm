module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
#include <liburing.h>
#include <liburing/io_uring.h>
#endif

export module cnetmod.io.platform.io_uring_buffer_ring;

import std;
import cnetmod.core.buffer;
import cnetmod.io.platform.io_uring;

namespace cnetmod {

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)

/// Registered io_uring provided-buffer group.
///
/// The kernel selects a block for recv operations that use IOSQE_BUFFER_SELECT.
/// Call release() only after the consumer no longer reads that block.
export class io_uring_buffer_ring {
public:
    io_uring_buffer_ring(io_uring_context& context,
                         std::uint16_t group_id,
                         std::size_t block_size = 8192,
                         unsigned block_count = 256);
    ~io_uring_buffer_ring();

    io_uring_buffer_ring(const io_uring_buffer_ring&) = delete;
    auto operator=(const io_uring_buffer_ring&) -> io_uring_buffer_ring& = delete;
    io_uring_buffer_ring(io_uring_buffer_ring&&) = delete;
    auto operator=(io_uring_buffer_ring&&) -> io_uring_buffer_ring& = delete;

    [[nodiscard]] auto group_id() const noexcept -> std::uint16_t;
    [[nodiscard]] auto block_size() const noexcept -> std::size_t;
    [[nodiscard]] auto block_count() const noexcept -> unsigned;

    /// View a buffer selected by the kernel in a CQE.
    [[nodiscard]] auto buffer(std::uint16_t buffer_id) noexcept -> mutable_buffer;

    /// Return a consumed buffer to the kernel-provided ring.
    void release(std::uint16_t buffer_id) noexcept;

private:
    io_uring_context* context_{};
    ::io_uring_buf_ring* ring_{};
    std::vector<std::vector<std::byte>> blocks_;
    std::uint16_t group_id_{};
    unsigned mask_{};
};

#endif

} // namespace cnetmod
