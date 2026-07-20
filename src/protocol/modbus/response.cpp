module cnetmod.protocol.modbus;
import std;
import :response;
namespace cnetmod::modbus {
response_parser::response_parser(const modbus_response &response)
    : response_(response) {}
auto response_parser::is_exception() const -> bool {
  return response_.is_exception;
}
auto response_parser::get_exception() const -> exception_code {
  return response_.exception;
}
auto response_parser::get_function_code() const -> function_code {
  return response_.func_code;
}
auto response_parser::parse_bits() const
    -> std::expected<std::vector<bool>, std::error_code> {
  if (response_.is_exception)
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  if (response_.data.empty())
    return std::unexpected(std::make_error_code(std::errc::message_size));
  const auto count = response_.data[0];
  if (response_.data.size() < 1U + count)
    return std::unexpected(std::make_error_code(std::errc::message_size));
  std::vector<bool> bits;
  for (std::size_t i{}; i < count; ++i)
    for (int bit{}; bit < 8; ++bit)
      bits.push_back((response_.data[1 + i] & (1 << bit)) != 0);
  return bits;
}
auto response_parser::parse_registers() const
    -> std::expected<std::vector<std::uint16_t>, std::error_code> {
  if (response_.is_exception)
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  if (response_.data.empty())
    return std::unexpected(std::make_error_code(std::errc::message_size));
  const auto count = response_.data[0];
  if (response_.data.size() < 1U + count)
    return std::unexpected(std::make_error_code(std::errc::message_size));
  if (count % 2)
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  std::vector<std::uint16_t> out;
  for (std::size_t i{}; i < count; i += 2)
    out.push_back(read_uint16_be(response_.data, 1 + i));
  return out;
}
auto response_parser::parse_write_response() const
    -> std::expected<std::pair<std::uint16_t, std::uint16_t>, std::error_code> {
  if (response_.is_exception)
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  if (response_.data.size() < 4)
    return std::unexpected(std::make_error_code(std::errc::message_size));
  return std::pair{read_uint16_be(response_.data, 0),
                   read_uint16_be(response_.data, 2)};
}
auto response_parser::get_raw_data() const -> std::span<const std::uint8_t> {
  return response_.data;
}
} // namespace cnetmod::modbus
