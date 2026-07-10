module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
#include <liburing.h>
#endif

module cnetmod.io.platform.io_uring_recv_service;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)

import std;

namespace cnetmod {

io_uring_recv_service::io_uring_recv_service(
    io_uring_context& context, std::uint16_t buffer_group,
    std::size_t block_size, unsigned block_count)
    : context_(&context), buffers_(context, buffer_group, block_size, block_count) {}

auto io_uring_recv_service::make_receiver(socket& socket)
    -> std::unique_ptr<io_uring_multishot_recv>
{
    return std::make_unique<io_uring_multishot_recv>(*context_, socket, buffers_);
}

} // namespace cnetmod

#endif
