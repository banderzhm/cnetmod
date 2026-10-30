export module cnetmod.protocol.mongodb:connection_string;
import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :error;
import :srv_seedlist;
import :topology_connection_pool;
import :topology_monitor;

export namespace cnetmod::mongodb {
struct connection_configuration
{
    topology_connection_pool_options topology;
    server_selection_options selection;
    std::optional<std::string> srv_hostname;
    std::string srv_service_name = "mongodb";
    std::size_t srv_max_hosts = 0;
    std::vector<std::pair<std::string, std::string>> uri_options;
};

auto parse_connection_string(std::string_view uri) -> result<connection_configuration>;
/// SRV/TXT complete and validate before callers can create any connections.
auto resolve_connection_string(io_context& io, thread_pool& pool, std::string uri,
    srv_lookup_callback lookup = {}) -> task<result<connection_configuration>>;
} // namespace cnetmod::mongodb

namespace cnetmod::mongodb::uri_detail {
auto decode(std::string_view text) -> result<std::string>;
auto query(std::string_view text) -> result<std::vector<std::pair<std::string, std::string>>>;
auto apply(connection_configuration& configuration, std::span<const std::pair<std::string, std::string>> options) -> result<void>;
} // namespace cnetmod::mongodb::uri_detail
