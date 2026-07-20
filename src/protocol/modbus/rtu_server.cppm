module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.modbus:rtu_server;
import std;
import :types;
import :data_store;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.core.serial_port;
export namespace cnetmod::modbus {
struct rtu_server_config {
  std::string port_name;
  std::uint32_t baudrate = 9600;
  std::uint8_t data_bits = 8;
  stop_bits stop = stop_bits::one;
  parity par = parity::none;
  std::uint8_t unit_id = 1;
  std::chrono::microseconds char_timeout = std::chrono::microseconds(1500);
  std::chrono::microseconds frame_delay = std::chrono::microseconds(3500);
  auto to_serial_config() const -> serial_config;
};
class rtu_server {
public:
  rtu_server(io_context &, data_store &);
  ~rtu_server();
  rtu_server(const rtu_server &) = delete;
  auto operator=(const rtu_server &) -> rtu_server & = delete;
  auto start(const rtu_server_config &) -> task<std::error_code>;
  void stop();
  auto is_running() const -> bool;
  auto get_config() const -> const rtu_server_config &;

private:
  class impl;
  std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::modbus
