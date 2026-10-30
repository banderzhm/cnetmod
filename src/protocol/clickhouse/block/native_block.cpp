module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
#include "../column/native_column.hpp"
#include "../wire/native_buffer.hpp"
#include "native_block.hpp"

namespace cnetmod::clickhouse {
auto block::rows() const noexcept -> std::size_t
{
    return columns.empty() ? 0 : columns.front().values.size();
}

namespace detail {
    auto read_block(reader& input) -> block
    {
        unsigned seen{};
        for (;;)
        {
            const auto field = input.varint();
            if (!field)
                break;
            if (field > 2)
                fail("unknown Native BlockInfo field");
            const auto bit = 1U << field;
            if (seen & bit)
                fail("duplicate Native BlockInfo field");
            seen |= bit;
            if (field == 1)
            {
                if (input.fixed(1) > 1)
                    fail("invalid Native overflow flag");
            }
            else
                (void)input.fixed(4);
        }
        auto columns = input.varint(), rows = input.varint();
        if (columns > input.bounds.max_columns || rows > input.bounds.max_rows || (!columns && rows))
            fail("Native block dimensions exceed limit");
        block result;
        for (std::size_t i{}; i < columns; ++i)
        {
            column field;
            field.name = input.string();
            field.type = input.string();
            const auto type = parse_type(field.type, input.bounds);
            if (input.revision >= 54454 && input.fixed(1) != 0)
                fail("unsupported Native custom serialization");
            if (rows)
            {
                read_column_prefix(input, type);
                field.values = read_values(input, type, static_cast<std::size_t>(rows));
            }
            result.columns.push_back(std::move(field));
        }
        return result;
    }

    void write_block(writer& output, const block& data)
    {
        if (data.columns.size() > output.bounds.max_columns || data.rows() > output.bounds.max_rows)
            fail("Native block dimensions exceed limit", error_kind::invalid_argument);
        output.varint(1);
        output.fixed(0, 1);
        output.varint(2);
        output.fixed(0xffffffff, 4);
        output.varint(0);
        output.varint(data.columns.size());
        output.varint(data.rows());
        auto budget = output.bounds.max_values;
        for (const auto& field : data.columns)
        {
            if (field.values.size() != data.rows())
                fail("Native column row counts differ", error_kind::invalid_argument);
            output.string(field.name);
            output.string(field.type);
            const auto type = parse_type(field.type, output.bounds);
            if (output.revision >= 54454)
                output.fixed(0, 1);
            if (data.rows())
            {
                write_column_prefix(output, type);
                write_values(output, type, field.values, budget);
            }
        }
    }
} // namespace detail

namespace wire {
    auto encode_block(const block& data, limits bounds) -> std::expected<std::vector<std::byte>, error>
    {
        try
        {
            detail::writer output{{}, bounds};
            detail::write_block(output, data);
            return std::move(output.output);
        }
        catch (detail::failure& failure)
        {
            failure.value.kind = error_kind::invalid_argument;
            return std::unexpected(std::move(failure.value));
        }
    }

    auto decode_block(std::span<const std::byte> bytes, limits bounds) -> std::expected<decoded_block, error>
    {
        try
        {
            detail::reader input{bytes, bounds};
            auto result = detail::read_block(input);
            return decoded_block{std::move(result), input.position};
        }
        catch (detail::failure& failure)
        {
            return std::unexpected(std::move(failure.value));
        }
    }
} // namespace wire
} // namespace cnetmod::clickhouse
