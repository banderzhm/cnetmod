/// cnetmod.protocol.modbus:request — Modbus Request Builder
/// Build Modbus requests for various function codes

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:request;

import std;
import :types;

namespace cnetmod::modbus {

// =============================================================================
// Request Builder
// =============================================================================

export class request_builder {
public:
    request_builder() = default;

    // Set transport type
    auto set_transport(transport_type type) -> request_builder& {
        transport_ = type;
        return *this;
    }

    // Set unit ID (slave address)
    auto set_unit_id(std::uint8_t unit_id) -> request_builder& {
        unit_id_ = unit_id;
        return *this;
    }

    // Set transaction ID (TCP/UDP only)
    auto set_transaction_id(std::uint16_t tid) -> request_builder& {
        transaction_id_ = tid;
        return *this;
    }

    // ── Read Coils (0x01) ──
    auto read_coils(std::uint16_t start_address, std::uint16_t quantity) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::read_coils;
        write_uint16_be(req.data, start_address);
        write_uint16_be(req.data, quantity);
        return finalize(req);
    }

    // ── Read Discrete Inputs (0x02) ──
    auto read_discrete_inputs(std::uint16_t start_address, std::uint16_t quantity) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::read_discrete_inputs;
        write_uint16_be(req.data, start_address);
        write_uint16_be(req.data, quantity);
        return finalize(req);
    }

    // ── Read Holding Registers (0x03) ──
    auto read_holding_registers(std::uint16_t start_address, std::uint16_t quantity) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::read_holding_registers;
        write_uint16_be(req.data, start_address);
        write_uint16_be(req.data, quantity);
        return finalize(req);
    }

    // ── Read Input Registers (0x04) ──
    auto read_input_registers(std::uint16_t start_address, std::uint16_t quantity) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::read_input_registers;
        write_uint16_be(req.data, start_address);
        write_uint16_be(req.data, quantity);
        return finalize(req);
    }

    // ── Write Single Coil (0x05) ──
    auto write_single_coil(std::uint16_t address, bool value) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::write_single_coil;
        write_uint16_be(req.data, address);
        write_uint16_be(req.data, value ? 0xFF00 : 0x0000);
        return finalize(req);
    }

    // ── Write Single Register (0x06) ──
    auto write_single_register(std::uint16_t address, std::uint16_t value) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::write_single_register;
        write_uint16_be(req.data, address);
        write_uint16_be(req.data, value);
        return finalize(req);
    }

    // ── Write Multiple Coils (0x0F) ──
    auto write_multiple_coils(std::uint16_t start_address, std::span<const bool> values) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::write_multiple_coils;
        write_uint16_be(req.data, start_address);
        write_uint16_be(req.data, static_cast<std::uint16_t>(values.size()));
        
        std::uint8_t byte_count = static_cast<std::uint8_t>((values.size() + 7) / 8);
        req.data.push_back(byte_count);
        
        for (std::size_t i = 0; i < byte_count; ++i) {
            std::uint8_t byte = 0;
            for (std::size_t bit = 0; bit < 8 && (i * 8 + bit) < values.size(); ++bit) {
                if (values[i * 8 + bit]) {
                    byte |= (1 << bit);
                }
            }
            req.data.push_back(byte);
        }
        return finalize(req);
    }

    // ── Write Multiple Registers (0x10) ──
    auto write_multiple_registers(std::uint16_t start_address, std::span<const std::uint16_t> values) -> modbus_request {
        modbus_request req;
        req.func_code = function_code::write_multiple_registers;
        write_uint16_be(req.data, start_address);
        write_uint16_be(req.data, static_cast<std::uint16_t>(values.size()));
        req.data.push_back(static_cast<std::uint8_t>(values.size() * 2));
        
        for (auto value : values) {
            write_uint16_be(req.data, value);
        }
        return finalize(req);
    }

private:
    transport_type transport_ = transport_type::tcp;
    std::uint8_t unit_id_ = 1;
    std::uint16_t transaction_id_ = 0;

    auto finalize(modbus_request& req) -> modbus_request {
        if (transport_ == transport_type::tcp || transport_ == transport_type::udp) {
            req.header.transaction_id = transaction_id_++;
            req.header.protocol_id = 0;
            req.header.length = static_cast<std::uint16_t>(2 + req.data.size());
            req.header.unit_id = unit_id_;
        } else {
            // RTU/ASCII: no MBAP header, just unit_id in PDU
            req.header.unit_id = unit_id_;
        }
        return req;
    }
};

} // namespace cnetmod::modbus
