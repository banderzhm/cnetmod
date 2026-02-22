module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

export module cnetmod.core.file;

import std;
import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// Platform type aliases
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
export using file_handle_t = HANDLE;
export inline const file_handle_t invalid_file_handle = INVALID_HANDLE_VALUE;
#else
export using file_handle_t = int;
export inline constexpr file_handle_t invalid_file_handle = -1;
#endif

// =============================================================================
// File open modes
// =============================================================================

export enum class open_mode : std::uint32_t {
    read         = 0x01,
    write        = 0x02,
    read_write   = 0x03,
    append       = 0x04,
    create       = 0x08,   // Create if not exists
    truncate     = 0x10,   // Truncate if exists
    create_new   = 0x20,   // Must not exist
};

export constexpr auto operator|(open_mode a, open_mode b) noexcept -> open_mode {
    return static_cast<open_mode>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

export constexpr auto operator&(open_mode a, open_mode b) noexcept -> open_mode {
    return static_cast<open_mode>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

export constexpr auto has_flag(open_mode mode, open_mode flag) noexcept -> bool {
    return (mode & flag) == flag;
}

// =============================================================================
// File class
// =============================================================================

/// Platform-independent file handle wrapper (RAII)
/// Supports async I/O (driven by io_context)
export class file {
public:
    file() noexcept = default;
    ~file();

    // Non-copyable
    file(const file&) = delete;
    auto operator=(const file&) -> file& = delete;

    // Movable
    file(file&& other) noexcept;
    auto operator=(file&& other) noexcept -> file&;

    /// Open file
    [[nodiscard]] static auto open(
        const std::filesystem::path& path,
        open_mode mode
    ) -> std::expected<file, std::error_code>;

    /// Close file
    void close() noexcept;

    /// Get file size
    [[nodiscard]] auto size() const -> std::expected<std::uint64_t, std::error_code>;

    /// Get native handle
    [[nodiscard]] auto native_handle() const noexcept -> file_handle_t {
        return handle_;
    }

    /// Release ownership (without closing)
    [[nodiscard]] auto release() noexcept -> file_handle_t {
        auto h = handle_;
        handle_ = invalid_file_handle;
        return h;
    }

    /// Check if valid
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return handle_ != invalid_file_handle;
    }

    explicit operator bool() const noexcept { return is_open(); }

private:
    explicit file(file_handle_t handle) noexcept : handle_(handle) {}

    file_handle_t handle_ = invalid_file_handle;
};

} // namespace cnetmod
