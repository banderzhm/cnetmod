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

namespace cnetmod {

namespace detail {

struct task_group_state
{
    async_wait_group completed;
    std::mutex mutex;
    std::vector<std::shared_ptr<cancel_token>> tokens;
    std::optional<std::error_code> first_error;
    bool joining = false;
};

inline void cancel_task_group(const std::shared_ptr<task_group_state>& state,
    cancellation_reason reason)
{
    std::vector<std::shared_ptr<cancel_token>> tokens;
    {
        std::scoped_lock lock{state->mutex};
        tokens = state->tokens;
    }
    for (const auto& token : tokens)
    {
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
        std::scoped_lock lock{state->mutex};
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
        std::scoped_lock lock{state->mutex};
        if (!state->first_error)
        {
            state->first_error = std::make_error_code(std::errc::timed_out);
            first = true;
        }
    }
    if (first)
        cancel_task_group(state, cancellation_reason::deadline_exceeded);
}

inline auto run_task_group_child(std::shared_ptr<task_group_state> state,
    std::shared_ptr<cancel_token> token,
    std::function<task<std::expected<void, std::error_code>>(cancel_token&)> operation)
    -> task<void>
{
    try
    {
        auto result = co_await operation(*token);
        if (!result)
            fail_task_group(state, result.error());
    }
    catch (...)
    {
        // task_group is an error-code boundary: never let a detached child
        // terminate the process while it is being structurally joined.
        fail_task_group(state, std::make_error_code(std::errc::operation_canceled));
    }
    state->completed.done();
}

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
        : context_(&context), deadline_(value),
          state_(std::make_shared<detail::task_group_state>()) {}

    task_group(const task_group&) = delete;
    auto operator=(const task_group&) -> task_group& = delete;
    task_group(task_group&&) = delete;
    auto operator=(task_group&&) -> task_group& = delete;

    /// Starts one cancellable child. Returns false once join() has begun.
    auto run(std::function<task<std::expected<void, std::error_code>>(cancel_token&)> operation)
        -> bool
    {
        auto token = std::make_shared<cancel_token>();
        {
            std::scoped_lock lock{state_->mutex};
            if (state_->joining)
                return false;
            state_->tokens.push_back(token);
            state_->completed.add();
        }
        spawn(*context_, detail::run_task_group_child(state_, std::move(token),
            std::move(operation)));
        return true;
    }

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

    /// Waits for every started child, returning the first observed error.
    auto join() -> task<std::expected<void, std::error_code>>
    {
        {
            std::scoped_lock lock{state_->mutex};
            state_->joining = true;
        }
        if (deadline_.is_unlimited())
        {
            co_await state_->completed.wait();
        }
        else
        {
            cancel_token timer_token;
            auto wait_for_children = [state = state_, &timer_token]()
                -> task<std::expected<void, std::error_code>> {
                co_await state->completed.wait();
                timer_token.cancel();
                co_return std::expected<void, std::error_code>{};
            };
            auto watch_deadline = [state = state_, context = context_, value = deadline_,
                                      &timer_token]() -> task<int> {
                (void)co_await async_timer_wait(*context, value.remaining(), timer_token);
                if (!timer_token.is_cancelled())
                    detail::timeout_task_group(state);
                co_return 0;
            };
            auto [ignored, timer_result] = co_await when_all(wait_for_children(),
                watch_deadline());
            (void)ignored;
            (void)timer_result;
        }

        std::scoped_lock lock{state_->mutex};
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
