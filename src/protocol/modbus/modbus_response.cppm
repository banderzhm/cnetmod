module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.modbus:response;
// Protocol-qualified filename prevents duplicate source basenames.
import std;
import :types;
namespace cnetmod::modbus {
export class response_parser {
public:
  explicit response_parser(const modbus_response &response);
  auto is_exception() const -> bool;
  auto get_exception() const -> exception_code;
  auto get_function_code() const -> function_code;
  auto parse_bits() const -> std::expected<std::vector<bool>, std::error_code>;
  auto parse_registers() const
      -> std::expected<std::vector<std::uint16_t>, std::error_code>;
  auto parse_write_response() const
      -> std::expected<std::pair<std::uint16_t, std::uint16_t>,
                       std::error_code>;
  auto get_raw_data() const -> std::span<const std::uint8_t>;

private:
  const modbus_response &response_;
};
} // namespace cnetmod::modbus
