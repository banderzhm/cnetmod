export module cnetmod.coro.cancel;

import std;

namespace cnetmod {

// =============================================================================
// cancel_token — Async operation cancellation handle
// =============================================================================

/// Cancellable async operation handle
/// Usage:
///   cancel_token token;
///   auto t = async_read(ctx, sock, buf, token);  // Read with cancel
///   // In another coroutine or thread:
///   token.cancel();  // Request cancellation
///
/// Note: cancel_token is not copyable/movable (address stability), can be reused via reset()
export class cancel_token {
public:
    cancel_token() = default;
    ~cancel_token() = default;

    // Not copyable/movable (awaiter stores pointer to token)
    cancel_token(const cancel_token&) = delete;
    cancel_token& operator=(const cancel_token&) = delete;

    /// Request cancellation. Thread-safe, can be called from any thread.
    /// If there's a pending operation, will cancel it via platform-specific mechanism.
    /// Safe to call multiple times (only first call takes effect).
    void cancel() noexcept {
        if (cancelled_.exchange(true, std::memory_order_acq_rel))
            return;  // Already cancelled
        if (pending_.load(std::memory_order_acquire) && cancel_fn_)
            cancel_fn_(*this);
    }

    /// Whether cancellation has been requested
    [[nodiscard]] auto is_cancelled() const noexcept -> bool {
        return cancelled_.load(std::memory_order_acquire);
    }

    /// Reset token (reuse for next operation)
    /// Prerequisite: no pending operation currently
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
    // Following fields are set internally by platform awaiter, users should not manipulate directly
    // =================================================================

    std::atomic<bool> cancelled_{false};   // Whether cancellation has been requested
    std::atomic<bool> pending_{false};     // Whether an operation is pending

    /// Platform-specific cancel function (set by cancel awaiter)
    void (*cancel_fn_)(cancel_token&) noexcept = nullptr;

    void* ctx_{};              // Platform context pointer (epoll_context*/io_uring_context*)
    void* io_handle_{};        // IOCP: HANDLE (socket/file)
    void* overlapped_{};       // IOCP: OVERLAPPED* / io_uring: uring_overlapped*
    int fd_{-1};               // epoll/kqueue: file descriptor
    std::int16_t filter_{0};   // kqueue: EVFILT_READ / EVFILT_WRITE
    std::coroutine_handle<> coroutine_{};  // epoll/kqueue: suspended coroutine
};

} // namespace cnetmod
