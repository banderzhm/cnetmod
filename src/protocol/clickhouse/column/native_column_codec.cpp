module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
#include "../wire/native_buffer.hpp"
#include "native_column.hpp"

namespace cnetmod::clickhouse::detail {
auto read_values(reader& input, const type_descriptor& type, std::size_t count) -> std::vector<value>
{
    input.account(count);
    using kind = type_descriptor::kind;
    std::vector<value> values(count);
    if (type.tag == kind::low_cardinality)
        return read_low_cardinality(input, type, count);
    if (type.tag == kind::nullable)
    {
        auto flags = input.take(count);
        auto nested = read_values(input, type.children[0], count);
        for (std::size_t i{}; i < count; ++i)
        {
            if (flags[i] != std::byte{0} && flags[i] != std::byte{1})
                fail("invalid Nullable bitmap");
            if (flags[i] == std::byte{0})
                values[i] = std::move(nested[i]);
        }
    }
    else if (type.tag == kind::array)
    {
        std::vector<std::size_t> offsets;
        offsets.reserve(count);
        std::uint64_t previous{};
        for (std::size_t i{}; i < count; ++i)
        {
            const auto next = input.fixed(8);
            if (next < previous || next > input.bounds.max_values)
                fail("invalid Native array offset");
            offsets.push_back(static_cast<std::size_t>(next));
            previous = next;
        }
        auto nested = read_values(input, type.children[0], static_cast<std::size_t>(previous));
        std::size_t start{};
        for (std::size_t i{}; i < count; ++i)
        {
            value::sequence row;
            for (; start < offsets[i]; ++start)
                row.push_back(std::move(nested[start]));
            values[i].data = std::move(row);
        }
    }
    else if (type.tag == kind::tuple)
    {
        for (auto& item : values)
            item.data = value::sequence(type.children.size());
        for (std::size_t field{}; field < type.children.size(); ++field)
        {
            auto nested = read_values(input, type.children[field], count);
            for (std::size_t i{}; i < count; ++i)
                std::get<value::sequence>(values[i].data)[field] = std::move(nested[i]);
        }
    }
    else
        for (auto& item : values)
        {
            switch (type.tag)
            {
            case kind::nothing:
                break;
            case kind::string:
                item.data = input.string();
                break;
            case kind::fixed:
            {
                auto bytes = input.take(type.width);
                item.data = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                break;
            }
            case kind::floating:
                item.data = type.width == 4 ? static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(input.fixed(4)))) : std::bit_cast<double>(input.fixed(8));
                break;
            case kind::signed_integer:
            {
                auto bits = input.fixed(type.width);
                if (type.width < 8 && (bits & (std::uint64_t{1} << (type.width * 8 - 1))))
                    bits |= ~std::uint64_t{} << (type.width * 8);
                item.data = std::bit_cast<std::int64_t>(bits);
                break;
            }
            case kind::unsigned_integer:
                item.data = input.fixed(type.width);
                break;
            default:
                fail("invalid Native type descriptor");
            }
        }
    return values;
}

auto default_value(const type_descriptor& type) -> value
{
    using kind = type_descriptor::kind;
    switch (type.tag)
    {
    case kind::unsigned_integer:
        return std::uint64_t{};
    case kind::signed_integer:
        return std::int64_t{};
    case kind::floating:
        return 0.0;
    case kind::string:
        return std::string{};
    case kind::fixed:
        return std::string(type.width, '\0');
    case kind::array:
        return value::sequence{};
    case kind::tuple:
    {
        value::sequence fields;
        for (const auto& child : type.children)
            fields.push_back(default_value(child));
        return fields;
    }
    default:
        return {};
    }
}

void write_values(writer& output, const type_descriptor& type, std::span<const value> values, std::size_t& budget)
{
    if (values.size() > budget)
        fail("Native values exceed limit", error_kind::invalid_argument);
    budget -= values.size();
    using kind = type_descriptor::kind;
    if (type.tag == kind::low_cardinality)
    {
        write_low_cardinality(output, type, values, budget);
        return;
    }
    if (type.tag == kind::nullable)
    {
        std::vector<value> nested;
        nested.reserve(values.size());
        for (const auto& item : values)
        {
            const bool null = std::holds_alternative<std::monostate>(item.data);
            output.fixed(null, 1);
            nested.push_back(null ? default_value(type.children[0]) : item);
        }
        write_values(output, type.children[0], nested, budget);
        return;
    }
    if (type.tag == kind::array)
    {
        std::vector<value> nested;
        for (const auto& item : values)
        {
            const auto* array = std::get_if<value::sequence>(&item.data);
            if (!array || array->size() > budget || nested.size() > budget - array->size())
                fail("invalid Native array", error_kind::invalid_argument);
            nested.insert(nested.end(), array->begin(), array->end());
            output.fixed(nested.size(), 8);
        }
        write_values(output, type.children[0], nested, budget);
        return;
    }
    if (type.tag == kind::tuple)
    {
        for (std::size_t field{}; field < type.children.size(); ++field)
        {
            std::vector<value> nested;
            for (const auto& item : values)
            {
                const auto* tuple = std::get_if<value::sequence>(&item.data);
                if (!tuple || tuple->size() != type.children.size())
                    fail("invalid Native tuple", error_kind::invalid_argument);
                nested.push_back((*tuple)[field]);
            }
            write_values(output, type.children[field], nested, budget);
        }
        return;
    }
    for (const auto& item : values)
    {
        if (type.tag == kind::string || type.tag == kind::fixed)
        {
            const auto* text = std::get_if<std::string>(&item.data);
            if (!text || (type.tag == kind::fixed && text->size() != type.width))
                fail("invalid Native binary/string value", error_kind::invalid_argument);
            if (type.tag == kind::string)
                output.string(*text);
            else
                output.bytes(*text);
        }
        else if (type.tag == kind::unsigned_integer)
        {
            const auto* number = std::get_if<std::uint64_t>(&item.data);
            if (!number || (type.width < 8 && *number >= (std::uint64_t{1} << (type.width * 8))))
                fail("Native unsigned integer out of range", error_kind::invalid_argument);
            output.fixed(*number, type.width);
        }
        else if (type.tag == kind::signed_integer)
        {
            const auto* number = std::get_if<std::int64_t>(&item.data);
            const auto bits = type.width * 8;
            if (!number || (bits < 64 && (*number < -(std::int64_t{1} << (bits - 1)) || *number >= (std::int64_t{1} << (bits - 1)))))
                fail("Native signed integer out of range", error_kind::invalid_argument);
            output.fixed(std::bit_cast<std::uint64_t>(*number), type.width);
        }
        else if (type.tag == kind::floating)
        {
            const auto* number = std::get_if<double>(&item.data);
            if (!number || (type.width == 4 && std::isfinite(*number) && std::abs(*number) > std::numeric_limits<float>::max()))
                fail("invalid Native float", error_kind::invalid_argument);
            if (type.width == 4)
                output.fixed(std::bit_cast<std::uint32_t>(static_cast<float>(*number)), 4);
            else
                output.fixed(std::bit_cast<std::uint64_t>(*number), 8);
        }
        else if (!std::holds_alternative<std::monostate>(item.data))
            fail("Nothing requires null", error_kind::invalid_argument);
    }
}

} // namespace cnetmod::clickhouse::detail
