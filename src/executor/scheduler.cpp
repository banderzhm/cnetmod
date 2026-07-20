module;

#include <cnetmod/config.hpp>
#include <stdexec/execution.hpp>

module cnetmod.executor.scheduler;

namespace cnetmod {

io_scheduler::io_scheduler(io_context &ctx) noexcept : ctx_(&ctx) {}

auto io_scheduler::operator==(const io_scheduler &other) const noexcept
    -> bool {
  return ctx_ == other.ctx_;
}

auto io_scheduler::context() const noexcept -> io_context & { return *ctx_; }

auto io_scheduler::schedule() noexcept -> schedule_sender {
  return schedule_sender{ctx_};
}

auto schedule_env::query(
    stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
    -> io_scheduler {
  return io_scheduler{*ctx_};
}

schedule_sender::schedule_sender(io_context *ctx) noexcept : ctx_(ctx) {}

auto schedule_sender::get_env() const noexcept -> schedule_env {
  return {ctx_};
}

} // namespace cnetmod
