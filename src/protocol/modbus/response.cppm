/// cnetmod.protocol.modbus:response — Modbus Response Parser
/// Parse Modbus responses and extract data

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:response;

import std;
import :types;

namespace cnetmod::modbus {

// =============================================================================
// Response Parser
// =============================================================================

export class response_parser {
public:
    explicit response_parser(const modbus_response& response) 
        : response_(response) {}

    // Check if response is an exception
    auto is_exception() const -> bool {
        return response_.is_exception;
    }

    // Get exception code
    auto get_exception() const -> exception_code {
        return response_.exception;
    }

    // Get function code
    auto get_function_code() const -> function_code {
        return response_.func_code;
    }

    // ── Parse Coils/Discrete Inputs ──
    auto parse_bits() const -> std::expected<std::vector<bool>, std::error_code> {
        if (response_.is_exception) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        if (response_.data.empty()) {
            return std::unexpected(std::make_error_code(std::errc::message_size));
        }

        std::uint8_t byte_count = response_.data[0];
        if (response_.data.size() < static_cast<std::size_t>(1 + byte_count)) {
            return std::unexpected(std::make_error_code(std::errc::message_size));
        }

        std::vector<bool> bits;
        for (std::size_t i = 0; i < byte_count; ++i) {
            std::uint8_t byte = response_.data[1 + i];
            for (int bit = 0; bit < 8; ++bit) {
                bits.push_back((byte & (1 << bit)) != 0);
            }
        }
        return bits;
    }

    // ── Parse Registers ──
    auto parse_registers() const -> std::expected<std::vector<std::uint16_t>, std::error_code> {
        if (response_.is_exception) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        if (response_.data.empty()) {
            return std::unexpected(std::make_error_code(std::errc::message_size));
        }

        std::uint8_t byte_count = response_.data[0];
        if (response_.data.size() < static_cast<std::size_t>(1 + byte_count)) {
            return std::unexpected(std::make_error_code(std::errc::message_size));
        }
        if (byte_count % 2 != 0) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        std::vector<std::uint16_t> registers;
        for (std::size_t i = 0; i < byte_count; i += 2) {
            std::uint16_t value = read_uint16_be(response_.data, 1 + i);
            registers.push_back(value);
        }
        return registers;
    }

    // ── Parse Write Response ──
    auto parse_write_response() const -> std::expected<std::pair<std::uint16_t, std::uint16_t>, std::error_code> {
        if (response_.is_exception) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        if (response_.data.size() < 4) {
            return std::unexpected(std::make_error_code(std::errc::message_size));
        }

        std::uint16_t address = read_uint16_be(response_.data, 0);
        std::uint16_t value = read_uint16_be(response_.data, 2);
        return std::make_pair(address, value);
    }

    // Get raw data
    auto get_raw_data() const -> std::span<const std::uint8_t> {
        return response_.data;
    }

private:
    const modbus_response& response_;
};

} // namespace cnetmod::modbus
