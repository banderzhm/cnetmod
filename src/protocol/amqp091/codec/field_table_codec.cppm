module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp091:field_table_codec;

import std;
import :protocol_constants;

export namespace cnetmod::amqp091 {

struct field_table;
struct field_array;

struct decimal_value
{
    std::uint8_t scale = 0;
    std::int32_t value = 0;
};

using field_value =
    std::variant<std::monostate, bool, std::int8_t, std::uint8_t, std::int16_t,
        std::uint16_t, std::int32_t, std::uint32_t, std::int64_t,
        std::uint64_t, float, double, decimal_value, std::string,
        std::vector<std::byte>, std::shared_ptr<field_array>,
        std::shared_ptr<field_table>>;

struct field_array
{
    std::vector<field_value> values;
};

struct field_table
{
    std::map<std::string, field_value, std::less<>> values;
};

[[nodiscard]] auto encode_field_table(const field_table& table)
    -> result<std::vector<std::byte>>;
[[nodiscard]] auto decode_field_table(std::span<const std::byte> bytes,
    std::size_t& consumed)
    -> result<field_table>;

} // namespace cnetmod::amqp091
