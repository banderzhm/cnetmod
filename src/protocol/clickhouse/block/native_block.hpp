#pragma once
#include "../wire/native_buffer.hpp"

// Private Native block framing; shared by codec and transport packets.
namespace cnetmod::clickhouse::detail {
auto read_block(reader&) -> block;
void write_block(writer&, const block&);
} // namespace cnetmod::clickhouse::detail
