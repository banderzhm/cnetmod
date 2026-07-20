/// cnetmod.protocol.modbus:types — Modbus Protocol Types
/// Modbus function codes, data types, and error codes
/// Supports TCP, UDP, and RTU (Serial) transports

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.modbus;
import :types;

import std;

namespace cnetmod::modbus {

// =============================================================================
// Helper Functions
// =============================================================================

auto is_exception_response(std::uint8_t func_code) -> bool {
  return (func_code & 0x80) != 0;
}

auto get_exception_function_code(function_code fc) -> std::uint8_t {
  return static_cast<std::uint8_t>(fc) | 0x80;
}

auto function_code_name(function_code fc) -> std::string_view {
  switch (fc) {
  case function_code::read_coils:
    return "Read Coils";
  case function_code::read_discrete_inputs:
    return "Read Discrete Inputs";
  case function_code::write_single_coil:
    return "Write Single Coil";
  case function_code::write_multiple_coils:
    return "Write Multiple Coils";
  case function_code::read_holding_registers:
    return "Read Holding Registers";
  case function_code::read_input_registers:
    return "Read Input Registers";
  case function_code::write_single_register:
    return "Write Single Register";
  case function_code::write_multiple_registers:
    return "Write Multiple Registers";
  case function_code::read_write_registers:
    return "Read/Write Multiple Registers";
  default:
    return "Unknown";
  }
}

auto exception_code_name(exception_code ec) -> std::string_view {
  switch (ec) {
  case exception_code::illegal_function:
    return "Illegal Function";
  case exception_code::illegal_data_address:
    return "Illegal Data Address";
  case exception_code::illegal_data_value:
    return "Illegal Data Value";
  case exception_code::server_device_failure:
    return "Server Device Failure";
  case exception_code::acknowledge:
    return "Acknowledge";
  case exception_code::server_device_busy:
    return "Server Device Busy";
  case exception_code::memory_parity_error:
    return "Memory Parity Error";
  case exception_code::gateway_path_unavailable:
    return "Gateway Path Unavailable";
  case exception_code::gateway_target_failed:
    return "Gateway Target Failed";
  default:
    return "Unknown";
  }
}

// =============================================================================
// Serialization Helpers
// =============================================================================

void write_uint16_be(std::vector<std::uint8_t> &buf, std::uint16_t value) {
  buf.push_back(static_cast<std::uint8_t>(value >> 8));
  buf.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void write_uint16_le(std::vector<std::uint8_t> &buf, std::uint16_t value) {
  buf.push_back(static_cast<std::uint8_t>(value & 0xFF));
  buf.push_back(static_cast<std::uint8_t>(value >> 8));
}

auto read_uint16_be(std::span<const std::uint8_t> buf, std::size_t offset)
    -> std::uint16_t {
  return (static_cast<std::uint16_t>(buf[offset]) << 8) | buf[offset + 1];
}

auto read_uint16_le(std::span<const std::uint8_t> buf, std::size_t offset)
    -> std::uint16_t {
  return buf[offset] | (static_cast<std::uint16_t>(buf[offset + 1]) << 8);
}

// =============================================================================
// CRC16 for Modbus RTU
// =============================================================================

auto calculate_crc16(std::span<const std::uint8_t> data) -> std::uint16_t {
  std::uint16_t crc = 0xFFFF;
  for (auto byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// =============================================================================
// LRC for Modbus ASCII
// =============================================================================

auto calculate_lrc(std::span<const std::uint8_t> data) -> std::uint8_t {
  std::uint8_t lrc = 0;
  for (auto byte : data) {
    lrc += byte;
  }
  return static_cast<std::uint8_t>(-static_cast<std::int8_t>(lrc));
}

// =============================================================================
// modbus_request Implementation
// =============================================================================

auto modbus_request::serialize() const -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> buffer;

  // For TCP/UDP: include MBAP header
  if (header.protocol_id == 0) { // TCP/UDP uses protocol_id = 0
    buffer.reserve(12 + data.size());

    // MBAP Header
    write_uint16_be(buffer, header.transaction_id);
    write_uint16_be(buffer, header.protocol_id);
    write_uint16_be(
        buffer, static_cast<std::uint16_t>(
                    2 + data.size())); // length = unit_id + func_code + data
    buffer.push_back(header.unit_id);
  } else {
    // RTU: no MBAP header, just unit_id at start
    buffer.reserve(3 + data.size());
    buffer.push_back(header.unit_id);
  }

  // PDU (Protocol Data Unit)
  buffer.push_back(static_cast<std::uint8_t>(func_code));
  buffer.insert(buffer.end(), data.begin(), data.end());

  return buffer;
}

// =============================================================================
// modbus_response Implementation
// =============================================================================

auto modbus_response::parse(std::span<const std::uint8_t> buffer)
    -> std::expected<modbus_response, std::error_code> {
  if (buffer.size() < 8) {
    return std::unexpected(std::make_error_code(std::errc::message_size));
  }

  modbus_response response;

  // Parse MBAP Header (TCP/UDP)
  response.header.transaction_id = read_uint16_be(buffer, 0);
  response.header.protocol_id = read_uint16_be(buffer, 2);
  response.header.length = read_uint16_be(buffer, 4);
  response.header.unit_id = buffer[6];

  // Validate protocol ID
  if (response.header.protocol_id != 0) {
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  }

  // Validate length
  if (buffer.size() <
      static_cast<std::size_t>(7 + response.header.length - 1)) {
    return std::unexpected(std::make_error_code(std::errc::message_size));
  }

  // Parse Function Code
  std::uint8_t func_byte = buffer[7];
  response.is_exception = is_exception_response(func_byte);

  if (response.is_exception) {
    response.func_code = static_cast<function_code>(func_byte & 0x7F);
    if (buffer.size() >= 9) {
      response.exception = static_cast<exception_code>(buffer[8]);
    }
  } else {
    response.func_code = static_cast<function_code>(func_byte);
    // Copy data
    if (buffer.size() > 8) {
      response.data.assign(buffer.begin() + 8, buffer.end());
    }
  }

  return response;
}

// =============================================================================
// modbus_response::serialize Implementation
// =============================================================================

auto modbus_response::serialize() const -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> buffer;

  // For TCP/UDP: include MBAP header
  if (header.protocol_id == 0) { // TCP/UDP uses protocol_id = 0
    buffer.reserve(12 + data.size());

    // MBAP Header
    write_uint16_be(buffer, header.transaction_id);
    write_uint16_be(buffer, header.protocol_id);
    write_uint16_be(
        buffer, static_cast<std::uint16_t>(
                    2 + data.size())); // length = unit_id + func_code + data
    buffer.push_back(header.unit_id);
  } else {
    // RTU: no MBAP header, just unit_id at start
    buffer.reserve(3 + data.size());
    buffer.push_back(header.unit_id);
  }

  // PDU (Protocol Data Unit)
  if (is_exception) {
    buffer.push_back(get_exception_function_code(func_code));
    buffer.push_back(static_cast<std::uint8_t>(exception));
  } else {
    buffer.push_back(static_cast<std::uint8_t>(func_code));
    buffer.insert(buffer.end(), data.begin(), data.end());
  }

  return buffer;
}

// =============================================================================
// RTU Frame Parsing (for Serial)
// =============================================================================

auto parse_rtu_frame(std::span<const std::uint8_t> buffer)
    -> std::expected<modbus_response, std::error_code> {
  if (buffer.size() <
      5) { // min: unit_id + func_code + 1 byte data + 2 bytes CRC
    return std::unexpected(std::make_error_code(std::errc::message_size));
  }

  // Verify CRC
  std::uint16_t received_crc = read_uint16_le(buffer, buffer.size() - 2);
  std::uint16_t calculated_crc =
      calculate_crc16(buffer.subspan(0, buffer.size() - 2));

  if (received_crc != calculated_crc) {
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  }

  modbus_response response;
  response.header.unit_id = buffer[0];
  response.header.protocol_id = 0xFFFF; // Mark as RTU

  std::uint8_t func_byte = buffer[1];
  response.is_exception = is_exception_response(func_byte);

  if (response.is_exception) {
    response.func_code = static_cast<function_code>(func_byte & 0x7F);
    if (buffer.size() >= 5) {
      response.exception = static_cast<exception_code>(buffer[2]);
    }
  } else {
    response.func_code = static_cast<function_code>(func_byte);
    // Copy data (excluding unit_id, func_code, and CRC)
    if (buffer.size() > 4) {
      response.data.assign(buffer.begin() + 2, buffer.end() - 2);
    }
  }

  return response;
}

// =============================================================================
// Serialize RTU Frame
// =============================================================================

auto serialize_rtu_frame(const modbus_request &request)
    -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> frame;
  frame.reserve(3 + request.data.size() + 2);

  // Unit ID
  frame.push_back(request.header.unit_id);

  // Function Code
  frame.push_back(static_cast<std::uint8_t>(request.func_code));

  // Data
  frame.insert(frame.end(), request.data.begin(), request.data.end());

  // Calculate and append CRC
  std::uint16_t crc = calculate_crc16(frame);
  write_uint16_le(frame, crc);

  return frame;
}

} // namespace cnetmod::modbus
