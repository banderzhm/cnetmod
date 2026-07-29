module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.broker_request_codec;
import std;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.request_header;
import cnetmod.protocol.kafka.protocol_value_codec;

export namespace cnetmod::kafka::protocol {
struct api_versions_response
{
    error_code error = error_code::none;
    std::vector<api_version> versions;
    std::int32_t throttle_ms = 0;
};

struct partition_metadata
{
    error_code error = error_code::none;
    std::int32_t partition = 0;
    std::int32_t leader = -1;
    std::int32_t leader_epoch = -1;
    std::vector<std::int32_t> replicas;
    std::vector<std::int32_t> isr;
};

struct topic_metadata
{
    error_code error = error_code::none;
    std::string name;
    bool internal = false;
    std::vector<partition_metadata> partitions;
};

struct metadata_response
{
    std::int32_t throttle_ms = 0;
    std::vector<broker_endpoint> brokers;
    std::optional<std::string> cluster_id;
    std::int32_t controller_id = -1;
    std::vector<topic_metadata> topics;
};

struct produce_partition
{
    topic_partition target;
    bytes records;
};

struct produce_request
{
    std::optional<std::string> transactional_id;
    acknowledgement acks = acknowledgement::all;
    std::chrono::milliseconds timeout{30000};
    std::vector<produce_partition> partitions;
};

struct produce_result
{
    topic_partition target;
    error_code error = error_code::none;
    std::int64_t base_offset = -1;
    std::int64_t log_append_time = -1;
};

struct coordinator_response
{
    std::int32_t throttle_ms = 0;
    error_code error = error_code::none;
    std::optional<std::string> error_message;
    broker_endpoint coordinator;
};

struct join_group_protocol
{
    std::string name;
    bytes metadata;
};

struct join_group_request
{
    std::string group_id;
    std::chrono::milliseconds session_timeout{45000};
    std::chrono::milliseconds rebalance_timeout{300000};
    std::string member_id;
    std::optional<std::string> group_instance_id;
    std::vector<join_group_protocol> protocols;
};

struct join_group_member
{
    std::string member_id;
    std::optional<std::string> group_instance_id;
    bytes metadata;
};

struct join_group_response
{
    std::int32_t throttle_ms = 0;
    error_code error = error_code::none;
    std::int32_t generation = -1;
    std::string protocol_name;
    std::string leader_id;
    std::string member_id;
    std::vector<join_group_member> members;
};

struct group_identity
{
    std::string group_id;
    std::int32_t generation = -1;
    std::string member_id;
    std::optional<std::string> group_instance_id;
};

struct sync_group_assignment
{
    std::string member_id;
    bytes assignment;
};

struct sync_group_request
{
    group_identity identity;
    std::vector<sync_group_assignment> assignments;
};

struct sync_group_response
{
    std::int32_t throttle_ms = 0;
    error_code error = error_code::none;
    bytes assignment;
};

struct group_operation_response
{
    std::int32_t throttle_ms = 0;
    error_code error = error_code::none;
};

struct committed_offset
{
    topic_partition source;
    std::int64_t offset = -1;
    std::int32_t leader_epoch = -1;
    std::string metadata;
    error_code error = error_code::none;
};

struct offset_fetch_response
{
    std::int32_t throttle_ms = 0;
    error_code error = error_code::none;
    std::vector<committed_offset> offsets;
};

struct list_offset_partition
{
    topic_partition source;
    std::int64_t timestamp = -2;
    std::int32_t current_leader_epoch = -1;
};

struct listed_offset
{
    topic_partition source;
    error_code error = error_code::none;
    std::int64_t timestamp = -1;
    std::int64_t offset = -1;
    std::int32_t leader_epoch = -1;
};

struct fetch_partition
{
    topic_partition source;
    std::int64_t offset = 0;
    std::int32_t max_bytes = 1024 * 1024;
    std::int32_t current_leader_epoch = -1;
};

struct fetch_request
{
    std::chrono::milliseconds max_wait{500};
    std::int32_t min_bytes = 1;
    std::int32_t max_bytes = 50 * 1024 * 1024;
    isolation_level isolation = isolation_level::read_uncommitted;
    std::int32_t session_id = 0;
    std::int32_t session_epoch = 0;
    std::vector<fetch_partition> partitions;
    std::vector<topic_partition> forgotten_partitions;
};

struct fetched_partition
{
    struct aborted_transaction
    {
        std::int64_t producer_id = -1;
        std::int64_t first_offset = -1;
    };

    topic_partition source;
    error_code error = error_code::none;
    std::int64_t high_watermark = 0;
    std::int64_t last_stable_offset = -1;
    std::int32_t preferred_replica = -1;
    std::vector<aborted_transaction> aborted_transactions;
    bytes records;
};

struct fetch_response
{
    std::int32_t throttle_ms = 0;
    error_code error = error_code::none;
    std::int32_t session_id = 0;
    std::vector<fetched_partition> partitions;
};

[[nodiscard]] auto encode_api_versions() -> bytes;
[[nodiscard]] auto decode_api_versions(std::span<const std::byte>, std::int16_t)
    -> result<api_versions_response>;
[[nodiscard]] auto encode_metadata(std::span<const std::string>) -> bytes;
[[nodiscard]] auto decode_metadata(std::span<const std::byte>, std::int16_t)
    -> result<metadata_response>;
[[nodiscard]] auto encode_produce(const produce_request&, std::int16_t)
    -> bytes;
[[nodiscard]] auto decode_produce(std::span<const std::byte>, std::int16_t,
    const produce_request&)
    -> result<std::vector<produce_result>>;
[[nodiscard]] auto encode_fetch(const fetch_request&, std::int16_t) -> bytes;
[[nodiscard]] auto decode_fetch(std::span<const std::byte>, std::int16_t)
    -> result<fetch_response>;
[[nodiscard]] auto encode_find_coordinator(std::string_view, std::int16_t,
    bool group = true) -> bytes;
[[nodiscard]] auto decode_find_coordinator(std::span<const std::byte>,
    std::int16_t)
    -> result<coordinator_response>;
[[nodiscard]] auto encode_join_group(const join_group_request&, std::int16_t)
    -> bytes;
[[nodiscard]] auto decode_join_group(std::span<const std::byte>, std::int16_t)
    -> result<join_group_response>;
[[nodiscard]] auto encode_sync_group(const sync_group_request&, std::int16_t)
    -> bytes;
[[nodiscard]] auto decode_sync_group(std::span<const std::byte>, std::int16_t)
    -> result<sync_group_response>;
[[nodiscard]] auto encode_heartbeat(const group_identity&, std::int16_t)
    -> bytes;
[[nodiscard]] auto decode_heartbeat(std::span<const std::byte>, std::int16_t)
    -> result<group_operation_response>;
[[nodiscard]] auto encode_leave_group(const group_identity&, std::int16_t)
    -> bytes;
[[nodiscard]] auto decode_leave_group(std::span<const std::byte>, std::int16_t)
    -> result<group_operation_response>;
[[nodiscard]] auto
encode_offset_commit(const group_identity&,
    const std::map<topic_partition, std::int64_t>&,
    std::int16_t) -> bytes;
[[nodiscard]] auto encode_offset_fetch(std::string_view,
    std::span<const topic_partition>)
    -> bytes;
[[nodiscard]] auto decode_offset_fetch(std::span<const std::byte>, std::int16_t)
    -> result<offset_fetch_response>;
[[nodiscard]] auto encode_list_offsets(std::span<const list_offset_partition>,
    isolation_level, std::int16_t) -> bytes;
[[nodiscard]] auto decode_list_offsets(std::span<const std::byte>, std::int16_t)
    -> result<std::vector<listed_offset>>;
[[nodiscard]] auto encode_end_transaction(std::string_view, std::int64_t,
    std::int16_t, bool) -> bytes;
[[nodiscard]] auto encode_transaction_offset_commit(
    std::string_view, std::string_view, std::int64_t, std::int16_t,
    std::int32_t, std::string_view,
    const std::map<topic_partition, std::int64_t>&) -> bytes;
} // namespace cnetmod::kafka::protocol
