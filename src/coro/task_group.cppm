/// Structured concurrency for cancellable, fallible child operations.
export module cnetmod.coro.task_group;

import std;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.cancel;
import cnetmod.coro.wait_group;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.executor.async_op;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod {

namespace detail {

    struct task_group_token_slot
    {
        std::shared_ptr<cancel_token> token;
        std::size_t next_free{};
        bool active = true;
    };

    struct task_group_state
    {
        async_wait_group completed;
        // This boundary is entered from worker threads as well as coroutines.
        // Keep the short metadata critical sections project-owned and CAS based;
        // never block an I/O worker on a platform mutex.
        concurrent_containers::atomic_rw_latch latch;
        std::vector<task_group_token_slot> tokens;
        std::size_t first_free = std::numeric_limits<std::size_t>::max();
        std::optional<std::error_code> first_error;
        bool joining = false;
    };

    inline void cancel_task_group(const std::shared_ptr<task_group_state>& state,
        cancellation_reason reason)
    {
        std::size_t count{};
        {
            concurrent_containers::exclusive_latch_guard lock{state->latch};
            count = state->tokens.size();
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            std::shared_ptr<cancel_token> token;
            {
                concurrent_containers::exclusive_latch_guard lock{state->latch};
                if (state->tokens[index].active)
                    token = state->tokens[index].token;
            }
            if (!token)
                continue;
            if (reason == cancellation_reason::deadline_exceeded)
                token->cancel_due_to_deadline();
            else
                token->cancel();
        }
    }

    inline void fail_task_group(const std::shared_ptr<task_group_state>& state,
        std::error_code error)
    {
        bool first = false;
        {
            concurrent_containers::exclusive_latch_guard lock{state->latch};
            if (!state->first_error)
            {
                state->first_error = error;
                first = true;
            }
        }
        if (first)
            cancel_task_group(state, cancellation_reason::caller_cancelled);
    }

    inline void timeout_task_group(const std::shared_ptr<task_group_state>& state)
    {
        bool first = false;
        {
            concurrent_containers::exclusive_latch_guard lock{state->latch};
            if (!state->first_error)
            {
                state->first_error = std::make_error_code(std::errc::timed_out);
                first = true;
            }
        }
        if (first)
            cancel_task_group(state, cancellation_reason::deadline_exceeded);
    }

    void retire_task_group_child(
        const std::shared_ptr<task_group_state>& state, std::size_t slot) noexcept;

    auto run_task_group_child(std::shared_ptr<task_group_state> state,
        std::shared_ptr<cancel_token> token,
        std::size_t slot,
        std::function<task<std::expected<void, std::error_code>>(cancel_token&)> operation)
        -> task<void>;

} // namespace detail

/// Owns a bounded lifetime for a fan-out of cancellable operations.
///
/// Every child receives a distinct cancel_token because one token cannot safely
/// back multiple concurrent platform I/O awaiters. The first failed child
/// cancels its siblings; join() always waits for those siblings to unwind.
export class task_group final
{
public:
    explicit task_group(io_context& context)
        : context_(&context), state_(std::make_shared<detail::task_group_state>()) {}

    task_group(io_context& context, deadline value)
        : context_(&context), deadline_(value), state_(std::make_shared<detail::task_group_state>()) {}

    task_group(const task_group&) = delete;
    auto operator=(const task_group&) -> task_group& = delete;
    task_group(task_group&&) = delete;
    auto operator=(task_group&&) -> task_group& = delete;

    /// Starts one cancellable child. Returns false once join() has begun.
    auto run(std::function<task<std::expected<void, std::error_code>>(cancel_token&)> operation)
        -> bool;

    /// Requests explicit caller cancellation for every child.
    void cancel() noexcept
    {
        detail::cancel_task_group(state_, cancellation_reason::caller_cancelled);
    }

    /// Requests deadline cancellation for every child.
    void cancel_due_to_deadline() noexcept
    {
        detail::cancel_task_group(state_, cancellation_reason::deadline_exceeded);
    }

    /**
     * @brief Seals the group and waits without allocating a coroutine frame.
     *
     * Emergency cleanup must request cancellation first. This awaitable only
     * joins existing children; it does not create a deadline watcher.
     */
    [[nodiscard]] auto settle() noexcept -> async_wait_group::wait_awaitable
    {
        {
            concurrent_containers::exclusive_latch_guard lock{state_->latch};
            state_->joining = true;
        }
        return state_->completed.wait();
    }

    /**
     * @brief Returns the completion result without allocating or suspending.
     * An empty optional means children are still running; a zero error code
     * means successful completion. This snapshot does not seal registration.
     * Owners must prevent new run() calls before using it to retire a group.
     */
    [[nodiscard]] auto completion_result() const noexcept -> std::optional<std::error_code>
    {
        concurrent_containers::exclusive_latch_guard lock{state_->latch};
        if (state_->completed.count() != 0)
            return std::nullopt;
        return state_->first_error.value_or(std::error_code{});
    }

    /// Waits for every started child, returning the first observed error.
    auto join() -> task<std::expected<void, std::error_code>>
    {
        {
            concurrent_containers::exclusive_latch_guard lock{state_->latch};
            state_->joining = true;
        }
        if (deadline_.is_unlimited())
        {
            co_await state_->completed.wait();
        }
        else
        {
            std::error_code setup_error;
            try
            {
                cancel_token timer_token;
                auto wait_for_children = [state = state_, &timer_token]()
                    -> task<std::expected<void, std::error_code>>
                {
                    co_await state->completed.wait();
                    timer_token.cancel();
                    co_return std::expected<void, std::error_code>{};
                };
                auto watch_deadline = [state = state_, context = context_, value = deadline_,
                                          &timer_token]() -> task<int>
                {
                    try
                    {
                        const auto waited = co_await async_timer_wait(*context, value.remaining(), timer_token);
                        if (!timer_token.is_cancelled())
                        {
                            if (waited)
                                detail::timeout_task_group(state);
                            else
                                detail::fail_task_group(state, waited.error());
                        }
                    }
                    catch (const std::bad_alloc&)
                    {
                        detail::fail_task_group(state, std::make_error_code(std::errc::not_enough_memory));
                    }
                    catch (...)
                    {
                        detail::fail_task_group(state, std::make_error_code(std::errc::io_error));
                    }
                    co_return 0;
                };
                auto [ignored, timer_result] = co_await when_all(wait_for_children(),
                    watch_deadline());
                (void)ignored;
                (void)timer_result;
            }
            catch (const std::bad_alloc&)
            {
                setup_error = std::make_error_code(std::errc::not_enough_memory);
            }
            catch (...)
            {
                setup_error = std::make_error_code(std::errc::io_error);
            }
            if (setup_error)
            {
                detail::fail_task_group(state_, setup_error);
                co_await state_->completed.wait();
            }
        }

        concurrent_containers::exclusive_latch_guard lock{state_->latch};
        if (state_->first_error)
            co_return std::unexpected(*state_->first_error);
        co_return std::expected<void, std::error_code>{};
    }

private:
    io_context* context_;
    deadline deadline_{};
    std::shared_ptr<detail::task_group_state> state_;
};

} // namespace cnetmod
