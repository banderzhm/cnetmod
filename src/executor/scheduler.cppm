/// cnetmod.executor.scheduler -- native io_context scheduler facade.
///
/// The public module intentionally exposes coroutine awaitables only. The
/// executor implementation stays confined to .cpp implementation units so it
/// cannot leak through a module BMI.
export module cnetmod.executor.scheduler;

import std;
import cnetmod.io.io_context;

namespace cnetmod {

/// Awaitable returned by io_scheduler::schedule(). Awaiting it always resumes
/// the current coroutine through the target io_context post queue.
export struct schedule_awaitable
{
    io_context& context;

    auto await_ready() const noexcept -> bool;
    void await_suspend(std::coroutine_handle<> coroutine) noexcept;
    void await_resume() noexcept;
};

/// Lightweight scheduler facade for selecting an io_context execution domain.
export class io_scheduler
{
public:
    explicit io_scheduler(io_context& context) noexcept;

    auto operator==(const io_scheduler& other) const noexcept -> bool;

    [[nodiscard]] auto context() const noexcept -> io_context&;

    /// Switch the current coroutine to this scheduler's io_context.
    [[nodiscard]] auto schedule() const noexcept -> schedule_awaitable;

private:
    io_context* context_;
};

} // namespace cnetmod
