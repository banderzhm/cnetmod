/// cnetmod.protocol.modbus:data_store — Modbus Data Store Interface
/// Abstract interface and memory implementation for Modbus data

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.modbus:data_store;

import std;
import :types;

namespace cnetmod::modbus {

// =============================================================================
// Data Store Interface
// =============================================================================

export class data_store {
public:
    virtual ~data_store() = default;

    // Coils (0x - Read/Write)
    virtual auto read_coil(std::uint16_t address) 
        -> std::expected<bool, exception_code> = 0;
    virtual auto write_coil(std::uint16_t address, bool value) 
        -> std::expected<void, exception_code> = 0;

    // Discrete Inputs (1x - Read Only)
    virtual auto read_discrete_input(std::uint16_t address) 
        -> std::expected<bool, exception_code> = 0;

    // Holding Registers (4x - Read/Write)
    virtual auto read_holding_register(std::uint16_t address) 
        -> std::expected<std::uint16_t, exception_code> = 0;
    virtual auto write_holding_register(std::uint16_t address, std::uint16_t value) 
        -> std::expected<void, exception_code> = 0;

    // Input Registers (3x - Read Only)
    virtual auto read_input_register(std::uint16_t address) 
        -> std::expected<std::uint16_t, exception_code> = 0;
};

// =============================================================================
// Simple In-Memory Data Store
// =============================================================================

export class memory_data_store : public data_store {
public:
    memory_data_store(std::size_t coil_count = 10000,
                     std::size_t discrete_input_count = 10000,
                     std::size_t holding_register_count = 10000,
                     std::size_t input_register_count = 10000)
        : coils_(coil_count, false)
        , discrete_inputs_(discrete_input_count, false)
        , holding_registers_(holding_register_count, 0)
        , input_registers_(input_register_count, 0)
    {}

    // ── Coils ──
    auto read_coil(std::uint16_t address) 
        -> std::expected<bool, exception_code> override 
    {
        std::lock_guard lock(mtx_);
        if (address >= coils_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        return coils_[address];
    }

    auto write_coil(std::uint16_t address, bool value) 
        -> std::expected<void, exception_code> override 
    {
        std::lock_guard lock(mtx_);
        if (address >= coils_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        coils_[address] = value;
        return {};
    }

    // ── Discrete Inputs ──
    auto read_discrete_input(std::uint16_t address) 
        -> std::expected<bool, exception_code> override 
    {
        std::lock_guard lock(mtx_);
        if (address >= discrete_inputs_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        return discrete_inputs_[address];
    }

    // ── Holding Registers ──
    auto read_holding_register(std::uint16_t address) 
        -> std::expected<std::uint16_t, exception_code> override 
    {
        std::lock_guard lock(mtx_);
        if (address >= holding_registers_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        return holding_registers_[address];
    }

    auto write_holding_register(std::uint16_t address, std::uint16_t value) 
        -> std::expected<void, exception_code> override 
    {
        std::lock_guard lock(mtx_);
        if (address >= holding_registers_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        holding_registers_[address] = value;
        return {};
    }

    // ── Input Registers ──
    auto read_input_register(std::uint16_t address) 
        -> std::expected<std::uint16_t, exception_code> override 
    {
        std::lock_guard lock(mtx_);
        if (address >= input_registers_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        return input_registers_[address];
    }

    // ── Direct access for testing/initialization ──
    auto& get_coils() { return coils_; }
    auto& get_discrete_inputs() { return discrete_inputs_; }
    auto& get_holding_registers() { return holding_registers_; }
    auto& get_input_registers() { return input_registers_; }

    // ── Batch operations ──
    auto write_coils_batch(std::uint16_t start_address, std::span<const bool> values)
        -> std::expected<void, exception_code>
    {
        std::lock_guard lock(mtx_);
        if (start_address + values.size() > coils_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            coils_[start_address + i] = values[i];
        }
        return {};
    }

    auto write_holding_registers_batch(std::uint16_t start_address, 
                                       std::span<const std::uint16_t> values)
        -> std::expected<void, exception_code>
    {
        std::lock_guard lock(mtx_);
        if (start_address + values.size() > holding_registers_.size()) {
            return std::unexpected(exception_code::illegal_data_address);
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            holding_registers_[start_address + i] = values[i];
        }
        return {};
    }

private:
    std::mutex mtx_;
    std::vector<bool> coils_;
    std::vector<bool> discrete_inputs_;
    std::vector<std::uint16_t> holding_registers_;
    std::vector<std::uint16_t> input_registers_;
};

} // namespace cnetmod::modbus
