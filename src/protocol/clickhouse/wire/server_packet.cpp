module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
#include "../block/native_block.hpp"
#include "../compression/native_compression.hpp"
#include "native_buffer.hpp"
#include "server_packet.hpp"

namespace cnetmod::clickhouse::detail {
auto read_exception(detail::reader& input, std::size_t depth = 0) -> error
{
    if (depth > input.bounds.max_depth)
        detail::fail("Native exception nesting exceeds limit");
    error result{.kind = error_kind::server};
    result.server_code = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(input.fixed(4)));
    result.name = input.string();
    result.message = input.string();
    (void)input.string(); // Stack traces are not logged or retained.
    const auto nested = input.fixed(1);
    if (nested > 1)
        detail::fail("invalid Native exception flag");
    if (nested)
        result.nested.push_back(read_exception(input, depth + 1));
    return result;
}

auto parse_packet(detail::reader& input, bool compressed) -> packet
{
    packet result{};
    result.kind = static_cast<server_packet>(input.varint());
    switch (result.kind)
    {
    case server_packet::hello:
        result.hello.name = input.string();
        result.hello.major = input.varint();
        result.hello.minor = input.varint();
        result.hello.revision = input.varint();
        if (result.hello.revision < input.revision)
            detail::fail("ClickHouse server Native revision is too old");
        result.hello.timezone = input.string();
        result.hello.display_name = input.string();
        result.hello.patch = input.varint();
        break;
    case server_packet::data:
    case server_packet::totals:
    case server_packet::extremes:
        (void)input.string(); // Temporary/external table name.
        result.data = detail::read_data_block(input, compressed);
        break;
    case server_packet::log:
    case server_packet::profile_events:
        (void)input.string();
        result.data = read_data_block(input, false);
        break;
    case server_packet::table_columns:
        (void)input.string();
        (void)input.string();
        break;
    case server_packet::part_uuids:
    {
        auto count = input.varint();
        if (count > input.bounds.max_values)
            fail("Native UUID count exceeds limit");
        (void)input.take(static_cast<std::size_t>(count) * 16);
        break;
    }
    case server_packet::exception:
        result.failure = read_exception(input);
        break;
    case server_packet::progress:
        result.statistics.rows = input.varint();
        result.statistics.bytes = input.varint();
        result.statistics.total_rows = input.varint();
        if (input.revision >= 54420)
        {
            result.statistics.written_rows = input.varint();
            result.statistics.written_bytes = input.varint();
        }
        break;
    case server_packet::profile:
        result.profile.rows = input.varint();
        result.profile.blocks = input.varint();
        result.profile.bytes = input.varint();
        result.profile.applied_limit = input.fixed(1) != 0;
        result.profile.rows_before_limit = input.varint();
        result.profile.calculated_rows_before_limit = input.fixed(1) != 0;
        break;
    case server_packet::pong:
    case server_packet::end:
        break;
    default:
        detail::fail("unexpected Native server packet");
    }
    result.wire_bytes = input.position;
    result.decoded_bytes = std::max(input.position, input.decoded_bytes);
    result.value_count = input.values;
    return result;
}
} // namespace cnetmod::clickhouse::detail
