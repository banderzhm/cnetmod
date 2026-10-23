module;

#include <cnetmod/config.hpp>

#include <liburing.h>

export module cnetmod.io.platform.io_uring;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

export struct uring_overlapped
{
    std::coroutine_handle<> coroutine{};
    int32_t result = 0;
    std::uint16_t buffer_id = 0;
    void (*completion)(uring_overlapped&, int32_t, std::uint32_t) = nullptr;
    void* completion_context = nullptr;
};

/// Caller-owned cancellation state; retain it until its completion callback.
export struct uring_cancel_request
{
    uring_overlapped operation{};
    uring_overlapped* target{};
    uring_cancel_request* next{};
};

/// Linux io_uring event loop.  The interface keeps the state layout only;
/// platform and syscall logic lives in io_uring_impl.cpp.
export class io_uring_context : public io_context
{
public:
    explicit io_uring_context(unsigned queue_depth = 256);
    ~io_uring_context() override;

    void run() override;
    [[nodiscard]] auto run_one() -> std::size_t override;
    [[nodiscard]] auto poll() -> std::size_t override;
    void stop() override;
    [[nodiscard]] auto stopped() const noexcept -> bool override;
    [[nodiscard]] auto instance_id() const noexcept -> std::uint64_t;
    [[nodiscard]] auto ring_initialized() const noexcept -> bool;
    /// Retain a resource whose kernel registration belongs to this ring.
    /// Call only from the thread currently driving this context. Resources
    /// are released after io_uring_queue_exit(), so their destructors never
    /// unregister ring state from a foreign thread.
    void retain_owner_resource(std::shared_ptr<void> resource);
    [[nodiscard]] auto native_ring() noexcept -> ::io_uring*;
    void restart() override;
    [[nodiscard]] auto get_sqe() -> ::io_uring_sqe*;
    [[nodiscard]] auto prepare_sqe() -> ::io_uring_sqe*;
    /// Prepared operations retain their buffers until CQE completion. Submission
    /// failures are deferred (zero submitted); inspect submission_error().
    [[nodiscard]] auto flush() -> std::expected<int, std::error_code>;
    [[nodiscard]] auto flush_pending() -> std::expected<int, std::error_code>;
    [[nodiscard]] auto submit_batch_size() const noexcept -> unsigned;
    /// Explicit synchronous submission reports errors without retiring SQEs.
    /// The caller must retain every queued operation even when this fails.
    [[nodiscard]] auto submit() -> std::expected<int, std::error_code>;
    /// Owner-thread diagnostic, cleared by successful submission. Fatal errors
    /// stop the reactor without completing queued operations: stopped() does not
    /// permit destroying their frames/buffers. Keep them alive through external
    /// ring repair, restart and completion draining, or the context's teardown
    /// contract. No general fatal-ring recovery is provided here.
    [[nodiscard]] auto submission_error() const noexcept -> std::error_code;
    /// Owner-thread only. SQ pressure never discards a cancellation request.
    void enqueue_cancel(uring_cancel_request& request);

protected:
    void wake() override;

private:
    static constexpr unsigned max_cqe_batch_ = 64;
    static constexpr unsigned default_cqe_tick_limit_ = 256;
    static constexpr unsigned max_cqe_tick_limit_ = 4096;
    static constexpr unsigned default_submit_batch_size_ = 8;
    static constexpr unsigned max_submit_batch_size_ = 128;

    static auto submit_batch_size_from_env() noexcept -> unsigned;
    static auto env_enabled(const char* name, bool default_value) noexcept -> bool;
    static auto cqe_tick_limit_from_env() noexcept -> unsigned;
    /// Create the ring on its driving thread. SINGLE_ISSUER designates the
    /// creating task as the only legal submitter.
    void ensure_ring();
    [[nodiscard]] auto submit_pending() -> std::expected<int, std::error_code>;
    auto run_one_impl(bool blocking) -> std::size_t;
    void submit_wake_read();
    void drain_cancellations();

    ::io_uring ring_{};
    std::atomic<bool> stopped_{false};
    bool initialized_ = false;
    unsigned queue_depth_ = 256;
    int pipe_fds_[2] = {-1, -1};
    char pipe_buf_ = 0;
    uring_overlapped wake_ov_{};
    bool wake_read_pending_ = false;
    unsigned pending_sqes_ = 0;
    std::error_code submission_error_{};
    uring_cancel_request* cancel_head_{};
    uring_cancel_request* cancel_tail_{};
    unsigned submit_batch_size_ = default_submit_batch_size_;
    unsigned cqe_tick_limit_ = default_cqe_tick_limit_;
    std::vector<std::shared_ptr<void>> owner_resources_;
    inline static std::atomic<std::uint64_t> next_instance_id_{0};
    std::uint64_t instance_id_ = 0;
};

} // namespace cnetmod
