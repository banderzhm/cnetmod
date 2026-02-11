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
// IOCP 协程操作基类
// =============================================================================

/// OVERLAPPED + coroutine_handle 封装
/// 每个异步操作都继承此类，I/O 完成后自动恢复协程
export struct iocp_overlapped : OVERLAPPED {
    std::coroutine_handle<> coroutine{};  // 完成后恢复的协程
    std::error_code error{};              // 完成错误码
    DWORD bytes_transferred = 0;          // 传输字节数

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

    /// 设置文件偏移
    void set_offset(std::uint64_t offset) noexcept {
        Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        OffsetHigh = static_cast<DWORD>(offset >> 32);
    }
};

// =============================================================================
// IOCP Context 实现
// =============================================================================

/// Windows IOCP (I/O Completion Port) 事件循环
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

    /// 运行事件循环直到 stop()
    void run() override {
        while (!stopped_.load(std::memory_order_relaxed)) {
            run_batch_impl(INFINITE);
        }
    }

    /// 处理一批完成事件（阻塞）
    auto run_one() -> std::size_t override {
        return run_batch_impl(INFINITE);
    }

    /// 非阻塞轮询
    auto poll() -> std::size_t override {
        return run_batch_impl(0);
    }

    void stop() override {
        stopped_.store(true, std::memory_order_relaxed);
        // 投递一个空完成包唤醒 GetQueuedCompletionStatus
        ::PostQueuedCompletionStatus(iocp_handle_, 0, 0, nullptr);
    }

    [[nodiscard]] auto stopped() const noexcept -> bool override {
        return stopped_.load(std::memory_order_relaxed);
    }

    void restart() override {
        stopped_.store(false, std::memory_order_relaxed);
    }

    /// 关联句柄（socket/file HANDLE）到 IOCP
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

    /// 投递用户自定义完成通知
    void post_completion(iocp_overlapped* ov) {
        ::PostQueuedCompletionStatus(
            iocp_handle_, 0, 0,
            reinterpret_cast<LPOVERLAPPED>(ov));
    }

    /// 获取原生 IOCP 句柄
    [[nodiscard]] auto native_handle() const noexcept -> HANDLE {
        return iocp_handle_;
    }

protected:
    void wake() override {
        // key=1 区分 stop(key=0)，ov=nullptr 表示 post 唤醒
        ::PostQueuedCompletionStatus(iocp_handle_, 0, 1, nullptr);
    }

private:
    static constexpr ULONG max_batch_ = 64;

    /// 核心事件分发 — 批量版
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
                    // post 唤醒信号
                    handled += drain_post_queue();
                }
                // key==0: stop 信号，忽略
                continue;
            }

            auto* iov = static_cast<iocp_overlapped*>(entry.lpOverlapped);

            // 检查错误：Internal 字段存储 NTSTATUS
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

    /// 动态加载 ntdll.dll!RtlNtStatusToDosError
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
