module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.spawn;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

namespace detail {

    /**
     * @brief Contains diagnostic failures outside the dispatch coroutine frame.
     */
    template <typename ErrorHandler>
    void report_spawn_failure(ErrorHandler& report) noexcept
    {
        try
        {
            std::invoke(report, std::current_exception());
        }
        catch (...)
        {
        }
    }

    template <auto OnError>
    void report_static_spawn_failure() noexcept
    {
        auto report = OnError;
        report_spawn_failure(report);
    }

    struct detached_post_state
    {
        std::coroutine_handle<> coroutine;
    };

    inline void resume_detached_post(void* raw) noexcept
    {
        std::unique_ptr<detached_post_state> state{
            static_cast<detached_post_state*>(raw)};
        const auto coroutine = state->coroutine;
        coroutine.resume();
    }

    inline void discard_detached_post(void* raw) noexcept
    {
        std::unique_ptr<detached_post_state> state{
            static_cast<detached_post_state*>(raw)};
        if (state->coroutine)
            state->coroutine.destroy();
    }

    struct detached_post_awaitable
    {
        io_context& ctx;

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> coroutine)
        {
            auto state = std::make_unique<detached_post_state>(
                detached_post_state{.coroutine = coroutine});
            ctx.post(&resume_detached_post, state.get(), &discard_detached_post);
            (void)state.release();
        }

        void await_resume() const noexcept {}
    };

} // namespace detail

// =============================================================================
// detached_task — Fire-and-forget coroutine type
// =============================================================================

/// Executes immediately after start, auto-destroys frame on completion, no external lifetime management needed
struct detached_task
{
    struct promise_type
    {
        auto get_return_object() noexcept -> detached_task
        {
            return {};
        }

        auto initial_suspend() noexcept -> std::suspend_never
        {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept
        {
            std::terminate();
        }

        auto final_suspend() noexcept -> std::suspend_never
        {
            return {};
        }
    };
};

// =============================================================================
// spawn — Start fire-and-forget coroutine on io_context
// =============================================================================

/// Post a fire-and-forget task to an io_context.
/// Kept in the interface unit: current MSVC module codegen emits an invalid
/// COFF object when this coroutine's detached promise lives in a .cpp unit.
export void spawn(io_context& ctx, task<void> task_to_run)
{
    [](io_context& context, task<void> inner) -> detached_task
    {
        co_await detail::detached_post_awaitable{context};
        co_await std::move(inner);
    }(ctx, std::move(task_to_run));
}

/**
 * @brief Dispatches a background task with an explicit failure observer.
 *
 * Exceptions from posting or executing the task are reported once. Exceptions
 * from the observer are contained. Allocation before the wrapper coroutine
 * starts may throw to the caller. The existing spawn() contract is unchanged.
 * This template remains visible for handler instantiation and MSVC coroutine
 * code generation, like the existing detached spawn implementation above.
 */
export template <typename ErrorHandler>
void spawn_guarded(io_context& ctx, task<void> task_to_run, ErrorHandler on_error)
{
    [](io_context& context, task<void> inner, ErrorHandler report) -> detached_task
    {
        try
        {
            co_await detail::detached_post_awaitable{context};
            co_await std::move(inner);
        }
        catch (...)
        {
            detail::report_spawn_failure(report);
        }
    }(ctx, std::move(task_to_run), std::move(on_error));
}

/**
 * @brief Dispatches with a compile-time observer and no runtime callback state.
 *
 * Use for fixed infrastructure diagnostics. Stateful observers continue to use
 * the three-argument overload. Posting and execution failures are contained in
 * the same way; allocation before wrapper startup can still reach the caller.
 */
export template <auto OnError>
requires std::invocable<decltype(OnError), std::exception_ptr>
void spawn_guarded(io_context& ctx, task<void> task_to_run)
{
    [](io_context& context, task<void> inner) -> detached_task
    {
        try
        {
            co_await detail::detached_post_awaitable{context};
            co_await std::move(inner);
        }
        catch (...)
        {
            detail::report_static_spawn_failure<OnError>();
        }
    }(ctx, std::move(task_to_run));
}

} // namespace cnetmod
