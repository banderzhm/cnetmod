module;

#include <cnetmod/config.hpp>
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

export module cnetmod.protocol.mysql:pool;

import std;
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
export struct pool_params {
  std::string host = "127.0.0.1";
  std::uint16_t port = 3306;
  std::string username;
  std::string password;
  std::string database;
  ssl_mode ssl = ssl_mode::enable;
  std::size_t initial_size = 1;
  std::size_t max_size = 16;
  std::chrono::steady_clock::duration connect_timeout =
      std::chrono::seconds(20);
  std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
  std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
  std::chrono::steady_clock::duration ping_interval = std::chrono::hours(1);
  std::chrono::steady_clock::duration ping_timeout = std::chrono::seconds(10);
  bool tls_verify = false;
  std::string tls_ca_file;
};

enum class conn_state : std::uint8_t {
  initial,
  connecting,
  idle,
  in_use,
  resetting,
  pinging,
  dead
};

struct conn_node {
  std::unique_ptr<client> conn;
  std::atomic<conn_state> state = conn_state::initial;
  std::chrono::steady_clock::time_point last_used;
  std::atomic<bool> needs_reset = false;
  cancel_token ping_sleep_token{};
  std::coroutine_handle<> task_waiting{};
  std::size_t index = 0;
  conn_node() = default;
  conn_node(conn_node &&) = delete;
  conn_node &operator=(conn_node &&) = delete;
  conn_node(const conn_node &) = delete;
  conn_node &operator=(const conn_node &) = delete;
};

struct pool_waiter {
  std::coroutine_handle<> handle{};
  conn_node **result_node = nullptr;
  pool_waiter *next = nullptr;
  cancel_token *token = nullptr;
};

export class connection_pool;

export class pooled_connection {
public:
  pooled_connection() noexcept = default;
  pooled_connection(pooled_connection &&other) noexcept;
  auto operator=(pooled_connection &&other) noexcept -> pooled_connection &;
  pooled_connection(const pooled_connection &) = delete;
  auto operator=(const pooled_connection &) -> pooled_connection & = delete;
  ~pooled_connection();
  auto valid() const noexcept -> bool;
  auto get() noexcept -> client &;
  auto get() const noexcept -> const client &;
  auto operator->() noexcept -> client *;
  auto operator->() const noexcept -> const client *;
  void return_without_reset();

private:
  friend class connection_pool;
  connection_pool *pool_ = nullptr;
  conn_node *node_ = nullptr;
  pooled_connection(connection_pool *pool, conn_node *node) noexcept;
  void return_to_pool(bool needs_reset);
};

export class connection_pool {
public:
  connection_pool(io_context &ctx, pool_params params);
  connection_pool(const connection_pool &) = delete;
  auto operator=(const connection_pool &) -> connection_pool & = delete;
  auto async_run() -> task<void>;
  auto async_get_connection(cancel_token &token)
      -> task<std::expected<pooled_connection, std::error_code>>;
  auto async_get_connection()
      -> task<std::expected<pooled_connection, std::error_code>>;
  auto try_get_connection()
      -> std::expected<pooled_connection, std::error_code>;
  auto cancel() -> task<void>;
  auto size() const noexcept -> std::size_t;
  auto idle_count() const noexcept -> std::size_t;
  auto waiter_count() const noexcept -> std::size_t;

private:
  friend class pooled_connection;
  io_context &ctx_;
  pool_params params_;
  std::deque<conn_node> conns_;
  async_mutex mtx_;
  bool running_ = false;
  pool_waiter *waiters_head_ = nullptr;
  pool_waiter *waiters_tail_ = nullptr;
  std::size_t num_pending_requests_ = 0;
  std::atomic<std::size_t> waiters_count_{0};
  static constexpr std::size_t BITMAP_BITS = 64;
  static constexpr std::size_t MAX_BITMAPS = 8;
  std::atomic<uint64_t> idle_bitmap_[MAX_BITMAPS];
  auto make_connect_options() const -> connect_options;
  auto count_ready_connections() const noexcept -> std::size_t;
  auto connection_task(conn_node &node) -> task<void>;
  void spawn_connection();
  void set_idle_bit(std::size_t index);
  void clear_idle_bit(std::size_t index);
  auto count_pending_conns() const -> std::size_t;
  void create_connections_if_needed();
  static void dec_if_positive(std::atomic<std::size_t> &counter);
  auto try_get_idle_locked() -> conn_node *;
  void notify_waiters_with_idle_locked();
  auto remove_waiter(pool_waiter *target) -> bool;
  auto try_get_idle_lockfree() -> conn_node *;
  void return_connection(conn_node &node, bool needs_reset);
};

export class sharded_connection_pool {
public:
  sharded_connection_pool(io_context &ctx, pool_params params,
                          std::size_t num_shards = 4);
  sharded_connection_pool(std::vector<io_context *> worker_contexts,
                          pool_params params);
  sharded_connection_pool(std::vector<io_context *> worker_contexts,
                          pool_params params, std::size_t num_shards);
  sharded_connection_pool(const sharded_connection_pool &) = delete;
  auto operator=(const sharded_connection_pool &)
      -> sharded_connection_pool & = delete;
  auto async_run() -> task<void>;
  auto async_get_connection()
      -> task<std::expected<pooled_connection, std::error_code>>;
  auto async_get_connection(cancel_token &token)
      -> task<std::expected<pooled_connection, std::error_code>>;
  auto async_get_connection(io_context &io)
      -> task<std::expected<pooled_connection, std::error_code>>;
  auto async_get_connection(io_context &io, cancel_token &token)
      -> task<std::expected<pooled_connection, std::error_code>>;
  auto cancel() -> task<void>;
  auto size() const noexcept -> std::size_t;
  auto idle_count() const noexcept -> std::size_t;
  auto shard_count() const noexcept -> std::size_t;

private:
  pool_params base_params_;
  std::vector<std::unique_ptr<connection_pool>> shards_;
  std::vector<io_context *> shard_ctxs_;
  std::unordered_map<io_context *, std::size_t> shard_by_ctx_;
  std::atomic<std::size_t> next_shard_{0};
  io_context *fallback_ctx_ = nullptr;
  auto get_shard_index(io_context &io) -> std::size_t;
  auto try_borrow_immediate(std::size_t primary_idx)
      -> std::expected<pooled_connection, std::error_code>;
  auto select_wait_shard(std::size_t preferred_idx) -> std::size_t;
  void init_shards(const std::vector<io_context *> &worker_contexts,
                   std::size_t num_shards);
};
} // namespace cnetmod::mysql
