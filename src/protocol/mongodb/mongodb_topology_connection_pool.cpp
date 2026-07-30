module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.timer;
import :error;
import :bson_document;
import :connection;
import :connection_pool;
import :server_description;
import :topology_monitor;
import :topology_connection_pool;

namespace cnetmod::mongodb {
topology_connection_pool::topology_connection_pool(io_context& context,
    topology_connection_pool_options options)
    : context_(context), options_(std::move(options)), topology_(options_.replica_set_name)
{
    if (options_.seeds.empty())
        options_.seeds.push_back({"127.0.0.1", 27017});
}

auto topology_connection_pool::pool_for(const server_address& address)
    -> connection_pool&
{
    std::scoped_lock lock(mutex_);
    auto found = pools_.find(address);
    if (found != pools_.end())
        return *found->second;
    auto pool_options = options_.per_server_pool;
    pool_options.connection.host = address.host;
    pool_options.connection.port = address.port;
    auto inserted = pools_.emplace(address,
        std::make_unique<connection_pool>(context_, std::move(pool_options)));
    return *inserted.first->second;
}

auto topology_connection_pool::refresh() -> task<result<void>>
{
    std::vector<server_address> addresses = options_.seeds;
    for (const auto& description : topology_.snapshot())
    {
        addresses.push_back(description.address);
        addresses.insert(addresses.end(), description.hosts.begin(), description.hosts.end());
    }
    std::ranges::sort(addresses);
    auto unique = std::ranges::unique(addresses);
    addresses.erase(unique.begin(), unique.end());
    std::optional<error> last_failure;
    std::size_t successes{};
    for (const auto& address : addresses)
    {
        auto connection_options = options_.per_server_pool.connection;
        connection_options.host = address.host;
        connection_options.port = address.port;
        try
        {
            auto checked = co_await topology_.check_server(
                context_, std::move(connection_options));
            if (checked)
                ++successes;
            else
                last_failure = checked.error();
        }
        catch (const std::exception& exception)
        {
            last_failure = make_error(error_code::connection_failed,
                std::format("MongoDB topology check failed for {}:{}: {}",
                    address.host, address.port, exception.what()));
        }
    }
    if (successes == 0)
        co_return std::unexpected(last_failure.value_or(make_error(
            error_code::server_selection_failed, "MongoDB topology has no reachable servers")));
    co_return result<void>{};
}

auto topology_connection_pool::acquire(server_selection_options selection)
    -> task<result<pooled_connection>>
{
    std::optional<server_description> selected;
    auto initial_selection = topology_.select_server(selection);
    if (initial_selection)
        selected.emplace(std::move(*initial_selection));
    else
    {
        auto refreshed = co_await refresh();
        if (!refreshed)
            co_return std::unexpected(refreshed.error());
        auto reselection = topology_.select_server(selection);
        if (!reselection)
            co_return std::unexpected(reselection.error());
        selected.emplace(std::move(*reselection));
    }
    auto acquired = co_await pool_for(selected->address).acquire();
    if (!acquired)
    {
        topology_.mark_unknown(selected->address, acquired.error());
        auto refreshed = co_await refresh();
        if (!refreshed)
            co_return std::unexpected(refreshed.error());
        auto reselection = topology_.select_server(selection);
        if (!reselection)
            co_return std::unexpected(reselection.error());
        selected.emplace(std::move(*reselection));
        co_return co_await pool_for(selected->address).acquire();
    }
    co_return acquired;
}

auto topology_connection_pool::command(std::string_view database,
    bson_document document, server_selection_options selection)
    -> task<result<bson_document>>
{
    std::optional<server_description> selected;
    auto initial_selection = topology_.select_server(selection);
    if (initial_selection)
        selected.emplace(std::move(*initial_selection));
    else
    {
        auto refreshed = co_await refresh();
        if (!refreshed)
            co_return std::unexpected(refreshed.error());
        auto reselection = topology_.select_server(selection);
        if (!reselection)
            co_return std::unexpected(reselection.error());
        selected.emplace(std::move(*reselection));
    }
    auto acquired = co_await pool_for(selected->address).acquire();
    if (!acquired)
    {
        topology_.mark_unknown(selected->address, acquired.error());
        co_return std::unexpected(acquired.error());
    }
    auto response = co_await (*acquired)->command(database, std::move(document));
    if (!response && (!(*acquired)->is_open() || response.error().labels.contains("RetryableWriteError") || response.error().labels.contains("NotPrimaryError") || response.error().server_code == 10107 || response.error().server_code == 13435 || response.error().server_code == 11600 || response.error().server_code == 11602))
    {
        topology_.mark_unknown(selected->address, response.error());
        acquired->discard();
    }
    co_return response;
}

auto topology_connection_pool::run_monitoring(std::stop_token stop,
    std::chrono::milliseconds heartbeat) -> task<void>
{
    heartbeat = std::max(heartbeat, std::chrono::milliseconds{500});
    while (!stop.stop_requested())
    {
        auto ignored = co_await refresh();
        (void)ignored;
        co_await async_sleep(context_, heartbeat);
    }
}

auto topology_connection_pool::topology() noexcept -> topology_monitor&
{
    return topology_;
}

auto topology_connection_pool::statistics() -> topology_connection_pool_statistics
{
    std::scoped_lock lock(mutex_);
    topology_connection_pool_statistics result;
    result.server_pool_count = pools_.size();
    for (const auto& [_, pool] : pools_)
    {
        result.connection_count += pool->size();
        result.idle_connection_count += pool->idle_count();
        result.checked_out_connection_count += pool->checked_out_count();
        result.waiting_request_count += pool->waiter_count();
    }
    return result;
}

void topology_connection_pool::close() noexcept
{
    std::scoped_lock lock(mutex_);
    for (auto& [_, pool] : pools_)
        pool->close();
}
} // namespace cnetmod::mongodb
