module cnetmod.protocol.modbus;

import std;
import :pool;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;

namespace cnetmod::modbus {

pooled_connection::pooled_connection(pooled_connection &&other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      node_(std::exchange(other.node_, nullptr)) {}
auto pooled_connection::operator=(pooled_connection &&other) noexcept
    -> pooled_connection & {
  if (this != &other) {
    return_to_pool();
    pool_ = std::exchange(other.pool_, nullptr);
    node_ = std::exchange(other.node_, nullptr);
  }
  return *this;
}
pooled_connection::~pooled_connection() { return_to_pool(); }
auto pooled_connection::valid() const noexcept -> bool {
  return node_ != nullptr;
}
auto pooled_connection::get() noexcept -> tcp_client & { return *node_->conn; }
auto pooled_connection::get() const noexcept -> const tcp_client & {
  return *node_->conn;
}
auto pooled_connection::operator->() noexcept -> tcp_client * {
  return node_->conn.get();
}
auto pooled_connection::operator->() const noexcept -> const tcp_client * {
  return node_->conn.get();
}
pooled_connection::pooled_connection(connection_pool *pool,
                                     conn_node *node) noexcept
    : pool_(pool), node_(node) {}
void pooled_connection::return_to_pool() {
  if (pool_ && node_) {
    pool_->return_connection(*node_);
    pool_ = nullptr;
    node_ = nullptr;
  }
}

connection_pool::connection_pool(io_context &ctx, pool_params params)
    : ctx_(ctx), params_(std::move(params)) {}
auto connection_pool::async_run() -> task<void> {
  running_ = true;
  for (std::size_t i{}; i < params_.initial_size && i < params_.max_size; ++i)
    spawn_connection();
  const auto target = std::min(params_.initial_size, params_.max_size);
  if (target > 0) {
    const auto deadline =
        std::chrono::steady_clock::now() + params_.connect_timeout;
    while (running_ && std::chrono::steady_clock::now() < deadline) {
      if (count_ready_connections() >= target)
        break;
      co_await async_sleep(ctx_, std::chrono::milliseconds(10));
    }
  }
  while (running_)
    co_await async_sleep(ctx_, params_.health_check_interval);
}
auto connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>> {
  cancel_token token;
  co_return co_await with_timeout(ctx_, params_.pool_timeout,
                                  async_get_connection(token), token);
}
auto connection_pool::try_get_connection()
    -> std::expected<pooled_connection, std::error_code> {
  if (waiters_count_.load(std::memory_order_acquire) == 0)
    if (auto *node = try_get_idle_lockfree())
      return pooled_connection(this, node);
  if (!mtx_.try_lock())
    return std::unexpected(
        std::make_error_code(std::errc::resource_unavailable_try_again));
  async_lock_guard guard(mtx_, std::adopt_lock);
  if (waiters_head_) {
    notify_waiters_with_idle_locked();
    return std::unexpected(
        std::make_error_code(std::errc::resource_unavailable_try_again));
  }
  if (auto *node = try_get_idle_locked())
    return pooled_connection(this, node);
  if (running_ && conns_.size() < params_.max_size)
    spawn_connection();
  return std::unexpected(
      std::make_error_code(std::errc::resource_unavailable_try_again));
}
auto connection_pool::cancel() -> task<void> {
  running_ = false;
  co_await mtx_.lock();
  for (auto &node : conns_)
    if (node.conn && node.conn->is_open())
      node.state.store(conn_state::dead, std::memory_order_release);
  mtx_.unlock();
}
auto connection_pool::size() const noexcept -> std::size_t {
  return conns_.size();
}
auto connection_pool::idle_count() const noexcept -> std::size_t {
  std::size_t count{};
  for (const auto &node : conns_)
    if (node.state.load(std::memory_order_acquire) == conn_state::idle)
      ++count;
  return count;
}
auto connection_pool::waiter_count() const noexcept -> std::size_t {
  return waiters_count_.load(std::memory_order_acquire);
}
auto connection_pool::count_ready_connections() const noexcept -> std::size_t {
  std::size_t count{};
  for (const auto &node : conns_) {
    const auto state = node.state.load(std::memory_order_acquire);
    if (state == conn_state::idle || state == conn_state::in_use)
      ++count;
  }
  return count;
}
auto connection_pool::connection_task(conn_node &node) -> task<void> {
  auto max_retry = std::chrono::duration_cast<std::chrono::milliseconds>(
      params_.retry_interval);
  if (max_retry <= std::chrono::milliseconds::zero())
    max_retry = std::chrono::milliseconds(1);
  auto backoff = std::min(max_retry, std::chrono::milliseconds(100));
  while (running_) {
    auto state = node.state.load(std::memory_order_acquire);
    if (state == conn_state::initial || state == conn_state::dead) {
      node.state.store(conn_state::connecting, std::memory_order_release);
      const auto result =
          co_await node.conn->connect(params_.host, params_.port);
      co_await mtx_.lock();
      if (result) {
        node.state.store(conn_state::dead, std::memory_order_release);
        mtx_.unlock();
        co_await async_sleep(ctx_, backoff);
        backoff = std::min(max_retry, backoff * 2);
        continue;
      }
      backoff = std::min(max_retry, std::chrono::milliseconds(100));
      node.state.store(conn_state::idle, std::memory_order_release);
      node.last_used = std::chrono::steady_clock::now();
      notify_waiters_with_idle_locked();
      mtx_.unlock();
    }
    state = node.state.load(std::memory_order_acquire);
    if (state == conn_state::idle) {
      co_await async_sleep(ctx_, params_.health_check_interval);
      if (!running_)
        break;
    }
    if (node.state.load(std::memory_order_acquire) == conn_state::in_use) {
      struct returned {
        conn_node &node;
        auto await_ready() const noexcept -> bool {
          return node.state.load(std::memory_order_acquire) !=
                 conn_state::in_use;
        }
        void await_suspend(std::coroutine_handle<> handle) noexcept {
          node.task_waiting = handle;
        }
        void await_resume() noexcept {}
      };
      co_await returned{node};
    }
  }
}
void connection_pool::spawn_connection() {
  const auto index = conns_.size();
  conns_.emplace_back();
  auto &node = conns_.back();
  node.conn = std::make_unique<tcp_client>(ctx_);
  node.state.store(conn_state::initial, std::memory_order_release);
  node.last_used = std::chrono::steady_clock::now();
  node.index = index;
  spawn(ctx_, connection_task(node));
}
void connection_pool::dec_if_positive(std::atomic<std::size_t> &counter) {
  auto value = counter.load(std::memory_order_acquire);
  while (value > 0 && !counter.compare_exchange_weak(
                          value, value - 1, std::memory_order_release,
                          std::memory_order_relaxed)) {
  }
}
auto connection_pool::try_get_idle_locked() -> conn_node * {
  for (auto &node : conns_) {
    auto expected = conn_state::idle;
    if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
      node.last_used = std::chrono::steady_clock::now();
      return &node;
    }
  }
  return nullptr;
}
auto connection_pool::try_get_idle_lockfree() -> conn_node * {
  return try_get_idle_locked();
}
void connection_pool::notify_waiters_with_idle_locked() {
  while (waiters_head_) {
    auto *node = try_get_idle_locked();
    if (!node)
      break;
    auto *waiter = waiters_head_;
    waiters_head_ = waiter->next;
    if (!waiters_head_)
      waiters_tail_ = nullptr;
    dec_if_positive(waiters_count_);
    if (num_pending_requests_ > 0)
      --num_pending_requests_;
    if (waiter->token) {
      waiter->token->pending_.store(false, std::memory_order_release);
      waiter->token->cancel_fn_ = nullptr;
    }
    *waiter->result_node = node;
    if (waiter->handle)
      ctx_.post(waiter->handle);
  }
}
auto connection_pool::remove_waiter(pool_waiter *target) -> bool {
  pool_waiter *previous = nullptr;
  for (auto *waiter = waiters_head_; waiter;
       previous = waiter, waiter = waiter->next)
    if (waiter == target) {
      if (previous)
        previous->next = waiter->next;
      else
        waiters_head_ = waiter->next;
      if (waiters_tail_ == waiter)
        waiters_tail_ = previous;
      waiter->next = nullptr;
      dec_if_positive(waiters_count_);
      if (num_pending_requests_ > 0)
        --num_pending_requests_;
      return true;
    }
  return false;
}
void connection_pool::return_connection(conn_node &node) {
  auto mark_idle = [this, &node] {
    auto expected = conn_state::in_use;
    if (!node.state.compare_exchange_strong(expected, conn_state::idle,
                                            std::memory_order_release,
                                            std::memory_order_relaxed))
      return false;
    node.last_used = std::chrono::steady_clock::now();
    if (auto handle = std::exchange(node.task_waiting, {}))
      ctx_.post(handle);
    return true;
  };
  if (waiters_count_.load(std::memory_order_acquire) == 0 && mark_idle())
    return;
  if (mtx_.try_lock()) {
    if (mark_idle() || waiters_head_)
      notify_waiters_with_idle_locked();
    mtx_.unlock();
    return;
  }
  auto *node_ptr = &node;
  spawn(ctx_, [this, node_ptr]() -> task<void> {
    co_await mtx_.lock();
    auto expected = conn_state::in_use;
    if (node_ptr->state.compare_exchange_strong(expected, conn_state::idle,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
      node_ptr->last_used = std::chrono::steady_clock::now();
      if (auto handle = std::exchange(node_ptr->task_waiting, {}))
        ctx_.post(handle);
    }
    if (waiters_head_)
      notify_waiters_with_idle_locked();
    mtx_.unlock();
  }());
}
auto connection_pool::async_get_connection(cancel_token &token)
    -> task<std::expected<pooled_connection, std::error_code>> {
  if (waiters_count_.load(std::memory_order_acquire) == 0)
    if (auto *node = try_get_idle_lockfree())
      co_return pooled_connection(this, node);
  co_await mtx_.lock();
  async_lock_guard guard(mtx_, std::adopt_lock);
  if (waiters_head_)
    notify_waiters_with_idle_locked();
  if (!waiters_head_)
    if (auto *node = try_get_idle_locked())
      co_return pooled_connection(this, node);
  ++num_pending_requests_;
  if (running_ && conns_.size() < params_.max_size)
    spawn_connection();
  conn_node *assigned = nullptr;
  pool_waiter waiter{.result_node = &assigned, .token = &token};
  struct awaiter {
    connection_pool &pool;
    pool_waiter &waiter;
    async_lock_guard &guard;
    cancel_token &token;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
      waiter.handle = handle;
      if (!pool.waiters_head_)
        pool.waiters_head_ = pool.waiters_tail_ = &waiter;
      else {
        pool.waiters_tail_->next = &waiter;
        pool.waiters_tail_ = &waiter;
      }
      pool.waiters_count_.fetch_add(1, std::memory_order_release);
      token.ctx_ = &pool;
      token.io_handle_ = &waiter;
      token.coroutine_ = handle;
      token.cancel_fn_ = [](cancel_token &value) noexcept {
        auto *pool = static_cast<connection_pool *>(value.ctx_);
        auto *waiter = static_cast<pool_waiter *>(value.io_handle_);
        if (pool->mtx_.try_lock()) {
          pool->remove_waiter(waiter);
          pool->mtx_.unlock();
        }
        pool->ctx_.post(value.coroutine_);
      };
      token.pending_.store(true, std::memory_order_release);
      guard.release();
      pool.mtx_.unlock();
      if (token.is_cancelled()) {
        if (pool.mtx_.try_lock()) {
          pool.remove_waiter(&waiter);
          pool.mtx_.unlock();
        }
        pool.ctx_.post(handle);
      }
    }
    void await_resume() noexcept {
      token.pending_.store(false, std::memory_order_relaxed);
    }
  };
  co_await awaiter{*this, waiter, guard, token};
  if (token.is_cancelled()) {
    co_await mtx_.lock();
    remove_waiter(&waiter);
    mtx_.unlock();
    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
  }
  if (assigned)
    co_return pooled_connection(this, assigned);
  co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}

} // namespace cnetmod::modbus
