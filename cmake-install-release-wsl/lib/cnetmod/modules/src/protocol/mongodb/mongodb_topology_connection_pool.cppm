export module cnetmod.protocol.mongodb:topology_connection_pool;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :connection_pool;
import :server_description;
import :topology_monitor;

export namespace cnetmod::mongodb {

struct topology_connection_pool_options
{
    std::vector<server_address> seeds{{"127.0.0.1", 27017}};
    connection_pool_options per_server_pool;
    std::optional<std::string> replica_set_name;
};

struct topology_connection_pool_statistics
{
    std::size_t server_pool_count{};
    std::size_t connection_count{};
    std::size_t idle_connection_count{};
    std::size_t checked_out_connection_count{};
    std::size_t waiting_request_count{};
};

class topology_connection_pool
{
public:
    topology_connection_pool(io_context& context,
        topology_connection_pool_options options);
    topology_connection_pool(const topology_connection_pool&) = delete;
    auto operator=(const topology_connection_pool&)
        -> topology_connection_pool& = delete;

    auto refresh() -> task<result<void>>;
    auto run_monitoring(std::stop_token stop,
        std::chrono::milliseconds heartbeat_frequency = std::chrono::seconds{10})
        -> task<void>;
    auto acquire(server_selection_options selection = {})
        -> task<result<pooled_connection>>;
    auto command(std::string_view database, bson_document command_document,
        server_selection_options selection = {})
        -> task<result<bson_document>>;
    [[nodiscard]] auto topology() noexcept -> topology_monitor&;
    [[nodiscard]] auto statistics() -> topology_connection_pool_statistics;
    void close() noexcept;

private:
    auto pool_for(const server_address& address) -> connection_pool&;
    io_context& context_;
    topology_connection_pool_options options_;
    topology_monitor topology_;
    std::mutex mutex_;
    std::map<server_address, std::unique_ptr<connection_pool>> pools_;
};

} // namespace cnetmod::mongodb
