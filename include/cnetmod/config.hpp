#pragma once

// =============================================================================
// Platform detection
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
// I/O backend detection
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
    // Io_uring liburing, CMake
#endif

#ifdef CNETMOD_PLATFORM_MACOS
    #ifndef CNETMOD_HAS_KQUEUE
        #define CNETMOD_HAS_KQUEUE
    #endif
#endif

// =============================================================================
// SSL/TLS ( CMake CNETMOD_HAS_SSL)
// =============================================================================

// CNETMOD_HAS_SSL CMakeLists.txt target_compile_definitions
// OpenSSL CNETMOD_ENABLE_SSL=ON enable

// =============================================================================
// Platform-specific headers
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

// =============================================================================
// Third-party warning suppression
// =============================================================================

#ifdef _MSC_VER
    // C4324: structure was padded due to alignment specifier
    // Implementation note: stdexec.
    #pragma warning(disable: 4324)
#endif
