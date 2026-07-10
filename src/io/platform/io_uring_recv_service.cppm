module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
#include <liburing.h>
#endif

export module cnetmod.io.platform.io_uring_recv_service;

import std;
import cnetmod.core.socket;
import cnetmod.io.platform.io_uring;
import cnetmod.io.platform.io_uring_buffer_ring;
import cnetmod.io.platform.io_uring_multishot_recv;

namespace cnetmod {

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT)

/// Context-scoped owner for a shared provided-buffer ring.
/// Keep one service alive for every io_uring worker context.
export class io_uring_recv_service {
public:
    io_uring_recv_service(io_uring_context& context, std::uint16_t buffer_group,
                          std::size_t block_size = 8192,
                          unsigned block_count = 256);

    io_uring_recv_service(const io_uring_recv_service&) = delete;
    auto operator=(const io_uring_recv_service&) -> io_uring_recv_service& = delete;

    [[nodiscard]] auto make_receiver(socket& socket)
        -> std::unique_ptr<io_uring_multishot_recv>;

private:
    io_uring_context* context_{};
    io_uring_buffer_ring buffers_;
};

#endif

} // namespace cnetmod
