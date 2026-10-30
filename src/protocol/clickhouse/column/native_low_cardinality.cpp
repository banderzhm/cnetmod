module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
#include "native_column.hpp"

namespace cnetmod::clickhouse::detail {
void read_column_prefix(reader& input, const type_descriptor& type)
{
    if (type.tag == type_descriptor::kind::low_cardinality)
    {
        if (input.fixed(8) != 1)
            fail("invalid Native LowCardinality version");
        return;
    }
    for (const auto& child : type.children)
        read_column_prefix(input, child);
}

void write_column_prefix(writer& output, const type_descriptor& type)
{
    if (type.tag == type_descriptor::kind::low_cardinality)
    {
        output.fixed(1, 8);
        return;
    }
    for (const auto& child : type.children)
        write_column_prefix(output, child);
}

auto read_low_cardinality(reader& input, const type_descriptor& type, std::size_t count) -> std::vector<value>
{
    const auto flags = input.fixed(8);
    const auto index_type = flags & 255;
    if (index_type > 3 || (flags & 256) || !(flags & 512) || (flags & ~std::uint64_t{0x703}))
        fail("invalid Native LowCardinality dictionary flags");
    const auto keys = input.fixed(8);
    if (keys > input.bounds.max_values)
        fail("Native LowCardinality dictionary exceeds limit");
    const auto& child = type.children[0];
    const bool nullable = child.tag == type_descriptor::kind::nullable;
    auto dictionary = read_values(input, nullable ? child.children[0] : child, static_cast<std::size_t>(keys));
    if (input.fixed(8) != count)
        fail("Native LowCardinality index count mismatch");
    std::vector<value> result;
    result.reserve(count);
    for (std::size_t i{}; i < count; ++i)
    {
        const auto index = input.fixed(std::size_t{1} << index_type);
        if (index >= dictionary.size())
            fail("Native LowCardinality index exceeds dictionary");
        result.push_back(nullable && index == 0 ? value{} : dictionary[static_cast<std::size_t>(index)]);
    }
    return result;
}

void write_low_cardinality(writer& output, const type_descriptor& type, std::span<const value> values, std::size_t& budget)
{
    const auto& child = type.children[0];
    const bool nullable = child.tag == type_descriptor::kind::nullable;
    std::vector<value> dictionary;
    if (nullable)
        dictionary.push_back(default_value(child.children[0]));
    std::vector<std::uint64_t> indexes;
    indexes.reserve(values.size());
    std::unordered_map<std::string, std::uint64_t> dictionary_indexes;
    // Hash exact scalar storage instead of rescanning an ever-growing
    // dictionary: high-cardinality inserts must not become quadratic.
    const auto key_for = [](const value& item)
    {
        std::string key(1, static_cast<char>(item.data.index()));
        std::visit([&](const auto& scalar)
            {
                using scalar_type = std::decay_t<decltype(scalar)>;
                if constexpr (std::same_as<scalar_type, std::string>)
                    key += scalar;
                else if constexpr (std::is_arithmetic_v<scalar_type>)
                    key.append(reinterpret_cast<const char*>(&scalar), sizeof(scalar));
                else
                    fail("LowCardinality dictionary requires scalar values", error_kind::invalid_argument);
            },
            item.data);
        return key;
    };
    for (const auto& item : values)
    {
        if (nullable && std::holds_alternative<std::monostate>(item.data))
        {
            indexes.push_back(0);
            continue;
        }
        auto [entry, inserted] = dictionary_indexes.try_emplace(key_for(item), dictionary.size());
        indexes.push_back(entry->second);
        if (inserted)
            dictionary.push_back(item);
    }
    const unsigned index_type = dictionary.size() <= 256 ? 0 : dictionary.size() <= 65536 ? 1
        : dictionary.size() <= 0x100000000ULL                                             ? 2
                                                                                          : 3;
    output.fixed(512 | index_type, 8);
    output.fixed(dictionary.size(), 8);
    write_values(output, nullable ? child.children[0] : child, dictionary, budget);
    output.fixed(values.size(), 8);
    for (const auto index : indexes)
        output.fixed(index, std::size_t{1} << index_type);
}
} // namespace cnetmod::clickhouse::detail
