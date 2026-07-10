module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#endif

module cnetmod.core.net_init;

import std;

namespace cnetmod
{
    net_init::net_init()
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        WSADATA wsa{};
        const int err = ::WSAStartup(MAKEWORD(2, 2), &wsa);
        if (err != 0) throw std::runtime_error(std::format("WSAStartup failed: {}", err));
#endif
    }

    net_init::~net_init()
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        ::WSACleanup();
#endif
    }
} // namespace cnetmod