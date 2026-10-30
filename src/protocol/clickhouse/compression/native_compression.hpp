#pragma once
#include "../wire/native_buffer.hpp"

namespace cnetmod::clickhouse::detail {
auto encode_frame(std::span<const std::byte>, compression_mode, limits) -> std::vector<std::byte>;
auto decode_frame(reader&) -> std::vector<std::byte>;
void write_data_packet(writer&, const block&, compression_mode, std::string_view table = {});
auto read_data_block(reader&, bool compressed) -> block;
} // namespace cnetmod::clickhouse::detail
