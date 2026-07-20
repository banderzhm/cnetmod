module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.modbus:rtu_client;
import std;
import :types;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.core.serial_port;
import cnetmod.core.file;
export namespace cnetmod::modbus {
struct rtu_config {
  std::string port_name;
  std::uint32_t baudrate = 9600;
  std::uint8_t data_bits = 8;
  stop_bits stop = stop_bits::one;
  parity par = parity::none;
  std::chrono::microseconds char_timeout = std::chrono::microseconds(1500);
  std::chrono::microseconds frame_delay = std::chrono::microseconds(3500);
  auto to_serial_config() const -> serial_config;
};
class rtu_client {
public:
  explicit rtu_client(io_context &);
  ~rtu_client();
  rtu_client(const rtu_client &) = delete;
  auto operator=(const rtu_client &) -> rtu_client & = delete;
  auto open(const rtu_config &) -> task<std::error_code>;
  auto execute(const modbus_request &)
      -> task<std::expected<modbus_response, std::error_code>>;
  auto execute_with_retry(const modbus_request &, int = 3)
      -> task<std::expected<modbus_response, std::error_code>>;
  void close();
  auto is_open() const -> bool;
  auto get_config() const -> const rtu_config &;
  auto native_handle() const -> file_handle_t;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::modbus
