/// cnetmod.protocol.modbus:request — Modbus request builder interface

export module cnetmod.protocol.modbus:request;

import std;
import :types;

namespace cnetmod::modbus {

export class request_builder {
public:
  request_builder() = default;
  auto set_transport(transport_type type) -> request_builder &;
  auto set_unit_id(std::uint8_t unit_id) -> request_builder &;
  auto set_transaction_id(std::uint16_t tid) -> request_builder &;
  auto read_coils(std::uint16_t start_address, std::uint16_t quantity)
      -> modbus_request;
  auto read_discrete_inputs(std::uint16_t start_address, std::uint16_t quantity)
      -> modbus_request;
  auto read_holding_registers(std::uint16_t start_address,
                              std::uint16_t quantity) -> modbus_request;
  auto read_input_registers(std::uint16_t start_address, std::uint16_t quantity)
      -> modbus_request;
  auto write_single_coil(std::uint16_t address, bool value) -> modbus_request;
  auto write_single_register(std::uint16_t address, std::uint16_t value)
      -> modbus_request;
  auto write_multiple_coils(std::uint16_t start_address,
                            std::span<const bool> values) -> modbus_request;
  auto write_multiple_registers(std::uint16_t start_address,
                                std::span<const std::uint16_t> values)
      -> modbus_request;

private:
  transport_type transport_ = transport_type::tcp;
  std::uint8_t unit_id_ = 1;
  std::uint16_t transaction_id_ = 0;
  auto finalize(modbus_request &req) -> modbus_request;
};

} // namespace cnetmod::modbus
