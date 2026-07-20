module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <WinSock2.h>
#endif

export module cnetmod.core.error;

import std;

namespace cnetmod {

// =============================================================================
// Error code enum
// =============================================================================

/// Network operation error codes
export enum class errc {
    success = 0,

    // Connection-related
    connection_refused,
    connection_reset,
    connection_aborted,
    connection_timed_out,
    not_connected,
    already_connected,

    // Address-related
    address_in_use,
    address_not_available,
    address_family_not_supported,

    // Operation-related
    operation_aborted,
    operation_in_progress,
    operation_not_supported,
    operation_would_block,

    // Resource-related
    too_many_files_open,
    no_buffer_space,
    out_of_memory,

    // Network-related
    network_down,
    network_unreachable,
    host_unreachable,
    host_not_found,

    // I/O related
    broken_pipe,
    end_of_file,
    bad_descriptor,

    // General
    permission_denied,
    invalid_argument,
    unknown_error,
};

// =============================================================================
// error_category
// =============================================================================

/// cnetmod error category
export class network_error_category : public std::error_category {
public:
    [[nodiscard]] auto name() const noexcept -> const char* override;
    [[nodiscard]] auto message(int ev) const -> std::string override;
};

/// Get global network_error_category instance
export [[nodiscard]] auto network_category() noexcept -> const std::error_category&;

/// Create std::error_code
export [[nodiscard]] auto make_error_code(errc e) noexcept -> std::error_code;

/// Convert platform native error code to cnetmod::errc
export [[nodiscard]] auto from_native_error(int native_error) noexcept -> errc;

} // namespace cnetmod

// Register with std::error_code system
template <>
struct std::is_error_code_enum<cnetmod::errc> : std::true_type {};
