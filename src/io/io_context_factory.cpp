module;

#include <cnetmod/config.hpp>

module cnetmod.io.io_context;

#ifdef CNETMOD_PLATFORM_WINDOWS
import cnetmod.io.platform.iocp;
#elif defined(CNETMOD_HAS_IO_URING)
import cnetmod.io.platform.io_uring;
#elif defined(CNETMOD_HAS_EPOLL)
import cnetmod.io.platform.epoll;
#elif defined(CNETMOD_HAS_KQUEUE)
import cnetmod.io.platform.kqueue;
#endif

import std;

namespace cnetmod {

auto make_io_context() -> std::unique_ptr<io_context> {
#ifdef CNETMOD_PLATFORM_WINDOWS
    return std::make_unique<iocp_context>();
#elif defined(CNETMOD_HAS_IO_URING)
    return std::make_unique<io_uring_context>();
#elif defined(CNETMOD_HAS_EPOLL)
    return std::make_unique<epoll_context>();
#elif defined(CNETMOD_HAS_KQUEUE)
    return std::make_unique<kqueue_context>();
#else
    throw std::runtime_error("make_io_context: platform not implemented");
#endif
}

} // namespace cnetmod
