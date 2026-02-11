module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#endif

export module cnetmod.core.net_init;

import std;

namespace cnetmod {

// =============================================================================
// net_init — 跨平台网络初始化 RAII guard
// =============================================================================

/// 构造时初始化平台网络库，析构时清理
/// Windows: WSAStartup / WSACleanup
/// Linux/macOS: no-op
export class net_init {
public:
    net_init() {
#ifdef CNETMOD_PLATFORM_WINDOWS
        WSADATA wsa{};
        int err = ::WSAStartup(MAKEWORD(2, 2), &wsa);
        if (err != 0)
            throw std::runtime_error(
                std::format("WSAStartup failed: {}", err));
#endif
    }

    ~net_init() {
#ifdef CNETMOD_PLATFORM_WINDOWS
        ::WSACleanup();
#endif
    }

    net_init(const net_init&) = delete;
    auto operator=(const net_init&) -> net_init& = delete;
    net_init(net_init&&) = delete;
    auto operator=(net_init&&) -> net_init& = delete;
};

} // namespace cnetmod
