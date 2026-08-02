module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.kafka.protocol_constants;

import std;

export namespace cnetmod::kafka {

using bytes = std::vector<std::byte>;

enum class error_code : std::int16_t
{
    none = 0,
    unknown_server_error = -1,
    offset_out_of_range = 1,
    corrupt_message = 2,
    unknown_topic_or_partition = 3,
    leader_not_available = 5,
    not_leader_or_follower = 6,
    request_timed_out = 7,
    message_too_large = 10,
    record_list_too_large = 18,
    coordinator_load_in_progress = 14,
    coordinator_not_available = 15,
    not_coordinator = 16,
    illegal_generation = 22,
    unknown_member_id = 25,
    rebalance_in_progress = 27,
    topic_authorization_failed = 29,
    group_authorization_failed = 30,
    cluster_authorization_failed = 31,
    unsupported_sasl_mechanism = 33,
    illegal_sasl_state = 34,
    unsupported_version = 35,
    out_of_order_sequence_number = 45,
    duplicate_sequence_number = 46,
    invalid_producer_epoch = 47,
    invalid_transaction_state = 48,
    concurrent_transactions = 51,
    transaction_coordinator_fenced = 52,
    transactional_id_authorization_failed = 53,
    operation_not_attempted = 55,
    fetch_session_id_not_found = 70,
    invalid_fetch_session_epoch = 71,
    member_id_required = 79,
    fenced_instance_id = 82,
    invalid_record = 87,
    producer_fenced = 90,
    cancelled = 1000,
    transport = 1001,
    malformed_response = 1002,
    configuration = 1003
};

struct error
{
    error_code code = error_code::none;
    std::string message;
    bool retriable = false;
    std::optional<std::int32_t> broker_id;
};

template <typename T> using result = std::expected<T, error>;

struct topic_partition
{
    std::string topic;
    std::int32_t partition = 0;
    auto operator<=>(const topic_partition&) const = default;
};

struct header
{
    std::string key;
    bytes value;
};

struct record
{
    std::optional<bytes> key;
    std::optional<bytes> value;
    std::vector<header> headers;
    std::int64_t timestamp = -1;
    std::optional<topic_partition> destination;
};

struct consumed_record
{
    topic_partition source;
    std::int64_t offset = -1;
    std::int64_t timestamp = -1;
    std::optional<bytes> key;
    std::optional<bytes> value;
    std::vector<header> headers;
    std::optional<std::int32_t> leader_epoch;
};

enum class isolation_level : std::int8_t
{
    read_uncommitted,
    read_committed
};
enum class compression : std::int8_t
{
    none,
    gzip,
    snappy,
    lz4,
    zstd
};
enum class acknowledgement : std::int16_t
{
    none = 0,
    leader = 1,
    all = -1
};
enum class sasl_mechanism
{
    none,
    plain,
    scram_sha_256,
    scram_sha_512
};

class scram_crypto_provider
{
public:
    virtual ~scram_crypto_provider() = default;
    virtual auto nonce(std::size_t) -> result<std::string> = 0;
    virtual auto hmac(sasl_mechanism, std::span<const std::byte>,
        std::span<const std::byte>) -> result<bytes> = 0;
    virtual auto hash(sasl_mechanism, std::span<const std::byte>)
        -> result<bytes> = 0;
    virtual auto pbkdf2(sasl_mechanism, std::string_view,
        std::span<const std::byte>, std::uint32_t)
        -> result<bytes> = 0;
    virtual auto base64_encode(std::span<const std::byte>)
        -> result<std::string> = 0;
    virtual auto base64_decode(std::string_view) -> result<bytes> = 0;
};

struct broker_endpoint
{
    std::int32_t node_id = -1;
    std::string host;
    std::uint16_t port = 9092;
    std::optional<std::string> rack;
};

[[nodiscard]] auto is_retriable(error_code code) noexcept -> bool;
[[nodiscard]] auto make_error(error_code code, std::string message = {})
    -> error;

} // namespace cnetmod::kafka
