module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.modbus:tcp_client;
import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.coro.task;
export namespace cnetmod::modbus {
class tcp_client {
public:
  explicit tcp_client(io_context &);
  ~tcp_client();
  tcp_client(const tcp_client &) = delete;
  auto operator=(const tcp_client &) -> tcp_client & = delete;
  auto connect(std::string_view, std::uint16_t) -> task<std::error_code>;
  auto execute(const modbus_request &)
      -> task<std::expected<modbus_response, std::error_code>>;
  auto execute_with_timeout(const modbus_request &,
                            std::chrono::steady_clock::duration)
      -> task<std::expected<modbus_response, std::error_code>>;
  auto reconnect() -> task<std::error_code>;
  void close();
  auto is_open() const -> bool;
  auto next_transaction_id() -> std::uint16_t;
  auto get_host() const -> const std::string &;
  auto get_port() const -> std::uint16_t;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::modbus
