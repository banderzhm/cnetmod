module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:pool;

import std;
import :types;
import :tcp_client;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;

namespace cnetmod::modbus {

export struct pool_params {
  std::string host = "127.0.0.1";
  std::uint16_t port = 502;
  std::size_t initial_size = 1;
  std::size_t max_size = 16;
  std::chrono::steady_clock::duration connect_timeout =
      std::chrono::seconds(10);
  std::chrono::steady_clock::duration pool_timeout = std::chrono::seconds(5);
  std::chrono::steady_clock::duration retry_interval = std::chrono::seconds(30);
  std::chrono::steady_clock::duration health_check_interval =
      std::chrono::minutes(5);
};

enum class conn_state : std::uint8_t {
  initial,
  connecting,
  idle,
  in_use,
  dead
};
struct conn_node {
  std::unique_ptr<tcp_client> conn;
  std::atomic<conn_state> state = conn_state::initial;
  std::chrono::steady_clock::time_point last_used;
  std::coroutine_handle<> task_waiting{};
  std::size_t index = 0;
  conn_node() = default;
  conn_node(conn_node &&) = delete;
  auto operator=(conn_node &&) -> conn_node & = delete;
  conn_node(const conn_node &) = delete;
  auto operator=(const conn_node &) -> conn_node & = delete;
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
  auto get() noexcept -> tcp_client &;
  auto get() const noexcept -> const tcp_client &;
  auto operator->() noexcept -> tcp_client *;
  auto operator->() const noexcept -> const tcp_client *;

private:
  friend class connection_pool;
  connection_pool *pool_ = nullptr;
  conn_node *node_ = nullptr;
  pooled_connection(connection_pool *pool, conn_node *node) noexcept;
  void return_to_pool();
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
  auto count_ready_connections() const noexcept -> std::size_t;
  auto connection_task(conn_node &node) -> task<void>;
  void spawn_connection();
  static void dec_if_positive(std::atomic<std::size_t> &counter);
  auto try_get_idle_locked() -> conn_node *;
  auto try_get_idle_lockfree() -> conn_node *;
  void notify_waiters_with_idle_locked();
  auto remove_waiter(pool_waiter *target) -> bool;
  void return_connection(conn_node &node);
};

} // namespace cnetmod::modbus
