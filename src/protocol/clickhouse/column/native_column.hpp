#pragma once
#include "../wire/native_buffer.hpp"

// Private column grammar and column-major serialization contracts.
namespace cnetmod::clickhouse::detail {
struct type_descriptor
{
    enum class kind
    {
        unsigned_integer,
        signed_integer,
        floating,
        string,
        fixed,
        nullable,
        array,
        tuple,
        nothing,
        low_cardinality
    } tag;
    std::size_t width{};
    std::vector<type_descriptor> children;
};

auto parse_type(std::string_view, limits) -> type_descriptor;
void read_column_prefix(reader&, const type_descriptor&);
void write_column_prefix(writer&, const type_descriptor&);
auto read_low_cardinality(reader&, const type_descriptor&, std::size_t) -> std::vector<value>;
void write_low_cardinality(writer&, const type_descriptor&, std::span<const value>, std::size_t&);
auto default_value(const type_descriptor&) -> value;
auto read_values(reader&, const type_descriptor&, std::size_t) -> std::vector<value>;
void write_values(writer&, const type_descriptor&, std::span<const value>, std::size_t&);
} // namespace cnetmod::clickhouse::detail
