#pragma once
#include "native_buffer.hpp"

// Server packet envelopes at the negotiated Native revision.
namespace cnetmod::clickhouse::detail {
enum class server_packet : std::uint64_t
{
    hello,
    data,
    exception,
    progress,
    pong,
    end,
    profile,
    totals,
    extremes,
    table_status,
    log,
    table_columns,
    part_uuids,
    read_task,
    profile_events
};

struct packet
{
    server_packet kind;
    block data;
    server_info hello;
    progress statistics;
    profile_info profile;
    error failure;
    std::size_t wire_bytes{}, value_count{}, decoded_bytes{};
};

auto parse_packet(reader&, bool compressed) -> packet;
auto transport_error(std::error_code) -> error;
} // namespace cnetmod::clickhouse::detail
