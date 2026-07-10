module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING

#include <liburing.h>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>

export module cnetmod.io.platform.io_uring;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

// =============================================================================
// io_uring coroutine operation base class
// =============================================================================

/// io_uring completion info (similar to IOCP's iocp_overlapped)
/// Create one for each async operation, set as SQE user_data, resume coroutine after CQE completion
export struct uring_overlapped {
    std::coroutine_handle<> coroutine{};  // Coroutine to resume after completion
    int32_t result = 0;                   // CQE result (>=0 success bytes, <0 negated error code)
    std::uint16_t buffer_id = 0;          // Selected provided-buffer ID, when applicable
    // Persistent operations (for example multishot accept) consume CQEs themselves.
    void (*completion)(uring_overlapped&, int32_t, std::uint32_t) = nullptr;
    void* completion_context = nullptr;
};

// =============================================================================
// io_uring Context implementation
// =============================================================================

/// Linux io_uring implementation
export class io_uring_context : public io_context {
public:
    explicit io_uring_context(unsigned queue_depth = 256) {
        submit_batch_size_ = submit_batch_size_from_env();
        cqe_tick_limit_ = cqe_tick_limit_from_env();
        int ret = 0;
#ifdef IORING_SETUP_CQSIZE
        ::io_uring_params params{};
        params.flags = IORING_SETUP_CQSIZE;
        params.cq_entries = queue_depth * 2;
#ifdef IORING_SETUP_COOP_TASKRUN
        const auto coop_taskrun = env_enabled("CNETMOD_IOURING_COOP_TASKRUN");
        if (coop_taskrun)
            params.flags |= IORING_SETUP_COOP_TASKRUN;
#endif
        ret = ::io_uring_queue_init_params(queue_depth, &ring_, &params);

        // CQSIZE is an optional setup feature.  The ordinary ring remains a
        // valid compatibility fallback for old kernels or restricted hosts.
        if (ret == -EINVAL) {
#ifdef IORING_SETUP_COOP_TASKRUN
            if (coop_taskrun) {
                params.flags = IORING_SETUP_CQSIZE;
                ret = ::io_uring_queue_init_params(queue_depth, &ring_, &params);
            }
#endif
        }
        if (ret == -EINVAL)
            ret = ::io_uring_queue_init(queue_depth, &ring_, 0);
#else
        ret = ::io_uring_queue_init(queue_depth, &ring_, 0);
#endif
        if (ret < 0)
            throw std::system_error(
                -ret, std::generic_category(),
                "io_uring_queue_init failed");
        initialized_ = true;

        // Create non-blocking pipe for post() wake
        if (::pipe2(pipe_fds_, O_NONBLOCK | O_CLOEXEC) < 0) {
            ::io_uring_queue_exit(&ring_);
            throw std::system_error(
                errno, std::generic_category(), "pipe2 failed");
        }
        // Submit read on pipe read end, use wake_ov_ as sentinel
        submit_wake_read();
    }

    ~io_uring_context() override {
        if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
        if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
        if (initialized_)
            ::io_uring_queue_exit(&ring_);
    }

    void run() override {
        while (!stopped_.load(std::memory_order_relaxed)) {
            run_one_impl(true);
        }
    }

    auto run_one() -> std::size_t override {
        return run_one_impl(true);
    }

    auto poll() -> std::size_t override {
        return run_one_impl(false);
    }

    void stop() override {
        stopped_.store(true, std::memory_order_relaxed);
        // Submit a NOP to wake waiting io_uring_wait_cqe
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe) {
            ::io_uring_prep_nop(sqe);
            ::io_uring_sqe_set_data(sqe, nullptr);  // nullptr = stop signal
            ::io_uring_submit(&ring_);
        }
    }

    [[nodiscard]] auto stopped() const noexcept -> bool override {
        return stopped_.load(std::memory_order_relaxed);
    }

    /// Stable identity used by platform-private persistent operation state.
    [[nodiscard]] auto instance_id() const noexcept -> std::uint64_t {
        return instance_id_;
    }

    /// Native ring access for platform-private extensions such as buffer rings.
    [[nodiscard]] auto native_ring() noexcept -> ::io_uring* {
        return &ring_;
    }

    void restart() override {
        stopped_.store(false, std::memory_order_relaxed);
    }

    /// Get SQE and submit immediately (compatible with old interface)
    [[nodiscard]] auto get_sqe() -> ::io_uring_sqe* {
        return ::io_uring_get_sqe(&ring_);
    }

    /// Get SQE but don't submit (deferred submission mode)
    /// After caller fills SQE, run loop will auto flush in next round
    [[nodiscard]] auto prepare_sqe() -> ::io_uring_sqe* {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe)
            ++pending_sqes_;
        return sqe;
    }

    /// Submit when the bounded batch is full.  The event loop always flushes
    /// any remaining work before it waits for completions.
    [[nodiscard]] auto flush() -> std::expected<int, std::error_code> {
        if (pending_sqes_ == 0)
            return 0;
        if (pending_sqes_ < submit_batch_size_)
            return 0;
        return submit_pending();
    }

    /// Force submission of all pending SQEs.
    [[nodiscard]] auto flush_pending() -> std::expected<int, std::error_code> {
        if (pending_sqes_ == 0)
            return 0;
        return submit_pending();
    }

    [[nodiscard]] auto submit_batch_size() const noexcept -> unsigned {
        return submit_batch_size_;
    }

    /// Submit I/O request immediately (compatible with old interface)
    [[nodiscard]] auto submit() -> std::expected<int, std::error_code> {
        pending_sqes_ = 0;
        int ret = ::io_uring_submit(&ring_);
        if (ret < 0)
            return std::unexpected(
                std::error_code(-ret, std::generic_category()));
        return ret;
    }

protected:
    void wake() override {
        char c = 1;
        ::write(pipe_fds_[1], &c, 1);
    }

private:
    static constexpr unsigned max_cqe_batch_ = 64;
    static constexpr unsigned default_cqe_tick_limit_ = 256;
    static constexpr unsigned max_cqe_tick_limit_ = 4096;
    // Immediate submission is the safe default.  Larger values are an opt-in
    // tuning knob because cross-context request/response traffic is latency
    // sensitive and may never fill a batch.
    static constexpr unsigned default_submit_batch_size_ = 1;
    static constexpr unsigned max_submit_batch_size_ = 128;

    static auto submit_batch_size_from_env() noexcept -> unsigned {
        const auto* text = std::getenv("CNETMOD_IOURING_SUBMIT_BATCH");
        if (!text || *text == '\0')
            return default_submit_batch_size_;
        char* end = nullptr;
        const auto value = std::strtoul(text, &end, 10);
        if (*end != '\0' || value == 0 || value > max_submit_batch_size_)
            return default_submit_batch_size_;
        return static_cast<unsigned>(value);
    }

    static auto env_enabled(const char* name) noexcept -> bool {
        const auto* value = std::getenv(name);
        return value && value[0] != '\0' && value[0] != '0';
    }

    static auto cqe_tick_limit_from_env() noexcept -> unsigned {
        const auto* text = std::getenv("CNETMOD_IOURING_CQE_TICK_LIMIT");
        if (!text || *text == '\0')
            return default_cqe_tick_limit_;
        char* end = nullptr;
        const auto value = std::strtoul(text, &end, 10);
        if (*end != '\0' || value < max_cqe_batch_ ||
            value > max_cqe_tick_limit_)
            return default_cqe_tick_limit_;
        return static_cast<unsigned>(value);
    }

    [[nodiscard]] auto submit_pending() -> std::expected<int, std::error_code> {
        int ret = ::io_uring_submit(&ring_);
        if (ret < 0)
            return std::unexpected(
                std::error_code(-ret, std::generic_category()));
        pending_sqes_ = 0;
        return ret;
    }

    auto run_one_impl(bool blocking) -> std::size_t {
        // First flush pending SQEs
        if (pending_sqes_ > 0) {
            (void)submit_pending();
        }

        // Block waiting for at least one CQE
        if (blocking) {
            ::io_uring_cqe* cqe = nullptr;
            int ret = ::io_uring_wait_cqe(&ring_, &cqe);
            if (ret < 0)
                return 0;
        }

        std::size_t handled = 0;
        unsigned reaped = 0;
        while (reaped < cqe_tick_limit_) {
            // CQ space must be released before any completion resumes a
            // coroutine: resumption may immediately submit another SQE.
            ::io_uring_cqe* cqes[max_cqe_batch_];
            const auto limit = std::min(max_cqe_batch_, cqe_tick_limit_ - reaped);
            const unsigned n = ::io_uring_peek_batch_cqe(&ring_, cqes, limit);
            if (n == 0)
                break;

            struct completion_record {
                uring_overlapped* overlapped;
                int32_t result;
                std::uint32_t flags;
            };
            completion_record completions[max_cqe_batch_];
            for (unsigned i = 0; i < n; ++i) {
                completions[i] = {
                    static_cast<uring_overlapped*>(::io_uring_cqe_get_data(cqes[i])),
                    cqes[i]->res,
                    cqes[i]->flags,
                };
            }

            ::io_uring_cq_advance(&ring_, n);
            reaped += n;

            for (unsigned i = 0; i < n; ++i) {
                auto* ov = completions[i].overlapped;
                auto result = completions[i].result;

                if (ov == nullptr) {
                    // NOP (stop wake signal)
                    continue;
                }

                if (ov == &wake_ov_) {
                    // Pipe wake sentinel — drain post queue and re-register pipe read
                    char buf[64];
                    while (::read(pipe_fds_[0], buf, sizeof(buf)) > 0) {}
                    submit_wake_read();
                    handled += drain_post_queue();
                    continue;
                }

                if (ov->completion) {
                    ov->completion(*ov, result, completions[i].flags);
                } else {
                    ov->result = result;
                    if (ov->coroutine)
                        ov->coroutine.resume();
                }
                ++handled;
            }
        }
        return handled;
    }

    void submit_wake_read() {
        auto* sqe = prepare_sqe();
        if (sqe) {
            ::io_uring_prep_read(sqe, pipe_fds_[0], &pipe_buf_, 1, 0);
            ::io_uring_sqe_set_data(sqe, &wake_ov_);
            (void)flush_pending();
        }
    }

    ::io_uring ring_{};
    std::atomic<bool> stopped_{false};
    bool initialized_ = false;
    int pipe_fds_[2] = {-1, -1};
    char pipe_buf_ = 0;
    uring_overlapped wake_ov_{};  // Sentinel, no coroutine stored
    unsigned pending_sqes_ = 0;   // Pending SQE count
    unsigned submit_batch_size_ = default_submit_batch_size_;
    unsigned cqe_tick_limit_ = default_cqe_tick_limit_;
    inline static std::atomic<std::uint64_t> next_instance_id_{0};
    std::uint64_t instance_id_ = next_instance_id_.fetch_add(1, std::memory_order_relaxed);
};

} // namespace cnetmod

#endif // CNETMOD_HAS_IO_URING
