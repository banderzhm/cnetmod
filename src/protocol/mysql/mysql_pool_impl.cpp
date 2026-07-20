module;

#include <cnetmod/config.hpp>
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

module cnetmod.protocol.mysql;

import :pool;
import :types;
import :diagnostics;
import :connection_client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;

namespace cnetmod::mysql {

pooled_connection::pooled_connection(pooled_connection &&o) noexcept
    : pool_(std::exchange(o.pool_, nullptr)),
      node_(std::exchange(o.node_, nullptr)) {}

auto pooled_connection::operator=(pooled_connection &&o) noexcept
    -> pooled_connection & {
  if (this != &o) {
    return_to_pool(true);
    pool_ = std::exchange(o.pool_, nullptr);
    node_ = std::exchange(o.node_, nullptr);
  }
  return *this;
}

pooled_connection::~pooled_connection() { return_to_pool(true); }

auto pooled_connection::valid() const noexcept -> bool {
  return node_ != nullptr;
}

auto pooled_connection::get() noexcept -> client & { return *node_->conn; }

auto pooled_connection::get() const noexcept -> const client & {
  return *node_->conn;
}

auto pooled_connection::operator->() noexcept -> client * {
  return node_->conn.get();
}

auto pooled_connection::operator->() const noexcept -> const client * {
  return node_->conn.get();
}

pooled_connection::pooled_connection(connection_pool *pool,
                                     conn_node *node) noexcept
    : pool_(pool), node_(node) {}

connection_pool::connection_pool(io_context &ctx, pool_params params)
    : ctx_(ctx), params_(std::move(params)) {
  // P6: Initialize bitmap to zero
  for (auto &bm : idle_bitmap_) {
    bm.store(0, std::memory_order_relaxed);
  }
}

auto connection_pool::async_run() -> task<void> {
  running_ = true;
  // Spawn initial connection tasks
  for (std::size_t i = 0; i < params_.initial_size && i < params_.max_size;
       ++i) {
    spawn_connection();
  }
  // Warm-up barrier: wait until initial connections are ready (or timeout).
  auto target = std::min(params_.initial_size, params_.max_size);
  if (target > 0) {
    auto deadline = std::chrono::steady_clock::now() + params_.connect_timeout;
    while (running_ && std::chrono::steady_clock::now() < deadline) {
      if (count_ready_connections() >= target)
        break;
      co_await async_sleep(ctx_, std::chrono::milliseconds(10));
    }
  }
  // Background: periodic scan for dead connections that have no task
  while (running_) {
    co_await async_sleep(ctx_, params_.ping_interval);
  }
}

auto connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>> {
  cancel_token token;
  co_return co_await with_timeout(ctx_, params_.pool_timeout,
                                  async_get_connection(token), token);
}

auto connection_pool::try_get_connection()
    -> std::expected<pooled_connection, std::error_code> {
  if (waiters_count_.load(std::memory_order_acquire) == 0) {
    if (auto *node = try_get_idle_lockfree()) {
      return pooled_connection(this, node);
    }
  }

  if (!mtx_.try_lock()) {
    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
  }
  async_lock_guard guard(mtx_, std::adopt_lock);

  if (waiters_head_) {
    notify_waiters_with_idle_locked();
    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
  }

  if (auto *node = try_get_idle_locked()) {
    return pooled_connection(this, node);
  }

  // No idle connection now: proactively grow one if possible.
  if (running_ && conns_.size() < params_.max_size) {
    spawn_connection();
  }

  return std::unexpected(
      make_error_code(std::errc::resource_unavailable_try_again));
}

auto connection_pool::cancel() -> task<void> {
  running_ = false;
  co_await mtx_.lock();
  for (auto &node : conns_) {
    if (node.conn && node.conn->is_open())
      node.state.store(conn_state::dead, std::memory_order_release);
  }
  mtx_.unlock();
}

auto connection_pool::size() const noexcept -> std::size_t {
  return conns_.size();
}

auto connection_pool::idle_count() const noexcept -> std::size_t {
  std::size_t n = 0;
  for (auto &nd : conns_) {
    if (nd.state.load(std::memory_order_acquire) == conn_state::idle)
      ++n;
  }
  return n;
}

auto connection_pool::waiter_count() const noexcept -> std::size_t {
  return waiters_count_.load(std::memory_order_acquire);
}

auto connection_pool::make_connect_options() const -> connect_options {
  connect_options opts;
  opts.host = params_.host;
  opts.port = params_.port;
  opts.username = params_.username;
  opts.password = params_.password;
  opts.database = params_.database;
  opts.ssl = params_.ssl;
  opts.tls_verify = params_.tls_verify;
  opts.tls_ca_file = params_.tls_ca_file;
  return opts;
}

auto connection_pool::count_ready_connections() const noexcept -> std::size_t {
  std::size_t ready = 0;
  for (auto &node : conns_) {
    auto st = node.state.load(std::memory_order_acquire);
    if (st == conn_state::idle || st == conn_state::in_use ||
        st == conn_state::pinging || st == conn_state::resetting) {
      ++ready;
    }
  }
  return ready;
}

auto connection_pool::connection_task(conn_node &node) -> task<void> {
  auto max_retry = std::chrono::duration_cast<std::chrono::milliseconds>(
      params_.retry_interval);
  if (max_retry <= std::chrono::milliseconds::zero()) {
    max_retry = std::chrono::milliseconds(1);
  }
  auto retry_backoff = std::min(max_retry, std::chrono::milliseconds(100));

  while (running_) {
    // Phase 1: Ensure connected
    auto state = node.state.load(std::memory_order_acquire);
    if (state == conn_state::initial || state == conn_state::dead) {
      node.state.store(conn_state::connecting, std::memory_order_release);
      auto opts = make_connect_options();
      auto rs = co_await node.conn->connect(opts);

      co_await mtx_.lock();
      if (rs.is_err()) {
        node.state.store(conn_state::dead, std::memory_order_release);
        mtx_.unlock();
        co_await async_sleep(ctx_, retry_backoff);
        retry_backoff = std::min(max_retry, retry_backoff * 2);
        continue;
      }
      retry_backoff = std::min(max_retry, std::chrono::milliseconds(100));
      node.state.store(conn_state::idle, std::memory_order_release);
      node.last_used = std::chrono::steady_clock::now();
      set_idle_bit(node.index); // P6: Mark as idle in bitmap
      // Hand idle connections to queued waiters first.
      notify_waiters_with_idle_locked();
      mtx_.unlock();
    }

    // Phase 2: Idle — wait ping_interval then ping
    state = node.state.load(std::memory_order_acquire);
    if (state == conn_state::idle) {
      // Make the idle wait cancellable so we can reconnect promptly
      // when a borrowed connection returns broken.
      node.ping_sleep_token.reset();
      auto wait_r = co_await cnetmod::async_timer_wait(
          ctx_, params_.ping_interval, node.ping_sleep_token);
      node.ping_sleep_token.reset();
      if (!wait_r)
        continue; // cancelled or timer error
      if (!running_)
        break;

      co_await mtx_.lock();
      conn_state expected = conn_state::idle;
      if (!node.state.compare_exchange_strong(expected, conn_state::pinging,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
        mtx_.unlock();
        continue; // Was borrowed during sleep
      }
      clear_idle_bit(node.index); // P6: Clear idle bit during ping
      mtx_.unlock();

      auto rs = co_await node.conn->ping();

      co_await mtx_.lock();
      if (rs.is_err()) {
        node.state.store(conn_state::dead, std::memory_order_release);
        mtx_.unlock();
        continue; // Will reconnect next iteration
      }
      node.state.store(conn_state::idle, std::memory_order_release);
      node.last_used = std::chrono::steady_clock::now();
      set_idle_bit(node.index); // P6: Mark as idle again
      mtx_.unlock();
    }

    // If in_use, suspend until returned (zero-cost wait)
    state = node.state.load(std::memory_order_acquire);
    if (state == conn_state::in_use) {
      struct return_awaitable {
        conn_node &n;

        auto await_ready() const noexcept -> bool {
          return n.state.load(std::memory_order_acquire) != conn_state::in_use;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
          n.task_waiting = h;
        }

        void await_resume() noexcept {}
      };
      co_await return_awaitable{node};
    }
  }
}

void connection_pool::spawn_connection() {
  std::size_t idx = conns_.size();
  if (idx >= MAX_BITMAPS * BITMAP_BITS)
    return; // Max capacity reached

  conns_.emplace_back();
  auto &node = conns_.back();
  node.conn = std::make_unique<client>(ctx_);
  node.state.store(conn_state::initial, std::memory_order_release);
  node.last_used = std::chrono::steady_clock::now();
  node.index = idx;
  spawn(ctx_, connection_task(node));
}

void connection_pool::set_idle_bit(std::size_t idx) {
  std::size_t bitmap_idx = idx / BITMAP_BITS;
  std::size_t bit_pos = idx % BITMAP_BITS;
  if (bitmap_idx < MAX_BITMAPS) {
    idle_bitmap_[bitmap_idx].fetch_or(1ULL << bit_pos,
                                      std::memory_order_release);
  }
}

void connection_pool::clear_idle_bit(std::size_t idx) {
  std::size_t bitmap_idx = idx / BITMAP_BITS;
  std::size_t bit_pos = idx % BITMAP_BITS;
  if (bitmap_idx < MAX_BITMAPS) {
    idle_bitmap_[bitmap_idx].fetch_and(~(1ULL << bit_pos),
                                       std::memory_order_release);
  }
}

std::size_t connection_pool::count_pending_conns() const {
  std::size_t n = 0;
  for (auto &nd : conns_) {
    auto state = nd.state.load(std::memory_order_acquire);
    if (state == conn_state::connecting || state == conn_state::initial)
      ++n;
  }
  return n;
}

void connection_pool::create_connections_if_needed() {
  // Must hold lock
  std::size_t pending = count_pending_conns();
  std::size_t room = params_.max_size - conns_.size();
  std::size_t needed =
      (num_pending_requests_ > pending) ? (num_pending_requests_ - pending) : 0;
  std::size_t to_create = std::min(needed, room);
  for (std::size_t i = 0; i < to_create; ++i)
    spawn_connection();
}

void connection_pool::dec_if_positive(std::atomic<std::size_t> &counter) {
  auto v = counter.load(std::memory_order_acquire);
  while (v > 0 &&
         !counter.compare_exchange_weak(v, v - 1, std::memory_order_release,
                                        std::memory_order_relaxed)) {
  }
}

auto connection_pool::try_get_idle_locked() -> conn_node * {
  // Must hold lock.
  for (std::size_t i = 0; i < MAX_BITMAPS; ++i) {
    uint64_t bits = idle_bitmap_[i].load(std::memory_order_acquire);
    while (bits != 0) {
      int bit_pos = -1;
#if defined(_MSC_VER)
      unsigned long pos;
      if (_BitScanForward64(&pos, bits)) {
        bit_pos = static_cast<int>(pos);
      }
#elif defined(__GNUC__) || defined(__clang__)
      bit_pos = __builtin_ctzll(bits);
#else
      for (int j = 0; j < 64; ++j) {
        if (bits & (1ULL << j)) {
          bit_pos = j;
          break;
        }
      }
#endif
      if (bit_pos < 0)
        break;
      bits &= (bits - 1);

      std::size_t idx = i * BITMAP_BITS + static_cast<std::size_t>(bit_pos);
      if (idx >= conns_.size())
        continue;

      auto &node = conns_[idx];
      conn_state expected = conn_state::idle;
      if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
        clear_idle_bit(idx);
        node.last_used = std::chrono::steady_clock::now();
        return &node;
      }
    }
  }
  return nullptr;
}

void connection_pool::notify_waiters_with_idle_locked() {
  // Must hold lock.
  while (waiters_head_) {
    auto *node = try_get_idle_locked();
    if (!node)
      break;

    auto *w = waiters_head_;
    waiters_head_ = w->next;
    if (!waiters_head_)
      waiters_tail_ = nullptr;
    dec_if_positive(waiters_count_);
    if (num_pending_requests_ > 0)
      --num_pending_requests_;

    if (w->token) {
      w->token->pending_.store(false, std::memory_order_release);
      w->token->cancel_fn_ = nullptr;
    }
    *w->result_node = node;
    if (w->handle)
      ctx_.post(w->handle);
  }
}

auto connection_pool::remove_waiter(pool_waiter *target) -> bool {
  pool_waiter *prev = nullptr;
  for (auto *w = waiters_head_; w; prev = w, w = w->next) {
    if (w == target) {
      if (prev)
        prev->next = w->next;
      else
        waiters_head_ = w->next;
      if (waiters_tail_ == w)
        waiters_tail_ = prev;
      w->next = nullptr;
      dec_if_positive(waiters_count_);
      if (num_pending_requests_ > 0)
        --num_pending_requests_;
      return true;
    }
  }
  return false;
}

auto connection_pool::try_get_idle_lockfree() -> conn_node * {
  // Scan bitmaps to find idle connection (64 connections per iteration)
  for (std::size_t i = 0; i < MAX_BITMAPS; ++i) {
    uint64_t bits = idle_bitmap_[i].load(std::memory_order_acquire);
    if (bits == 0)
      continue; // No idle connections in this bitmap

    while (bits != 0) {
      // Find first set bit (idle connection)
      int bit_pos = -1;
#if defined(_MSC_VER)
      unsigned long pos;
      if (_BitScanForward64(&pos, bits)) {
        bit_pos = static_cast<int>(pos);
      }
#elif defined(__GNUC__) || defined(__clang__)
      bit_pos = __builtin_ctzll(bits); // Count trailing zeros
#else
      // Fallback: linear scan
      for (int j = 0; j < 64; ++j) {
        if (bits & (1ULL << j)) {
          bit_pos = j;
          break;
        }
      }
#endif
      if (bit_pos < 0)
        break;
      bits &= (bits - 1);

      std::size_t idx = i * BITMAP_BITS + static_cast<std::size_t>(bit_pos);
      if (idx >= conns_.size())
        continue;

      auto &node = conns_[idx];
      conn_state expected = conn_state::idle;
      if (node.state.compare_exchange_strong(expected, conn_state::in_use,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
        clear_idle_bit(idx);
        node.last_used = std::chrono::steady_clock::now();
        return &node;
      }
    }
  }
  return nullptr;
}

void connection_pool::return_connection(conn_node &node, bool needs_reset) {
  enum class ret_kind { failed, idle, dead };

  auto mark_returned = [this, &node, needs_reset]() -> ret_kind {
    const bool open = node.conn && node.conn->is_open();
    const conn_state target = open ? conn_state::idle : conn_state::dead;

    conn_state expected = conn_state::in_use;
    if (!node.state.compare_exchange_strong(expected, target,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
      return ret_kind::failed;
    }

    node.needs_reset.store(needs_reset, std::memory_order_relaxed);
    node.last_used = std::chrono::steady_clock::now();

    if (open) {
      set_idle_bit(node.index);
    } else {
      clear_idle_bit(node.index);
      // Wake the per-connection task if it's waiting in idle sleep,
      // so it can reconnect immediately.
      node.ping_sleep_token.cancel();
    }

    if (auto h = std::exchange(node.task_waiting, {}))
      ctx_.post(h);

    return open ? ret_kind::idle : ret_kind::dead;
  };

  // No queued waiters: avoid lock and return immediately.
  if (waiters_count_.load(std::memory_order_acquire) == 0) {
    if (mark_returned() != ret_kind::failed)
      return;
  }

  // Waiters exist: notify under lock.
  if (mtx_.try_lock()) {
    auto r = mark_returned();
    if (r == ret_kind::idle || waiters_head_) {
      notify_waiters_with_idle_locked();
    }
    mtx_.unlock();
    return;
  }

  // Async fallback: enqueue lock acquisition without CPU spinning.
  auto *node_ptr = &node;
  spawn(ctx_, [this, node_ptr, needs_reset]() -> task<void> {
    co_await mtx_.lock();
    const bool open = node_ptr->conn && node_ptr->conn->is_open();
    const conn_state target = open ? conn_state::idle : conn_state::dead;

    conn_state expected = conn_state::in_use;
    if (node_ptr->state.compare_exchange_strong(expected, target,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
      node_ptr->needs_reset.store(needs_reset, std::memory_order_relaxed);
      node_ptr->last_used = std::chrono::steady_clock::now();

      if (open) {
        set_idle_bit(node_ptr->index);
      } else {
        clear_idle_bit(node_ptr->index);
        node_ptr->ping_sleep_token.cancel();
      }

      if (auto h = std::exchange(node_ptr->task_waiting, {}))
        ctx_.post(h);
    }
    if (waiters_head_) {
      notify_waiters_with_idle_locked();
    }
    mtx_.unlock();
  }());
}

auto connection_pool::async_get_connection(cancel_token &token)
    -> task<std::expected<pooled_connection, std::error_code>> {
  // P4: Try lock-free fast path first (no lock needed), but preserve FIFO
  // fairness.
  if (waiters_count_.load(std::memory_order_acquire) == 0) {
    if (auto *node = try_get_idle_lockfree()) {
      co_return pooled_connection(this, node);
    }
  }

  // Slow path: need lock for queue operations
  co_await mtx_.lock();
  async_lock_guard guard(mtx_, std::adopt_lock);

  // Prioritize old waiters first if any idle connections exist.
  if (waiters_head_) {
    notify_waiters_with_idle_locked();
  }

  // 1) Try to find an idle connection again (may have become available)
  if (!waiters_head_) {
    if (auto *node = try_get_idle_locked()) {
      co_return pooled_connection(this, node);
    }
  }

  // 2) P1: Record pending request + demand-driven scaling
  ++num_pending_requests_;
  if (running_)
    create_connections_if_needed();

  // 3) Wait in queue (cancellable via cancel_token)
  conn_node *assigned = nullptr;
  pool_waiter waiter;
  waiter.result_node = &assigned;
  waiter.token = &token;

  struct waiter_awaitable {
    connection_pool &pool;
    pool_waiter &w;
    async_lock_guard &guard;
    cancel_token &token;

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      w.handle = h;
      if (!pool.waiters_head_) {
        pool.waiters_head_ = pool.waiters_tail_ = &w;
      } else {
        pool.waiters_tail_->next = &w;
        pool.waiters_tail_ = &w;
      }
      pool.waiters_count_.fetch_add(1, std::memory_order_release);
      token.ctx_ = &pool;
      token.io_handle_ = &w;
      token.coroutine_ = h;
      token.cancel_fn_ = [](cancel_token &tok) noexcept {
        auto *p = static_cast<connection_pool *>(tok.ctx_);
        auto *wt = static_cast<pool_waiter *>(tok.io_handle_);
        if (p->mtx_.try_lock()) {
          p->remove_waiter(wt);
          p->mtx_.unlock();
        }
        p->ctx_.post(tok.coroutine_);
      };
      token.pending_.store(true, std::memory_order_release);
      guard.release();
      pool.mtx_.unlock();

      if (token.is_cancelled()) {
        if (pool.mtx_.try_lock()) {
          pool.remove_waiter(&w);
          pool.mtx_.unlock();
        }
        pool.ctx_.post(h);
      }
    }

    void await_resume() noexcept {
      token.pending_.store(false, std::memory_order_relaxed);
    }
  };

  co_await waiter_awaitable{*this, waiter, guard, token};

  if (token.is_cancelled()) {
    co_await mtx_.lock();
    remove_waiter(&waiter);
    mtx_.unlock();
    co_return std::unexpected(make_error_code(std::errc::timed_out));
  }

  if (assigned) {
    co_return pooled_connection(this, assigned);
  }
  co_return std::unexpected(make_error_code(std::errc::timed_out));
}

void pooled_connection::return_to_pool(bool needs_reset) {
  if (pool_ && node_) {
    pool_->return_connection(*node_, needs_reset);
    pool_ = nullptr;
    node_ = nullptr;
  }
}

void pooled_connection::return_without_reset() { return_to_pool(false); }

sharded_connection_pool::sharded_connection_pool(io_context &ctx,
                                                 pool_params params,
                                                 std::size_t num_shards)
    : base_params_(std::move(params)), fallback_ctx_(&ctx) {
  if (num_shards == 0)
    num_shards = 1;
  init_shards(std::vector<io_context *>{&ctx}, num_shards);
}

sharded_connection_pool::sharded_connection_pool(
    std::vector<io_context *> worker_contexts, pool_params params)
    : sharded_connection_pool(
          worker_contexts, std::move(params),
          worker_contexts.empty() ? 1 : worker_contexts.size()) {}

sharded_connection_pool::sharded_connection_pool(
    std::vector<io_context *> worker_contexts, pool_params params,
    std::size_t num_shards)
    : base_params_(std::move(params)) {
  if (worker_contexts.empty()) {
    throw std::invalid_argument(
        "sharded_connection_pool requires at least one io_context");
  }
  if (num_shards == 0)
    num_shards = 1;
  fallback_ctx_ = worker_contexts.front();
  init_shards(worker_contexts, num_shards);
}

auto sharded_connection_pool::async_run() -> task<void> {
  for (std::size_t i = 0; i < shards_.size(); ++i) {
    spawn(*shard_ctxs_[i], shards_[i]->async_run());
  }
  co_return;
}

auto sharded_connection_pool::async_get_connection()
    -> task<std::expected<pooled_connection, std::error_code>> {
  auto primary =
      next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
  if (auto fast = try_borrow_immediate(primary)) {
    co_return std::move(*fast);
  }

  auto wait_idx = select_wait_shard(primary);
  co_return co_await shards_[wait_idx]->async_get_connection();
}

auto sharded_connection_pool::async_get_connection(cancel_token &token)
    -> task<std::expected<pooled_connection, std::error_code>> {
  auto primary =
      next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
  if (auto fast = try_borrow_immediate(primary)) {
    co_return std::move(*fast);
  }

  auto wait_idx = select_wait_shard(primary);
  co_return co_await shards_[wait_idx]->async_get_connection(token);
}

auto sharded_connection_pool::async_get_connection(io_context &io)
    -> task<std::expected<pooled_connection, std::error_code>> {
  auto primary = get_shard_index(io);
  if (auto fast = try_borrow_immediate(primary)) {
    co_return std::move(*fast);
  }

  auto wait_idx = select_wait_shard(primary);
  co_return co_await shards_[wait_idx]->async_get_connection();
}

auto sharded_connection_pool::async_get_connection(io_context &io,
                                                   cancel_token &token)
    -> task<std::expected<pooled_connection, std::error_code>> {
  auto primary = get_shard_index(io);
  if (auto fast = try_borrow_immediate(primary)) {
    co_return std::move(*fast);
  }

  auto wait_idx = select_wait_shard(primary);
  co_return co_await shards_[wait_idx]->async_get_connection(token);
}

auto sharded_connection_pool::cancel() -> task<void> {
  for (auto &shard : shards_) {
    co_await shard->cancel();
  }
}

auto sharded_connection_pool::size() const noexcept -> std::size_t {
  std::size_t total = 0;
  for (auto &shard : shards_) {
    total += shard->size();
  }
  return total;
}

auto sharded_connection_pool::idle_count() const noexcept -> std::size_t {
  std::size_t total = 0;
  for (auto &shard : shards_) {
    total += shard->idle_count();
  }
  return total;
}

auto sharded_connection_pool::shard_count() const noexcept -> std::size_t {
  return shards_.size();
}

auto sharded_connection_pool::get_shard_index(io_context &io) -> std::size_t {
  auto it = shard_by_ctx_.find(&io);
  if (it != shard_by_ctx_.end()) {
    return it->second;
  }
  return next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
}

auto sharded_connection_pool::try_borrow_immediate(std::size_t primary_idx)
    -> std::expected<pooled_connection, std::error_code> {
  // 1) Try preferred shard first.
  if (primary_idx < shards_.size()) {
    if (auto conn = shards_[primary_idx]->try_get_connection()) {
      return std::move(*conn);
    }
  }

  // 2) Steal from other shards (best effort, no waiting).
  if (shards_.size() <= 1) {
    return std::unexpected(
        make_error_code(std::errc::resource_unavailable_try_again));
  }

  auto start =
      next_shard_.fetch_add(1, std::memory_order_relaxed) % shards_.size();
  for (std::size_t i = 0; i < shards_.size(); ++i) {
    auto idx = (start + i) % shards_.size();
    if (idx == primary_idx)
      continue;
    if (auto conn = shards_[idx]->try_get_connection()) {
      return std::move(*conn);
    }
  }
  return std::unexpected(
      make_error_code(std::errc::resource_unavailable_try_again));
}

auto sharded_connection_pool::select_wait_shard(std::size_t preferred_idx)
    -> std::size_t {
  if (preferred_idx >= shards_.size()) {
    preferred_idx = 0;
  }
  auto best_idx = preferred_idx;
  auto best_waiters = shards_[best_idx]->waiter_count();

  for (std::size_t i = 0; i < shards_.size(); ++i) {
    auto w = shards_[i]->waiter_count();
    if (w < best_waiters) {
      best_waiters = w;
      best_idx = i;
    }
  }
  return best_idx;
}

void sharded_connection_pool::init_shards(
    const std::vector<io_context *> &worker_contexts, std::size_t num_shards) {
  if (num_shards == 0)
    num_shards = 1;

  std::vector<io_context *> contexts;
  contexts.reserve(worker_contexts.size());
  for (auto *ctx : worker_contexts) {
    if (ctx)
      contexts.push_back(ctx);
  }
  if (contexts.empty()) {
    throw std::invalid_argument(
        "sharded_connection_pool has no valid io_context");
  }

  auto shard_initial =
      (base_params_.initial_size + num_shards - 1) / num_shards;
  auto shard_max = (base_params_.max_size + num_shards - 1) / num_shards;

  shards_.reserve(num_shards);
  shard_ctxs_.reserve(num_shards);
  for (std::size_t i = 0; i < num_shards; ++i) {
    auto *shard_ctx = contexts[i % contexts.size()];
    if (!shard_ctx)
      shard_ctx = fallback_ctx_;
    if (!shard_ctx)
      continue;
    auto shard_params = base_params_;
    shard_params.initial_size = shard_initial;
    shard_params.max_size = shard_max;
    shard_ctxs_.push_back(shard_ctx);
    shards_.push_back(
        std::make_unique<connection_pool>(*shard_ctx, std::move(shard_params)));
  }

  if (shards_.empty()) {
    throw std::invalid_argument(
        "sharded_connection_pool has no valid io_context");
  }

  // Build context -> primary shard mapping (workers and shards are decoupled).
  for (std::size_t i = 0; i < contexts.size(); ++i) {
    auto *ctx = contexts[i];
    auto primary = i % shards_.size();
    if (!shard_by_ctx_.contains(ctx)) {
      shard_by_ctx_.emplace(ctx, primary);
    }
  }
}

} // namespace cnetmod::mysql
