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
            run_one_impl(INFINITE);
        }
    }

    /// 处理一个完成事件（阻塞）
    auto run_one() -> std::size_t override {
        return run_one_impl(INFINITE);
    }

    /// 非阻塞轮询
    auto poll() -> std::size_t override {
        return run_one_impl(0);
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
    /// 核心事件分发
    auto run_one_impl(DWORD timeout_ms) -> std::size_t {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED ov = nullptr;

        BOOL ok = ::GetQueuedCompletionStatus(
            iocp_handle_, &bytes, &key, &ov, timeout_ms);

        if (ov == nullptr) {
            if (key == 1) {
                // post 唤醒信号 — 排空并恢复已投递的协程
                return drain_post_queue();
            }
            // 超时或 stop 信号
            return 0;
        }

        auto* iov = static_cast<iocp_overlapped*>(ov);

        if (!ok) {
            iov->error = std::error_code(
                static_cast<int>(::GetLastError()),
                std::system_category());
        }
        iov->bytes_transferred = bytes;

        // 恢复等待此操作的协程
        if (iov->coroutine)
            iov->coroutine.resume();

        return 1;
    }

    HANDLE iocp_handle_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_IOCP
