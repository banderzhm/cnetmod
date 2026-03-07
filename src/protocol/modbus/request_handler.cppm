/// cnetmod.protocol.modbus:request_handler — Modbus Request Handler
/// Processes Modbus requests and generates responses

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:request_handler;

import std;
import :types;
import :data_store;

namespace cnetmod::modbus {

// =============================================================================
// Request Handler
// =============================================================================

export class request_handler {
public:
    explicit request_handler(data_store& store) : store_(store) {}

    auto handle(const modbus_request& request) -> modbus_response {
        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;

        try {
            switch (request.func_code) {
                case function_code::read_coils:
                    return handle_read_coils(request);
                case function_code::read_discrete_inputs:
                    return handle_read_discrete_inputs(request);
                case function_code::read_holding_registers:
                    return handle_read_holding_registers(request);
                case function_code::read_input_registers:
                    return handle_read_input_registers(request);
                case function_code::write_single_coil:
                    return handle_write_single_coil(request);
                case function_code::write_single_register:
                    return handle_write_single_register(request);
                case function_code::write_multiple_coils:
                    return handle_write_multiple_coils(request);
                case function_code::write_multiple_registers:
                    return handle_write_multiple_registers(request);
                default:
                    return make_exception_response(request, exception_code::illegal_function);
            }
        } catch (...) {
            return make_exception_response(request, exception_code::server_device_failure);
        }
    }

private:
    data_store& store_;

    auto make_exception_response(const modbus_request& request, exception_code ec) 
        -> modbus_response 
    {
        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        response.is_exception = true;
        response.exception = ec;
        response.data.push_back(static_cast<std::uint8_t>(ec));
        return response;
    }

    auto handle_read_coils(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 4) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t start_address = read_uint16_be(request.data, 0);
        std::uint16_t quantity = read_uint16_be(request.data, 2);

        if (quantity == 0 || quantity > 2000) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;

        std::uint8_t byte_count = static_cast<std::uint8_t>((quantity + 7) / 8);
        response.data.push_back(byte_count);

        for (std::size_t i = 0; i < byte_count; ++i) {
            std::uint8_t byte = 0;
            for (std::size_t bit = 0; bit < 8 && (i * 8 + bit) < quantity; ++bit) {
                auto result = store_.read_coil(start_address + static_cast<std::uint16_t>(i * 8 + bit));
                if (!result) {
                    return make_exception_response(request, result.error());
                }
                if (*result) {
                    byte |= (1 << bit);
                }
            }
            response.data.push_back(byte);
        }

        return response;
    }

    auto handle_read_discrete_inputs(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 4) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t start_address = read_uint16_be(request.data, 0);
        std::uint16_t quantity = read_uint16_be(request.data, 2);

        if (quantity == 0 || quantity > 2000) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;

        std::uint8_t byte_count = static_cast<std::uint8_t>((quantity + 7) / 8);
        response.data.push_back(byte_count);

        for (std::size_t i = 0; i < byte_count; ++i) {
            std::uint8_t byte = 0;
            for (std::size_t bit = 0; bit < 8 && (i * 8 + bit) < quantity; ++bit) {
                auto result = store_.read_discrete_input(start_address + static_cast<std::uint16_t>(i * 8 + bit));
                if (!result) {
                    return make_exception_response(request, result.error());
                }
                if (*result) {
                    byte |= (1 << bit);
                }
            }
            response.data.push_back(byte);
        }

        return response;
    }

    auto handle_read_holding_registers(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 4) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t start_address = read_uint16_be(request.data, 0);
        std::uint16_t quantity = read_uint16_be(request.data, 2);

        if (quantity == 0 || quantity > 125) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        response.data.push_back(static_cast<std::uint8_t>(quantity * 2));

        for (std::uint16_t i = 0; i < quantity; ++i) {
            auto result = store_.read_holding_register(start_address + i);
            if (!result) {
                return make_exception_response(request, result.error());
            }
            write_uint16_be(response.data, *result);
        }

        return response;
    }

    auto handle_read_input_registers(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 4) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t start_address = read_uint16_be(request.data, 0);
        std::uint16_t quantity = read_uint16_be(request.data, 2);

        if (quantity == 0 || quantity > 125) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        response.data.push_back(static_cast<std::uint8_t>(quantity * 2));

        for (std::uint16_t i = 0; i < quantity; ++i) {
            auto result = store_.read_input_register(start_address + i);
            if (!result) {
                return make_exception_response(request, result.error());
            }
            write_uint16_be(response.data, *result);
        }

        return response;
    }

    auto handle_write_single_coil(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 4) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t address = read_uint16_be(request.data, 0);
        std::uint16_t value = read_uint16_be(request.data, 2);

        if (value != 0x0000 && value != 0xFF00) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        auto result = store_.write_coil(address, value == 0xFF00);
        if (!result) {
            return make_exception_response(request, result.error());
        }

        // Echo request as response
        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        response.data = request.data;
        return response;
    }

    auto handle_write_single_register(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 4) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t address = read_uint16_be(request.data, 0);
        std::uint16_t value = read_uint16_be(request.data, 2);

        auto result = store_.write_holding_register(address, value);
        if (!result) {
            return make_exception_response(request, result.error());
        }

        // Echo request as response
        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        response.data = request.data;
        return response;
    }

    auto handle_write_multiple_coils(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 5) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t start_address = read_uint16_be(request.data, 0);
        std::uint16_t quantity = read_uint16_be(request.data, 2);
        std::uint8_t byte_count = request.data[4];

        if (quantity == 0 || quantity > 1968 || byte_count != (quantity + 7) / 8) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        if (request.data.size() < static_cast<std::size_t>(5 + byte_count)) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        for (std::uint16_t i = 0; i < quantity; ++i) {
            std::size_t byte_idx = 5 + i / 8;
            std::size_t bit_idx = i % 8;
            bool value = (request.data[byte_idx] & (1 << bit_idx)) != 0;
            
            auto result = store_.write_coil(start_address + i, value);
            if (!result) {
                return make_exception_response(request, result.error());
            }
        }

        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        write_uint16_be(response.data, start_address);
        write_uint16_be(response.data, quantity);
        return response;
    }

    auto handle_write_multiple_registers(const modbus_request& request) -> modbus_response {
        if (request.data.size() < 5) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        std::uint16_t start_address = read_uint16_be(request.data, 0);
        std::uint16_t quantity = read_uint16_be(request.data, 2);
        std::uint8_t byte_count = request.data[4];

        if (quantity == 0 || quantity > 123 || byte_count != quantity * 2) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        if (request.data.size() < static_cast<std::size_t>(5 + byte_count)) {
            return make_exception_response(request, exception_code::illegal_data_value);
        }

        for (std::uint16_t i = 0; i < quantity; ++i) {
            std::uint16_t value = read_uint16_be(request.data, 5 + i * 2);
            auto result = store_.write_holding_register(start_address + i, value);
            if (!result) {
                return make_exception_response(request, result.error());
            }
        }

        modbus_response response;
        response.header = request.header;
        response.func_code = request.func_code;
        write_uint16_be(response.data, start_address);
        write_uint16_be(response.data, quantity);
        return response;
    }
};

} // namespace cnetmod::modbus
