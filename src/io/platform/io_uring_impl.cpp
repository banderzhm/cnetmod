module;

#include <cnetmod/config.hpp>

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

module cnetmod.io.platform.io_uring;

import std;
import cnetmod.core.log;

namespace cnetmod {

io_uring_context::io_uring_context(unsigned queue_depth)
    : instance_id_(next_instance_id_.fetch_add(1, std::memory_order_relaxed))
{
    submit_batch_size_ = submit_batch_size_from_env();
    cqe_tick_limit_ = cqe_tick_limit_from_env();
    int ret = 0;
#ifdef IORING_SETUP_CQSIZE
    ::io_uring_params params{};
    params.flags = IORING_SETUP_CQSIZE;
    params.cq_entries = queue_depth * 2;
    #ifdef IORING_SETUP_COOP_TASKRUN
    const auto coop_taskrun =
        env_enabled("CNETMOD_IOURING_COOP_TASKRUN", true);
    if (coop_taskrun)
        params.flags |= IORING_SETUP_COOP_TASKRUN;
    #endif
    ret = ::io_uring_queue_init_params(queue_depth, &ring_, &params);
    if (ret == -EINVAL)
    {
    #ifdef IORING_SETUP_COOP_TASKRUN
        if (coop_taskrun)
        {
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
        throw std::system_error(-ret, std::generic_category(),
            "io_uring_queue_init failed");
    initialized_ = true;
    if (::pipe2(pipe_fds_, O_NONBLOCK | O_CLOEXEC) < 0)
    {
        ::io_uring_queue_exit(&ring_);
        throw std::system_error(errno, std::generic_category(), "pipe2 failed");
    }
    submit_wake_read();
}

io_uring_context::~io_uring_context()
{
    if (pipe_fds_[0] >= 0)
        ::close(pipe_fds_[0]);
    if (pipe_fds_[1] >= 0)
        ::close(pipe_fds_[1]);
    if (initialized_)
        ::io_uring_queue_exit(&ring_);
}

void io_uring_context::run()
{
    execution_scope executing{*this};
    while (!stopped_.load(std::memory_order_relaxed))
        run_one_impl(true);
}

auto io_uring_context::run_one() -> std::size_t
{
    execution_scope executing{*this};
    return run_one_impl(true);
}

auto io_uring_context::poll() -> std::size_t
{
    execution_scope executing{*this};
    return run_one_impl(false);
}

void io_uring_context::stop()
{
    stopped_.store(true, std::memory_order_relaxed);
    // Never touch the ring from the stopping thread: stop() is callable from
    // outside the reactor, and a future SINGLE_ISSUER ring rejects foreign
    // submissions. The wake pipe is safe from any thread and the loop
    // re-checks stopped() after every wakeup.
    wake();
}

auto io_uring_context::stopped() const noexcept -> bool
{
    return stopped_.load(std::memory_order_relaxed);
}

auto io_uring_context::instance_id() const noexcept -> std::uint64_t
{
    return instance_id_;
}

auto io_uring_context::native_ring() noexcept -> ::io_uring*
{
    return &ring_;
}

void io_uring_context::restart()
{
    stopped_.store(false, std::memory_order_relaxed);
}

auto io_uring_context::get_sqe() -> ::io_uring_sqe*
{
    return ::io_uring_get_sqe(&ring_);
}

auto io_uring_context::prepare_sqe() -> ::io_uring_sqe*
{
    auto* sqe = ::io_uring_get_sqe(&ring_);
    if (sqe)
        ++pending_sqes_;
    return sqe;
}

auto io_uring_context::flush() -> std::expected<int, std::error_code>
{
    if (pending_sqes_ == 0 || pending_sqes_ < submit_batch_size_)
        return 0;
    const auto result = submit_pending();
    return result ? *result : 0;
}

auto io_uring_context::flush_pending() -> std::expected<int, std::error_code>
{
    if (pending_sqes_ == 0)
        return 0;
    const auto result = submit_pending();
    return result ? *result : 0;
}

auto io_uring_context::submit_batch_size() const noexcept -> unsigned
{
    return submit_batch_size_;
}

auto io_uring_context::submit() -> std::expected<int, std::error_code>
{
    return submit_pending();
}

auto io_uring_context::submission_error() const noexcept -> std::error_code
{
    return submission_error_;
}

void io_uring_context::enqueue_cancel(uring_cancel_request& request)
{
    request.next = nullptr;
    if (cancel_tail_)
        cancel_tail_->next = &request;
    else
        cancel_head_ = &request;
    cancel_tail_ = &request;
    drain_cancellations();
}

void io_uring_context::drain_cancellations()
{
    while (cancel_head_)
    {
        auto* sqe = prepare_sqe();
        if (!sqe)
        {
            // Retain the request if submission cannot make room yet. The
            // reactor retries before waiting, including after transient errors.
            if (auto result = submit_pending(); !result)
                return;
            sqe = prepare_sqe();
            if (!sqe)
                return;
        }
        auto* request = cancel_head_;
        cancel_head_ = request->next;
        if (!cancel_head_)
            cancel_tail_ = nullptr;
        request->next = nullptr;
        ::io_uring_prep_cancel(sqe, request->target, 0);
        ::io_uring_sqe_set_data(sqe, &request->operation);
    }
    // Prepared cancellations must not wait for the normal submission threshold.
    if (pending_sqes_ != 0)
        (void)submit_pending();
}

void io_uring_context::wake()
{
    const char c = 1;
    (void)::write(pipe_fds_[1], &c, 1);
}

auto io_uring_context::submit_batch_size_from_env() noexcept -> unsigned
{
    const auto* text = std::getenv("CNETMOD_IOURING_SUBMIT_BATCH");
    if (!text || *text == '\0')
        return default_submit_batch_size_;
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (*end != '\0' || value == 0 || value > max_submit_batch_size_)
        return default_submit_batch_size_;
    return static_cast<unsigned>(value);
}

auto io_uring_context::env_enabled(
    const char* name, bool default_value) noexcept -> bool
{
    const auto* value = std::getenv(name);
    if (!value || value[0] == '\0')
        return default_value;
    return value[0] != '0';
}

auto io_uring_context::cqe_tick_limit_from_env() noexcept -> unsigned
{
    const auto* text = std::getenv("CNETMOD_IOURING_CQE_TICK_LIMIT");
    if (!text || *text == '\0')
        return default_cqe_tick_limit_;
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (*end != '\0' || value < max_cqe_batch_ || value > max_cqe_tick_limit_)
        return default_cqe_tick_limit_;
    return static_cast<unsigned>(value);
}

auto io_uring_context::submit_pending() -> std::expected<int, std::error_code>
{
    const int ret = ::io_uring_submit(&ring_);
    // This explicit submission includes raw get_sqe() entries. Raw acquisition
    // alone does not register them for automatic submission. Retain partially
    // submitted entries and failures once submission has been requested.
    pending_sqes_ = ::io_uring_sq_ready(&ring_);
    if (ret < 0)
    {
        const auto error = std::error_code(-ret, std::generic_category());
        submission_error_ = error;
        return std::unexpected(error);
    }
    submission_error_.clear();
    return ret;
}

auto io_uring_context::run_one_impl(bool blocking) -> std::size_t
{
    if (cancel_head_)
        drain_cancellations();
    if (pending_sqes_ > 0)
        (void)submit_pending();
    if (!wake_read_pending_)
        submit_wake_read();
    const bool fatal_submission_error = submission_error_ &&
        submission_error_.value() != EINTR &&
        submission_error_.value() != EAGAIN &&
        submission_error_.value() != EBUSY;
    // A broken ring cannot safely retire prepared operations. Report the error
    // and return control instead of spinning indefinitely. Stopping is not a
    // completion/join boundary: callers must retain pending frames and buffers.
    // After external repair, restart() permits another submission attempt.
    if (fatal_submission_error &&
        !stopped_.exchange(true, std::memory_order_relaxed))
        logger::error{"io_uring submission failed: {} ({}); reactor stopped with queued operations retained",
            submission_error_.message(), submission_error_.value()};
    // Blocking here after an unsuccessful submission could strand the only
    // operation capable of cancelling a blocked send. Keep retrying on ticks.
    if (blocking && !fatal_submission_error && !cancel_head_ && pending_sqes_ == 0)
    {
        ::io_uring_cqe* cqe = nullptr;
        if (::io_uring_wait_cqe(&ring_, &cqe) < 0)
            return 0;
    }
    std::size_t handled = 0;
    unsigned reaped = 0;
    while (reaped < cqe_tick_limit_)
    {
        ::io_uring_cqe* cqes[max_cqe_batch_];
        const auto limit = std::min(max_cqe_batch_, cqe_tick_limit_ - reaped);
        const unsigned n = ::io_uring_peek_batch_cqe(&ring_, cqes, limit);
        if (n == 0)
            break;

        struct completion_record
        {
            uring_overlapped* overlapped;
            int32_t result;
            std::uint32_t flags;
        };

        completion_record completions[max_cqe_batch_];
        for (unsigned i = 0; i < n; ++i)
            completions[i] = {
                static_cast<uring_overlapped*>(::io_uring_cqe_get_data(cqes[i])),
                cqes[i]->res, cqes[i]->flags};
        ::io_uring_cq_advance(&ring_, n);
        reaped += n;
        for (unsigned i = 0; i < n; ++i)
        {
            auto* ov = completions[i].overlapped;
            const auto result = completions[i].result;
            if (!ov)
                continue;
            if (ov == &wake_ov_)
            {
                wake_read_pending_ = false;
                char buf[64];
                while (::read(pipe_fds_[0], buf, sizeof(buf)) > 0)
                {
                }
                submit_wake_read();
                handled += drain_post_queue();
                continue;
            }
            if (ov->completion)
                ov->completion(*ov, result, completions[i].flags);
            else
            {
                ov->result = result;
                if (ov->coroutine)
                    ov->coroutine.resume();
            }
            ++handled;
        }
    }
    return handled;
}

void io_uring_context::submit_wake_read()
{
    if (auto* sqe = prepare_sqe())
    {
        wake_read_pending_ = true;
        ::io_uring_prep_read(sqe, pipe_fds_[0], &pipe_buf_, 1, 0);
        ::io_uring_sqe_set_data(sqe, &wake_ov_);
        (void)flush_pending();
    }
}

} // namespace cnetmod
