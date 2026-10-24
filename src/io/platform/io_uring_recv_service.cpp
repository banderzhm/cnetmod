module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
    #include <liburing.h>
#endif

module cnetmod.io.platform.io_uring_recv_service;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) && defined(IORING_CQE_F_BUFFER) &&             \
    defined(IORING_CQE_F_MORE)

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

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

auto shared_io_uring_recv_service(io_uring_context& context)
    -> std::shared_ptr<io_uring_recv_service>
{
    struct registry
    {
        concurrent_containers::atomic_rw_latch latch;
        std::vector<std::pair<std::uint64_t,
            std::weak_ptr<io_uring_recv_service>>>
            loops;
        std::atomic<std::uint32_t> next_group{1};
    };

    static registry services;

    concurrent_containers::exclusive_latch_guard lock{services.latch};
    std::erase_if(services.loops, [](const auto& entry)
        {
            return entry.second.expired();
        });
    for (const auto& [id, service] : services.loops)
        if (id == context.instance_id())
            if (auto existing = service.lock())
                return existing;

    const auto group = static_cast<std::uint16_t>(
        services.next_group.fetch_add(1, std::memory_order_relaxed));
    auto created = std::make_shared<io_uring_recv_service>(
        context, group, 8192, 256);
    context.retain_owner_resource(created);
    services.loops.emplace_back(context.instance_id(), created);
    return created;
}

} // namespace cnetmod

#endif
