export module cnetmod.protocol.mongodb:server_description;

import std;
import :error;
import :bson_document;

export namespace cnetmod::mongodb {

enum class server_kind
{
    unknown,
    standalone,
    mongos,
    replica_primary,
    replica_secondary,
    replica_arbiter,
    replica_other,
    load_balancer
};

struct server_address
{
    std::string host;
    std::uint16_t port = 27017;
    auto operator<=>(const server_address&) const = default;
};

struct server_description
{
    server_address address;
    server_kind kind = server_kind::unknown;
    std::string replica_set_name;
    std::optional<std::string> primary;
    std::vector<server_address> hosts;
    std::map<std::string, std::string> tags;
    std::optional<std::chrono::milliseconds> round_trip_time;
    std::optional<std::chrono::minutes> logical_session_timeout;
    std::int32_t minimum_wire_version = 0;
    std::int32_t maximum_wire_version = 0;
    std::optional<bson_object_id> election_id;
    std::optional<std::int64_t> set_version;
    std::optional<error> last_error;
    std::chrono::steady_clock::time_point last_update{};

    [[nodiscard]] auto readable() const noexcept -> bool;
    [[nodiscard]] auto writable() const noexcept -> bool;
};

auto parse_server_address(std::string_view address,
    std::uint16_t default_port = 27017) -> result<server_address>;
auto describe_server(const server_address& address, const bson_document& hello,
    std::chrono::milliseconds round_trip_time) -> result<server_description>;

} // namespace cnetmod::mongodb
