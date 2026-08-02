module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.request_header;
import std;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka::protocol {
enum class api_key : std::int16_t
{
    produce = 0,
    fetch = 1,
    list_offsets = 2,
    metadata = 3,
    offset_commit = 8,
    offset_fetch = 9,
    find_coordinator = 10,
    join_group = 11,
    heartbeat = 12,
    leave_group = 13,
    sync_group = 14,
    sasl_handshake = 17,
    api_versions = 18,
    init_producer_id = 22,
    add_partitions_to_txn = 24,
    add_offsets_to_txn = 25,
    end_txn = 26,
    txn_offset_commit = 28,
    sasl_authenticate = 36
};

struct api_version
{
    api_key key{};
    std::int16_t minimum = 0;
    std::int16_t maximum = 0;
};

struct request_header
{
    api_key key{};
    std::int16_t version = 0;
    std::int32_t correlation_id = 0;
    std::string client_id;
};
} // namespace cnetmod::kafka::protocol
