module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:types;
import std;

export namespace cnetmod::modbus {

enum class transport_type { tcp, udp, rtu, ascii };
enum class function_code : std::uint8_t {
  read_coils = 0x01,
  read_discrete_inputs = 0x02,
  write_single_coil = 0x05,
  write_multiple_coils = 0x0F,
  read_holding_registers = 0x03,
  read_input_registers = 0x04,
  write_single_register = 0x06,
  write_multiple_registers = 0x10,
  read_write_registers = 0x17,
  read_exception_status = 0x07,
  diagnostics = 0x08,
  get_comm_event_counter = 0x0B,
  get_comm_event_log = 0x0C,
  report_server_id = 0x11,
  read_file_record = 0x14,
  write_file_record = 0x15,
  read_fifo_queue = 0x18,
  encapsulated_interface = 0x2B
};
enum class exception_code : std::uint8_t {
  illegal_function = 0x01,
  illegal_data_address = 0x02,
  illegal_data_value = 0x03,
  server_device_failure = 0x04,
  acknowledge = 0x05,
  server_device_busy = 0x06,
  memory_parity_error = 0x08,
  gateway_path_unavailable = 0x0A,
  gateway_target_failed = 0x0B
};
struct mbap_header {
  std::uint16_t transaction_id = 0;
  std::uint16_t protocol_id = 0;
  std::uint16_t length = 0;
  std::uint8_t unit_id = 1;
};
struct modbus_request {
  mbap_header header;
  function_code func_code;
  std::vector<std::uint8_t> data;
  auto serialize() const -> std::vector<std::uint8_t>;
};
struct modbus_response {
  mbap_header header;
  function_code func_code;
  std::vector<std::uint8_t> data;
  bool is_exception = false;
  exception_code exception = exception_code::illegal_function;
  static auto parse(std::span<const std::uint8_t> buffer)
      -> std::expected<modbus_response, std::error_code>;
  auto serialize() const -> std::vector<std::uint8_t>;
};
auto is_exception_response(std::uint8_t func_code) -> bool;
auto get_exception_function_code(function_code fc) -> std::uint8_t;
auto function_code_name(function_code fc) -> std::string_view;
auto exception_code_name(exception_code ec) -> std::string_view;
auto calculate_crc16(std::span<const std::uint8_t> data) -> std::uint16_t;
auto calculate_lrc(std::span<const std::uint8_t> data) -> std::uint8_t;
auto parse_rtu_frame(std::span<const std::uint8_t> buffer)
    -> std::expected<modbus_response, std::error_code>;
auto serialize_rtu_frame(const modbus_request &request)
    -> std::vector<std::uint8_t>;

// Module-internal wire helpers used by sibling Modbus partitions.
void write_uint16_be(std::vector<std::uint8_t> &buffer, std::uint16_t value);
void write_uint16_le(std::vector<std::uint8_t> &buffer, std::uint16_t value);
auto read_uint16_be(std::span<const std::uint8_t> buffer, std::size_t offset)
    -> std::uint16_t;
auto read_uint16_le(std::span<const std::uint8_t> buffer, std::size_t offset)
    -> std::uint16_t;
} // namespace cnetmod::modbus
