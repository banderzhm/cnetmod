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

/// spawn(ctx, task) — Post task to io_context event loop for execution
/// detached_task owns task's co_await, frame auto-destroys after task completes
export void spawn(io_context& ctx, task<void> t) {
    [](io_context& c, task<void> inner) -> detached_task {
        co_await post_awaitable{c};     // Switch to event loop thread
        co_await std::move(inner);      // Drive task execution
    }(ctx, std::move(t));
}

} // namespace cnetmod
