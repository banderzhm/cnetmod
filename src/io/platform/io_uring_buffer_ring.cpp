module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
#include <liburing.h>
#include <liburing/io_uring.h>
#include <cstdlib>
#include <cerrno>
#endif

module cnetmod.io.platform.io_uring_buffer_ring;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
import std;

namespace cnetmod {

namespace {

auto is_power_of_two(unsigned value) noexcept -> bool {
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

io_uring_buffer_ring::io_uring_buffer_ring(io_uring_context& context,
                                           std::uint16_t group_id,
                                           std::size_t block_size,
                                           unsigned block_count)
    : context_(&context), group_id_(group_id)
{
    if (!is_power_of_two(block_count))
        throw std::invalid_argument("io_uring buffer-ring block_count must be a power of two");
    if (block_size == 0)
        throw std::invalid_argument("io_uring buffer-ring block_size must be non-zero");

    const auto allocation_size = sizeof(::io_uring_buf_ring) +
        static_cast<std::size_t>(block_count) * sizeof(::io_uring_buf);
    void* allocation = nullptr;
    if (::posix_memalign(&allocation, 4096, allocation_size) != 0)
        throw std::bad_alloc{};
    ring_ = static_cast<::io_uring_buf_ring*>(allocation);
    ::io_uring_buf_ring_init(ring_);

    blocks_.reserve(block_count);
    for (unsigned i = 0; i < block_count; ++i)
        blocks_.emplace_back(block_size);

    ::io_uring_buf_reg registration{};
    registration.ring_addr = reinterpret_cast<unsigned long>(ring_);
    registration.ring_entries = block_count;
    registration.bgid = group_id_;
    const int result = ::io_uring_register_buf_ring(context_->native_ring(),
                                                    &registration, 0);
    if (result < 0) {
        std::free(ring_);
        ring_ = nullptr;
        throw std::system_error(-result, std::generic_category(),
                                "io_uring_register_buf_ring failed");
    }

    mask_ = ::io_uring_buf_ring_mask(block_count);
    for (unsigned i = 0; i < block_count; ++i) {
        ::io_uring_buf_ring_add(ring_, blocks_[i].data(),
                                static_cast<unsigned>(blocks_[i].size()),
                                static_cast<std::uint16_t>(i), mask_, i);
    }
    ::io_uring_buf_ring_advance(ring_, block_count);
}

io_uring_buffer_ring::~io_uring_buffer_ring() {
    if (ring_) {
        (void)::io_uring_unregister_buf_ring(context_->native_ring(), group_id_);
        std::free(ring_);
    }
}

auto io_uring_buffer_ring::group_id() const noexcept -> std::uint16_t {
    return group_id_;
}

auto io_uring_buffer_ring::block_size() const noexcept -> std::size_t {
    return blocks_.empty() ? 0 : blocks_.front().size();
}

auto io_uring_buffer_ring::block_count() const noexcept -> unsigned {
    return static_cast<unsigned>(blocks_.size());
}

auto io_uring_buffer_ring::buffer(std::uint16_t buffer_id) noexcept -> mutable_buffer {
    if (buffer_id >= blocks_.size())
        return {};
    auto& block = blocks_[buffer_id];
    return {block.data(), block.size()};
}

void io_uring_buffer_ring::release(std::uint16_t buffer_id) noexcept {
    if (!ring_ || buffer_id >= blocks_.size())
        return;
    auto& block = blocks_[buffer_id];
    ::io_uring_buf_ring_add(ring_, block.data(), static_cast<unsigned>(block.size()),
                            buffer_id, mask_, 0);
    ::io_uring_buf_ring_advance(ring_, 1);
}

} // namespace cnetmod

#endif
