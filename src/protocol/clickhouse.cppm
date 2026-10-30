module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.clickhouse;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.io.io_context;

export namespace cnetmod::clickhouse {
// Negotiated Native revision with settings, parameters and LowCardinality.
// The legacy 54401 profile remains available for older servers.
inline constexpr std::uint64_t protocol_revision = 54459;

struct value
{
    using sequence = std::vector<value>;
    // Binary strings also carry FixedString/UUID/IPv6 and wide integer bytes.
    using storage = std::variant<std::monostate, std::uint64_t, std::int64_t,
        double, std::string, sequence>;
    storage data;
    value() = default;

    template <typename T>
    requires std::constructible_from<storage, T>
    value(T&& input) : data(std::forward<T>(input))
    {}

    auto operator==(const value&) const -> bool = default;
};

struct column
{
    std::string name;
    std::string type;
    std::vector<value> values;
    auto operator==(const column&) const -> bool = default;
};

struct block
{
    std::vector<column> columns;
    [[nodiscard]] auto rows() const noexcept -> std::size_t;
    auto operator==(const block&) const -> bool = default;
};
enum class error_kind
{
    transport,
    protocol,
    server,
    invalid_argument,
    busy,
    cancelled
};

struct error
{
    error_kind kind{error_kind::protocol};
    std::error_code cause;
    std::int32_t server_code{};
    std::string name;
    std::string message;
    std::vector<error> nested;
};

struct limits
{
    std::size_t max_packet_bytes{64 * 1024 * 1024};
    std::size_t max_string_bytes{16 * 1024 * 1024};
    std::size_t max_rows{1'000'000};
    std::size_t max_columns{4096};
    std::size_t max_values{4'000'000};
    std::size_t max_depth{32};
};
enum class compression_mode
{
    none,
    checksum,
    lz4,
    zstd
};

struct connection_options
{
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::string database{"default"};
    std::string user{"default"};
    std::string password;
    bool tls{false}; // Direct Native TLS, normally on port 9440; verifies host/CA.
    std::string ca_file;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds operation_timeout{30000};
    std::uint64_t revision{protocol_revision};
    compression_mode compression{compression_mode::lz4};
    limits bounds;
};

struct server_info
{
    std::string name;
    std::uint64_t major{}, minor{}, revision{}, patch{};
    std::string timezone, display_name;
};

struct query_setting
{
    std::string name, value;
    bool important{false};
};

struct query_parameter
{
    std::string name, value;
};

struct external_table
{
    std::string name;
    block data;
};

struct query_options
{
    std::string query_id;
    std::vector<query_setting> settings;
    std::vector<query_parameter> parameters;
    std::vector<external_table> external_tables;
};

struct progress
{
    std::uint64_t rows{}, bytes{}, total_rows{}, written_rows{}, written_bytes{};
};

struct profile_info
{
    std::uint64_t rows{}, blocks{}, bytes{}, rows_before_limit{};
    bool applied_limit{}, calculated_rows_before_limit{};
};

struct query_result
{
    std::vector<block> blocks;
    std::optional<block> totals, extremes;
    progress statistics;
    profile_info profile;
    std::vector<block> logs, profile_events;
};

namespace wire {
    struct decoded_frame
    {
        std::vector<std::byte> data;
        std::size_t consumed{};
    };

    [[nodiscard]] auto compress_frame(std::span<const std::byte>, compression_mode, limits = {})
        -> std::expected<std::vector<std::byte>, error>;
    [[nodiscard]] auto decompress_frame(std::span<const std::byte>, limits = {})
        -> std::expected<decoded_frame, error>;

    // Stateless codecs use Native revision 54401. An incomplete packet returns
    // result_out_of_range without consuming input; malformed input fails closed.
    struct decoded_block
    {
        block data;
        std::size_t consumed{};
    };

    [[nodiscard]] auto encode_block(const block&, limits = {})
        -> std::expected<std::vector<std::byte>, error>;
    [[nodiscard]] auto decode_block(std::span<const std::byte>, limits = {})
        -> std::expected<decoded_block, error>;
} // namespace wire

// One executor owns a client. Overlapping operations are rejected. Cancellation,
// callback exceptions and malformed packets discard the transport before reuse.
class client
{
public:
    using block_sink = std::function<task<void>(const block&)>;
    explicit client(io_context&);
    ~client();
    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;
    auto connect(connection_options = {}, cancel_token* = nullptr)
        -> task<std::expected<server_info, error>>;
    // Send Native Cancel while a SELECT is reading results. Retain the client
    // until both this request and the original query have completed.
    auto cancel_current_query(cancel_token* = nullptr) -> task<std::expected<void, error>>;
    auto ping(cancel_token* = nullptr) -> task<std::expected<void, error>>;
    auto query(std::string sql, std::string query_id = {}, cancel_token* = nullptr)
        -> task<std::expected<query_result, error>>;
    // Awaited sink provides backpressure; streamed blocks are not retained.
    auto query(std::string sql, query_options, cancel_token* = nullptr)
        -> task<std::expected<query_result, error>>;
    auto query_blocks(std::string sql, block_sink, std::string query_id = {},
        cancel_token* = nullptr) -> task<std::expected<query_result, error>>;
    // table and column names are quoted as identifiers, never concatenated SQL.
    auto insert(std::string table, block, std::string query_id = {},
        cancel_token* = nullptr) -> task<std::expected<query_result, error>>;
    void close() noexcept; // Only when no operation is pending.
    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto info() const noexcept -> const server_info&;

private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
};

struct connection_pool_options
{
    connection_options connection;
    std::size_t minimum_connections{1}, maximum_connections{8};
};
struct connection_pool_state;

class pooled_connection
{
public:
    pooled_connection() noexcept;
    pooled_connection(pooled_connection&&) noexcept;
    auto operator=(pooled_connection&&) noexcept -> pooled_connection&;
    ~pooled_connection();
    pooled_connection(const pooled_connection&) = delete;
    auto operator=(const pooled_connection&) -> pooled_connection& = delete;
    [[nodiscard]] auto get() const -> client&;
    [[nodiscard]] auto operator->() const -> client*;

private:
    friend class connection_pool;
    pooled_connection(std::shared_ptr<connection_pool_state>, std::unique_ptr<client>);
    void release() noexcept;
    std::shared_ptr<connection_pool_state> state_;
    std::unique_ptr<client> client_;
};

// Executor-owned pool. Exhaustion returns busy immediately, applying bounded
// admission control instead of queuing unbounded pending operations.
class connection_pool
{
public:
    connection_pool(io_context&, connection_pool_options = {});
    ~connection_pool();
    connection_pool(const connection_pool&) = delete;
    auto operator=(const connection_pool&) -> connection_pool& = delete;
    auto acquire(cancel_token* = nullptr) -> task<std::expected<pooled_connection, error>>;
    auto warm_up(cancel_token* = nullptr) -> task<std::expected<void, error>>;
    auto health_check(cancel_token* = nullptr) -> task<std::expected<void, error>>;
    auto close() -> std::expected<void, std::error_code>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;
    [[nodiscard]] auto checked_out_count() const noexcept -> std::size_t;

private:
    std::shared_ptr<connection_pool_state> state_;
};

} // namespace cnetmod::clickhouse
