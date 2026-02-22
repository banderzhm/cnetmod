module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

module cnetmod.core.file;

import cnetmod.core.error;

namespace cnetmod {

file::~file() {
    close();
}

file::file(file&& other) noexcept : handle_(other.handle_) {
    other.handle_ = invalid_file_handle;
}

auto file::operator=(file&& other) noexcept -> file& {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = invalid_file_handle;
    }
    return *this;
}

// =============================================================================
// Open
// =============================================================================

auto file::open(const std::filesystem::path& path, open_mode mode)
    -> std::expected<file, std::error_code>
{
#ifdef CNETMOD_PLATFORM_WINDOWS
    // --- Access permissions ---
    DWORD access = 0;
    if (has_flag(mode, open_mode::read))
        access |= GENERIC_READ;
    if (has_flag(mode, open_mode::write) || has_flag(mode, open_mode::append))
        access |= GENERIC_WRITE;

    // --- Share mode ---
    DWORD share = FILE_SHARE_READ;

    // --- Creation disposition ---
    DWORD disposition = OPEN_EXISTING;
    if (has_flag(mode, open_mode::create_new))
        disposition = CREATE_NEW;
    else if (has_flag(mode, open_mode::truncate) && has_flag(mode, open_mode::create))
        disposition = CREATE_ALWAYS;
    else if (has_flag(mode, open_mode::create))
        disposition = OPEN_ALWAYS;
    else if (has_flag(mode, open_mode::truncate))
        disposition = TRUNCATE_EXISTING;

    // FILE_FLAG_OVERLAPPED allows handle to be associated with IOCP
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;

    HANDLE h = ::CreateFileW(
        path.c_str(),
        access,
        share,
        nullptr,
        disposition,
        flags,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        int err = static_cast<int>(::GetLastError());
        return std::unexpected(make_error_code(from_native_error(err)));
    }

    return file{h};

#else
    // --- POSIX implementation ---
    int flags = 0;
    if (has_flag(mode, open_mode::read_write))
        flags |= O_RDWR;
    else if (has_flag(mode, open_mode::write))
        flags |= O_WRONLY;
    else
        flags |= O_RDONLY;

    if (has_flag(mode, open_mode::append))   flags |= O_APPEND;
    if (has_flag(mode, open_mode::create))   flags |= O_CREAT;
    if (has_flag(mode, open_mode::truncate)) flags |= O_TRUNC;
    if (has_flag(mode, open_mode::create_new))
        flags |= (O_CREAT | O_EXCL);

    int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0)
        return std::unexpected(make_error_code(from_native_error(errno)));

    return file{fd};
#endif
}

// =============================================================================
// Close
// =============================================================================

void file::close() noexcept {
    if (handle_ == invalid_file_handle) return;
#ifdef CNETMOD_PLATFORM_WINDOWS
    ::CloseHandle(handle_);
#else
    ::close(handle_);
#endif
    handle_ = invalid_file_handle;
}

// =============================================================================
// File size
// =============================================================================

auto file::size() const -> std::expected<std::uint64_t, std::error_code> {
#ifdef CNETMOD_PLATFORM_WINDOWS
    LARGE_INTEGER li{};
    if (!::GetFileSizeEx(handle_, &li)) {
        int err = static_cast<int>(::GetLastError());
        return std::unexpected(make_error_code(from_native_error(err)));
    }
    return static_cast<std::uint64_t>(li.QuadPart);
#else
    struct stat st{};
    if (::fstat(handle_, &st) != 0)
        return std::unexpected(make_error_code(from_native_error(errno)));
    return static_cast<std::uint64_t>(st.st_size);
#endif
}

} // namespace cnetmod
