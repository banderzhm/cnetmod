module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#else
#include <cerrno>
#endif

module cnetmod.core.error;

namespace cnetmod {

auto from_native_error([[maybe_unused]] int native_error) noexcept -> errc {
#ifdef CNETMOD_PLATFORM_WINDOWS
    switch (native_error) {
        case 0:                    return errc::success;
        case WSAECONNREFUSED:      return errc::connection_refused;
        case WSAECONNRESET:        return errc::connection_reset;
        case WSAECONNABORTED:      return errc::connection_aborted;
        case WSAETIMEDOUT:         return errc::connection_timed_out;
        case WSAENOTCONN:          return errc::not_connected;
        case WSAEISCONN:           return errc::already_connected;
        case WSAEADDRINUSE:        return errc::address_in_use;
        case WSAEADDRNOTAVAIL:     return errc::address_not_available;
        case WSAEAFNOSUPPORT:      return errc::address_family_not_supported;
        case WSA_OPERATION_ABORTED:return errc::operation_aborted;  // == ERROR_OPERATION_ABORTED (995)
        case WSAEINPROGRESS:       return errc::operation_in_progress;
        case WSAEOPNOTSUPP:        return errc::operation_not_supported;
        case WSAEWOULDBLOCK:       return errc::operation_would_block;
        case WSAEMFILE:            return errc::too_many_files_open;
        case WSAENOBUFS:           return errc::no_buffer_space;
        case WSAENETDOWN:          return errc::network_down;
        case WSAENETUNREACH:       return errc::network_unreachable;
        case WSAEHOSTUNREACH:      return errc::host_unreachable;
        case WSAHOST_NOT_FOUND:    return errc::host_not_found;
        case WSAEACCES:            return errc::permission_denied;
        case WSAEINVAL:            return errc::invalid_argument;
        case WSAEBADF:             return errc::bad_descriptor;
        default:                   return errc::unknown_error;
    }
#else
    switch (native_error) {
        case 0:            return errc::success;
        case ECONNREFUSED: return errc::connection_refused;
        case ECONNRESET:   return errc::connection_reset;
        case ECONNABORTED: return errc::connection_aborted;
        case ETIMEDOUT:    return errc::connection_timed_out;
        case ENOTCONN:     return errc::not_connected;
        case EISCONN:      return errc::already_connected;
        case EADDRINUSE:   return errc::address_in_use;
        case EADDRNOTAVAIL:return errc::address_not_available;
        case EAFNOSUPPORT: return errc::address_family_not_supported;
        case ECANCELED:    return errc::operation_aborted;
        case EINPROGRESS:  return errc::operation_in_progress;
        case ENOTSUP:      return errc::operation_not_supported;
        case EWOULDBLOCK:  return errc::operation_would_block;
        case EMFILE:       return errc::too_many_files_open;
        case ENOBUFS:      return errc::no_buffer_space;
        case ENOMEM:       return errc::out_of_memory;
        case ENETDOWN:     return errc::network_down;
        case ENETUNREACH:  return errc::network_unreachable;
        case EHOSTUNREACH: return errc::host_unreachable;
        case EPIPE:        return errc::broken_pipe;
        case EBADF:        return errc::bad_descriptor;
        case EACCES:       return errc::permission_denied;
        case EINVAL:       return errc::invalid_argument;
        default:           return errc::unknown_error;
    }
#endif
}

} // namespace cnetmod
