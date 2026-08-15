module;

#include <cnetmod/config.hpp>

module cnetmod.executor.scheduler;

import std;
import cnetmod.io.io_context;

namespace cnetmod {

auto schedule_awaitable::await_ready() const noexcept -> bool
{
    return false;
}

void schedule_awaitable::await_suspend(
    std::coroutine_handle<> coroutine) noexcept
{
    context.post(coroutine);
}

void schedule_awaitable::await_resume() noexcept {}

io_scheduler::io_scheduler(io_context& context) noexcept
    : context_(&context) {}

auto io_scheduler::operator==(const io_scheduler& other) const noexcept -> bool
{
    return context_ == other.context_;
}

auto io_scheduler::context() const noexcept -> io_context&
{
    return *context_;
}

auto io_scheduler::schedule() const noexcept -> schedule_awaitable
{
    return {*context_};
}

} // namespace cnetmod
