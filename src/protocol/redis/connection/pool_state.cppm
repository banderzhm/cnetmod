export module cnetmod.protocol.redis:pool_state;

import std;
import :client;
import cnetmod.coro.cancel;

export namespace cnetmod::redis {
enum class conn_state : std::uint8_t {
  initial,
  connecting,
  idle,
  in_use,
  pinging,
  dead
};
struct conn_node {
  std::unique_ptr<client> conn;
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
} // namespace cnetmod::redis
