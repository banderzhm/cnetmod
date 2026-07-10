module;

#include <cnetmod/config.hpp>

export module cnetmod.coro.spawn;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

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
        co_await post_awaitable{context};
        co_await std::move(inner);
    }(ctx, std::move(task_to_run));
}

} // namespace cnetmod
