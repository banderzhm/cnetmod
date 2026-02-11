export module cnetmod.coro.cancel;

import std;

namespace cnetmod {

// =============================================================================
// cancel_token — 异步操作取消句柄
// =============================================================================

/// 可取消的异步操作句柄
/// 用法：
///   cancel_token token;
///   auto t = async_read(ctx, sock, buf, token);  // 带 cancel 的读
///   // 另一个协程或线程中：
///   token.cancel();  // 请求取消
///
/// 注意：cancel_token 不可复制/移动（地址稳定性），可通过 reset() 复用
export class cancel_token {
public:
    cancel_token() = default;
    ~cancel_token() = default;

    // 不可复制/移动（awaiter 存储了指向 token 的指针）
    cancel_token(const cancel_token&) = delete;
    cancel_token& operator=(const cancel_token&) = delete;

    /// 请求取消。线程安全，可从任意线程调用。
    /// 如果有挂起的操作，会通过平台特定机制取消它。
    /// 多次调用安全（仅首次生效）。
    void cancel() noexcept {
        if (cancelled_.exchange(true, std::memory_order_acq_rel))
            return;  // 已取消
        if (pending_.load(std::memory_order_acquire) && cancel_fn_)
            cancel_fn_(*this);
    }

    /// 是否已请求取消
    [[nodiscard]] auto is_cancelled() const noexcept -> bool {
        return cancelled_.load(std::memory_order_acquire);
    }

    /// 重置 token（复用于下一个操作）
    /// 前提：当前没有挂起的操作
    void reset() noexcept {
        cancelled_.store(false, std::memory_order_relaxed);
        pending_.store(false, std::memory_order_relaxed);
        cancel_fn_ = nullptr;
        ctx_ = nullptr;
        io_handle_ = nullptr;
        overlapped_ = nullptr;
        fd_ = -1;
        filter_ = 0;
        coroutine_ = {};
    }

    // =================================================================
    // 以下字段由平台 awaiter 内部设置，用户不应直接操作
    // =================================================================

    std::atomic<bool> cancelled_{false};   // 是否已请求取消
    std::atomic<bool> pending_{false};     // 是否有操作正在挂起

    /// 平台特定取消函数（由 cancel awaiter 设置）
    void (*cancel_fn_)(cancel_token&) noexcept = nullptr;

    void* ctx_{};              // 平台 context 指针（epoll_context*/io_uring_context*）
    void* io_handle_{};        // IOCP: HANDLE（socket/file）
    void* overlapped_{};       // IOCP: OVERLAPPED* / io_uring: uring_overlapped*
    int fd_{-1};               // epoll/kqueue: 文件描述符
    std::int16_t filter_{0};   // kqueue: EVFILT_READ / EVFILT_WRITE
    std::coroutine_handle<> coroutine_{};  // epoll/kqueue: 挂起的协程
};

} // namespace cnetmod
