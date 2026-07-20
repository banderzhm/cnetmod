module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.spawn;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

namespace detail {

struct detached_post_state {
    std::coroutine_handle<> coroutine;
};

inline void resume_detached_post(void* raw) noexcept {
    std::unique_ptr<detached_post_state> state{
        static_cast<detached_post_state*>(raw)};
    const auto coroutine = state->coroutine;
    coroutine.resume();
}

inline void discard_detached_post(void* raw) noexcept {
    std::unique_ptr<detached_post_state> state{
        static_cast<detached_post_state*>(raw)};
    if (state->coroutine)
        state->coroutine.destroy();
}

struct detached_post_awaitable {
    io_context& ctx;

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> coroutine) {
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
struct detached_task {
    struct promise_type {
        auto get_return_object() noexcept -> detached_task { return {}; }
        auto initial_suspend() noexcept -> std::suspend_never { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
        auto final_suspend() noexcept -> std::suspend_never { return {}; }
    };
};

// =============================================================================
// spawn — Start fire-and-forget coroutine on io_context
// =============================================================================

/// Post a fire-and-forget task to an io_context.
/// Kept in the interface unit: current MSVC module codegen emits an invalid
/// COFF object when this coroutine's detached promise lives in a .cpp unit.
export void spawn(io_context& ctx, task<void> task_to_run) {
    [](io_context& context, task<void> inner) -> detached_task {
        co_await detail::detached_post_awaitable{context};
        co_await std::move(inner);
    }(ctx, std::move(task_to_run));
}

} // namespace cnetmod
