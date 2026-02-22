module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IOCP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>


export module cnetmod.io.platform.iocp;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

// =============================================================================
// IOCP coroutine operation base class
// =============================================================================

/// OVERLAPPED + coroutine_handle wrapper
/// Each async operation inherits this class, automatically resumes coroutine after I/O completion
export struct iocp_overlapped : OVERLAPPED {
    std::coroutine_handle<> coroutine{};  // Coroutine to resume after completion
    std::error_code error{};              // Completion error code
    DWORD bytes_transferred = 0;          // Transferred bytes

    iocp_overlapped() noexcept {
        Internal = 0;
        InternalHigh = 0;
        Offset = 0;
        OffsetHigh = 0;
        hEvent = nullptr;
    }

    void reset() noexcept {
        Internal = 0;
        InternalHigh = 0;
        Offset = 0;
        OffsetHigh = 0;
        hEvent = nullptr;
        coroutine = {};
        error = {};
        bytes_transferred = 0;
    }

    /// Set file offset
    void set_offset(std::uint64_t offset) noexcept {
        Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        OffsetHigh = static_cast<DWORD>(offset >> 32);
    }
};

// =============================================================================
// IOCP Context implementation
// =============================================================================

/// Windows IOCP (I/O Completion Port) event loop
export class iocp_context : public io_context {
public:
    iocp_context() {
        iocp_handle_ = ::CreateIoCompletionPort(
            INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (iocp_handle_ == nullptr)
            throw std::system_error(
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "CreateIoCompletionPort failed");
    }

    ~iocp_context() override {
        if (iocp_handle_ != nullptr && iocp_handle_ != INVALID_HANDLE_VALUE)
            ::CloseHandle(iocp_handle_);
    }

    /// Run event loop until stop()
    void run() override {
        while (!stopped_.load(std::memory_order_relaxed)) {
            run_batch_impl(INFINITE);
        }
    }

    /// Process a batch of completion events (blocking)
    auto run_one() -> std::size_t override {
        return run_batch_impl(INFINITE);
    }

    /// Non-blocking poll
    auto poll() -> std::size_t override {
        return run_batch_impl(0);
    }

    void stop() override {
        stopped_.store(true, std::memory_order_relaxed);
        // Post an empty completion packet to wake GetQueuedCompletionStatus
        ::PostQueuedCompletionStatus(iocp_handle_, 0, 0, nullptr);
    }

    [[nodiscard]] auto stopped() const noexcept -> bool override {
        return stopped_.load(std::memory_order_relaxed);
    }

    void restart() override {
        stopped_.store(false, std::memory_order_relaxed);
    }

    /// Associate handle (socket/file HANDLE) to IOCP
    [[nodiscard]] auto associate(HANDLE handle)
        -> std::expected<void, std::error_code>
    {
        auto result = ::CreateIoCompletionPort(
            handle, iocp_handle_, 0, 0);
        if (result == nullptr)
            return std::unexpected(
                std::error_code(static_cast<int>(::GetLastError()),
                                std::system_category()));
        return {};
    }

    /// Post user-defined completion notification
    void post_completion(iocp_overlapped* ov) {
        ::PostQueuedCompletionStatus(
            iocp_handle_, 0, 0,
            reinterpret_cast<LPOVERLAPPED>(ov));
    }

    /// Get native IOCP handle
    [[nodiscard]] auto native_handle() const noexcept -> HANDLE {
        return iocp_handle_;
    }

protected:
    void wake() override {
        // key=1 distinguishes from stop(key=0), ov=nullptr indicates post wake
        ::PostQueuedCompletionStatus(iocp_handle_, 0, 1, nullptr);
    }

private:
    static constexpr ULONG max_batch_ = 64;

    /// Core event dispatch — batch version
    auto run_batch_impl(DWORD timeout_ms) -> std::size_t {
        OVERLAPPED_ENTRY entries[max_batch_];
        ULONG removed = 0;

        BOOL ok = ::GetQueuedCompletionStatusEx(
            iocp_handle_, entries, max_batch_, &removed,
            timeout_ms, FALSE);

        if (!ok || removed == 0)
            return 0;

        std::size_t handled = 0;
        for (ULONG i = 0; i < removed; ++i) {
            auto& entry = entries[i];

            if (entry.lpOverlapped == nullptr) {
                if (entry.lpCompletionKey == 1) {
                    // post wake signal
                    handled += drain_post_queue();
                }
                // key==0: stop signal, ignore
                continue;
            }

            auto* iov = static_cast<iocp_overlapped*>(entry.lpOverlapped);

            // Check error: Internal field stores NTSTATUS
            if (entry.lpOverlapped->Internal != 0) {
                // NTSTATUS → Win32 error code (RtlNtStatusToDosError from ntdll)
                iov->error = std::error_code(
                    static_cast<int>(ntstatus_to_win32(
                        static_cast<long>(entry.lpOverlapped->Internal))),
                    std::system_category());
            }
            iov->bytes_transferred = entry.dwNumberOfBytesTransferred;

            if (iov->coroutine)
                iov->coroutine.resume();

            ++handled;
        }
        return handled;
    }

    /// Dynamically load ntdll.dll!RtlNtStatusToDosError
    static auto ntstatus_to_win32(long ntstatus) noexcept -> unsigned long {
        using fn_t = unsigned long (__stdcall*)(long);
        static fn_t fn = []() -> fn_t {
            auto* mod = ::GetModuleHandleW(L"ntdll.dll");
            if (!mod) return nullptr;
            return reinterpret_cast<fn_t>(
                ::GetProcAddress(mod, "RtlNtStatusToDosError"));
        }();
        if (fn)
            return fn(ntstatus);
        return static_cast<unsigned long>(ntstatus); // fallback
    }

    HANDLE iocp_handle_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_IOCP
