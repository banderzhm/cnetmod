export module cnetmod.protocol.mongodb:topology_connection_pool;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :error;
import :bson_document;
import :connection_pool;
import :server_description;
import :topology_monitor;
import :srv_seedlist;
import :server_address;

export namespace cnetmod::mongodb {

struct topology_connection_pool_options
{
    std::vector<server_address> seeds{{"127.0.0.1", 27017}};
    connection_pool_options per_server_pool;
    std::optional<std::string> replica_set_name;
    std::function<task<result<srv_seedlist>>(std::vector<server_address>)> refresh_srv;
    std::chrono::seconds srv_refresh_interval{60};
};

struct topology_connection_pool_statistics
{
    std::size_t server_pool_count{};
    std::size_t connection_count{};
    std::size_t idle_connection_count{};
    std::size_t checked_out_connection_count{};
    std::size_t waiting_request_count{};
    std::size_t connecting_count{};
};

class topology_connection_pool
{
public:
    topology_connection_pool(io_context& context,
        topology_connection_pool_options options);
    topology_connection_pool(const topology_connection_pool&) = delete;
    auto operator=(const topology_connection_pool&)
        -> topology_connection_pool& = delete;

    auto refresh(cancel_token* cancellation = nullptr) -> task<result<void>>;
    auto warm_up(cancel_token& cancellation, server_selection_options selection = {}) -> task<result<void>>;
    auto health_check(cancel_token& cancellation, server_selection_options selection = {}) -> task<result<void>>;
    auto async_close() -> task<void>;
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
    std::chrono::steady_clock::time_point next_srv_refresh_{};
    auto pool_for(const server_address& address) -> std::shared_ptr<connection_pool>;
    void mark_unknown_if_current(const server_address& address,
        const std::shared_ptr<connection_pool>& pool, const error& failure);
    async_mutex refresh_mutex_;
    std::atomic<bool> closed_{false};
    std::vector<std::shared_ptr<connection_pool>> retired_pools_;
    io_context& context_;
    topology_connection_pool_options options_;
    topology_monitor topology_;
    // Pool creation, statistics and close share ownership of the per-server
    // map. A pending acquire/command retains its own shared pool reference,
    // so SRV retirement can close/remove a node without destroying its facade.
    concurrent_containers::atomic_rw_latch pools_latch_;
    std::map<server_address, std::shared_ptr<connection_pool>> pools_;
};

} // namespace cnetmod::mongodb
