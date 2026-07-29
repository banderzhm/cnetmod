module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
    #include <Windows.h>
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
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

export enum class open_mode : std::uint32_t
{
    read = 0x01,
    write = 0x02,
    read_write = 0x03,
    append = 0x04,
    create = 0x08,     // Create if not exists
    truncate = 0x10,   // Truncate if exists
    create_new = 0x20, // Must not exist
    direct = 0x40,     // Bypass/minimize the platform page cache
};

export constexpr auto operator|(open_mode a, open_mode b) noexcept -> open_mode
{
    return static_cast<open_mode>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

export constexpr auto operator&(open_mode a, open_mode b) noexcept -> open_mode
{
    return static_cast<open_mode>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

export constexpr auto has_flag(open_mode mode, open_mode flag) noexcept -> bool
{
    return (mode & flag) == flag;
}

export enum class file_strategy
{
    buffered,
    direct,
    zero_copy,
};

export struct file_strategy_options
{
    bool to_socket = false;
    bool encrypted_transport = false;
    bool requires_processing = false;
    bool allow_direct = false;
    std::uint64_t direct_threshold = 16 * 1024 * 1024;
};

/// Select a conservative transfer strategy. Direct I/O is never selected
/// solely from file size: the caller must explicitly allow it.
export constexpr auto select_file_strategy(
    std::uint64_t file_size, file_strategy_options options = {}) noexcept
    -> file_strategy
{
    if (options.to_socket && !options.encrypted_transport &&
        !options.requires_processing)
    {
        return file_strategy::zero_copy;
    }
    if (options.allow_direct && !options.requires_processing &&
        file_size >= options.direct_threshold)
    {
        return file_strategy::direct;
    }
    return file_strategy::buffered;
}

// =============================================================================
// File stat
// =============================================================================

export struct file_stat
{
    std::uint64_t size = 0;
    bool is_regular = false;
    bool is_directory = false;
};

// =============================================================================
// File class
// =============================================================================

/// Platform-independent file handle wrapper (RAII)
/// Supports async I/O (driven by io_context)
export class file
{
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
    /// Note: Async version is async_file_open() in cnetmod.executor.async_op
    [[nodiscard]] static auto open(
        const std::filesystem::path& path,
        open_mode mode) -> std::expected<file, std::error_code>;

    /// Stat file by path
    /// Note: Async version is async_file_stat() in cnetmod.executor.async_op
    [[nodiscard]] static auto stat(
        const std::filesystem::path& path) -> std::expected<file_stat, std::error_code>;

    /// Close file
    void close() noexcept;

    /// Get file size
    [[nodiscard]] auto size() const -> std::expected<std::uint64_t, std::error_code>;

    /// Get native handle
    [[nodiscard]] auto native_handle() const noexcept -> file_handle_t
    {
        return handle_;
    }

    /// Release ownership (without closing)
    [[nodiscard]] auto release() noexcept -> file_handle_t
    {
        auto h = handle_;
        handle_ = invalid_file_handle;
        return h;
    }

    /// Check if valid
    [[nodiscard]] auto is_open() const noexcept -> bool
    {
        return handle_ != invalid_file_handle;
    }

    explicit operator bool() const noexcept
    {
        return is_open();
    }

private:
    explicit file(file_handle_t handle) noexcept
        : handle_(handle) {}

    file_handle_t handle_ = invalid_file_handle;
};

} // namespace cnetmod
