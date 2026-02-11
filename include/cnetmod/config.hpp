#pragma once

// =============================================================================
// 平台检测
// =============================================================================

#if defined(_WIN32) || defined(_WIN64)
    #ifndef CNETMOD_PLATFORM_WINDOWS
        #define CNETMOD_PLATFORM_WINDOWS
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #ifndef CNETMOD_PLATFORM_MACOS
        #define CNETMOD_PLATFORM_MACOS
    #endif
#elif defined(__linux__)
    #ifndef CNETMOD_PLATFORM_LINUX
        #define CNETMOD_PLATFORM_LINUX
    #endif
#endif

// =============================================================================
// I/O 后端检测
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
    #ifndef CNETMOD_HAS_IOCP
        #define CNETMOD_HAS_IOCP
    #endif
#endif

#ifdef CNETMOD_PLATFORM_LINUX
    #ifndef CNETMOD_HAS_EPOLL
        #define CNETMOD_HAS_EPOLL
    #endif
    // io_uring 需要 liburing，通过 CMake 检测
#endif

#ifdef CNETMOD_PLATFORM_MACOS
    #ifndef CNETMOD_HAS_KQUEUE
        #define CNETMOD_HAS_KQUEUE
    #endif
#endif

// =============================================================================
// 平台特定头文件
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif
