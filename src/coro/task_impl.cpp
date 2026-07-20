module cnetmod.coro.task;

import std;

namespace cnetmod {

auto promise_base::initial_suspend() noexcept -> std::suspend_always {
  return {};
}

auto promise_base::final_awaiter::await_ready() const noexcept -> bool {
  return false;
}

void promise_base::final_awaiter::await_resume() noexcept {}

auto promise_base::final_suspend() noexcept -> final_awaiter { return {}; }

void promise_base::set_caller(std::coroutine_handle<> caller) noexcept {
  caller_ = caller;
}

void sync_wait(task<void> task) {
  auto handle = task.handle();
  handle.resume();
  handle.promise().result();
}

namespace detail {

when_all_state::when_all_state(int count) noexcept : remaining(count) {}

void when_all_state::notify_one_done() noexcept {
  if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1 && caller) {
    caller.resume();
  }
}

auto make_when_all_task(task<void> task) -> when_all_task<void> {
  co_await std::move(task);
}

} // namespace detail

auto when_all(task<void> first, task<void> second) -> task<void> {
  auto first_waiter = detail::make_when_all_task(std::move(first));
  auto second_waiter = detail::make_when_all_task(std::move(second));
  detail::when_all_awaiter<detail::when_all_task<void>,
                           detail::when_all_task<void>>
      awaiter{std::move(first_waiter), std::move(second_waiter)};
  co_await awaiter;
  auto &[first_result, second_result] = awaiter.tasks_;
  first_result.result();
  second_result.result();
}

} // namespace cnetmod
