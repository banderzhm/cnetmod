module;

#include <cnetmod/config.hpp>
#include <exec/static_thread_pool.hpp>

module cnetmod.executor.pool;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;

namespace cnetmod {

void spawn_on(io_context &target, task<void> task) {
  spawn(target, std::move(task));
}

server_context::server_context(unsigned worker_count, unsigned pool_threads)
    : pool_(pool_threads == 0 ? 1 : pool_threads) {
  worker_count = std::max(worker_count, 1U);
  accept_io_ = make_io_context();
  workers_.reserve(worker_count);
  for (unsigned index = 0; index < worker_count; ++index)
    workers_.push_back(make_io_context());
}

server_context::~server_context() { stop(); }

auto server_context::accept_io() noexcept -> io_context & {
  return *accept_io_;
}

auto server_context::next_worker_io() noexcept -> io_context & {
  const auto index =
      next_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
  return *workers_[index];
}

auto server_context::worker_count() const noexcept -> unsigned {
  return static_cast<unsigned>(workers_.size());
}

auto server_context::worker_ios() -> std::vector<io_context *> {
  std::vector<io_context *> result;
  result.reserve(workers_.size());
  for (const auto &worker : workers_)
    result.push_back(worker.get());
  return result;
}

auto server_context::pool() noexcept -> exec::static_thread_pool & {
  return pool_;
}

void server_context::run() {
  threads_.reserve(workers_.size());
  for (const auto &worker : workers_) {
    threads_.emplace_back([context = worker.get()] { context->run(); });
  }
  accept_io_->run();
  for (auto &thread : threads_) {
    if (thread.joinable())
      thread.join();
  }
  threads_.clear();
}

void server_context::stop() {
  if (accept_io_)
    accept_io_->stop();
  for (const auto &worker : workers_)
    worker->stop();
  pool_.request_stop();
}

} // namespace cnetmod
