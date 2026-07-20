module cnetmod.protocol.modbus;

import std;
import :request;

namespace cnetmod::modbus {

auto request_builder::set_transport(transport_type type) -> request_builder & {
  transport_ = type;
  return *this;
}
auto request_builder::set_unit_id(std::uint8_t unit_id) -> request_builder & {
  unit_id_ = unit_id;
  return *this;
}
auto request_builder::set_transaction_id(std::uint16_t tid)
    -> request_builder & {
  transaction_id_ = tid;
  return *this;
}
auto request_builder::read_coils(std::uint16_t start, std::uint16_t quantity)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::read_coils;
  write_uint16_be(req.data, start);
  write_uint16_be(req.data, quantity);
  return finalize(req);
}
auto request_builder::read_discrete_inputs(std::uint16_t start,
                                           std::uint16_t quantity)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::read_discrete_inputs;
  write_uint16_be(req.data, start);
  write_uint16_be(req.data, quantity);
  return finalize(req);
}
auto request_builder::read_holding_registers(std::uint16_t start,
                                             std::uint16_t quantity)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::read_holding_registers;
  write_uint16_be(req.data, start);
  write_uint16_be(req.data, quantity);
  return finalize(req);
}
auto request_builder::read_input_registers(std::uint16_t start,
                                           std::uint16_t quantity)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::read_input_registers;
  write_uint16_be(req.data, start);
  write_uint16_be(req.data, quantity);
  return finalize(req);
}
auto request_builder::write_single_coil(std::uint16_t address, bool value)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::write_single_coil;
  write_uint16_be(req.data, address);
  write_uint16_be(req.data, value ? 0xFF00 : 0x0000);
  return finalize(req);
}
auto request_builder::write_single_register(std::uint16_t address,
                                            std::uint16_t value)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::write_single_register;
  write_uint16_be(req.data, address);
  write_uint16_be(req.data, value);
  return finalize(req);
}
auto request_builder::write_multiple_coils(std::uint16_t start,
                                           std::span<const bool> values)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::write_multiple_coils;
  write_uint16_be(req.data, start);
  write_uint16_be(req.data, static_cast<std::uint16_t>(values.size()));
  const auto count = static_cast<std::uint8_t>((values.size() + 7) / 8);
  req.data.push_back(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::uint8_t byte = 0;
    for (std::size_t bit = 0; bit < 8 && i * 8 + bit < values.size(); ++bit)
      if (values[i * 8 + bit])
        byte |= static_cast<std::uint8_t>(1U << bit);
    req.data.push_back(byte);
  }
  return finalize(req);
}
auto request_builder::write_multiple_registers(
    std::uint16_t start, std::span<const std::uint16_t> values)
    -> modbus_request {
  modbus_request req;
  req.func_code = function_code::write_multiple_registers;
  write_uint16_be(req.data, start);
  write_uint16_be(req.data, static_cast<std::uint16_t>(values.size()));
  req.data.push_back(static_cast<std::uint8_t>(values.size() * 2));
  for (auto value : values)
    write_uint16_be(req.data, value);
  return finalize(req);
}
auto request_builder::finalize(modbus_request &req) -> modbus_request {
  if (transport_ == transport_type::tcp || transport_ == transport_type::udp) {
    req.header.transaction_id = transaction_id_++;
    req.header.protocol_id = 0;
    req.header.length = static_cast<std::uint16_t>(2 + req.data.size());
    req.header.unit_id = unit_id_;
  } else {
    req.header.unit_id = unit_id_;
  }
  return req;
}

} // namespace cnetmod::modbus
