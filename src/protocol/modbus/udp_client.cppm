module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.modbus:udp_client;
import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.coro.task;
export namespace cnetmod::modbus {
class udp_client {
public:
  explicit udp_client(io_context &);
  ~udp_client();
  udp_client(const udp_client &) = delete;
  auto operator=(const udp_client &) -> udp_client & = delete;
  auto connect(std::string_view, std::uint16_t) -> task<std::error_code>;
  auto execute(const modbus_request &)
      -> task<std::expected<modbus_response, std::error_code>>;
  auto execute_with_retry(const modbus_request &, int = 3)
      -> task<std::expected<modbus_response, std::error_code>>;
  void close();
  auto is_open() const -> bool;
  auto next_transaction_id() -> std::uint16_t;
  auto get_remote_host() const -> const std::string &;
  auto get_remote_port() const -> std::uint16_t;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::modbus
