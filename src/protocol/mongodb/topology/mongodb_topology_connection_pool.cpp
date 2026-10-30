module cnetmod.protocol.mongodb;

import std;
import cnetmod.coro.timer;
import cnetmod.coro.mutex;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :error;
import :bson_document;
import :connection;
import :connection_pool;
import :server_description;
import :topology_monitor;
import :topology_connection_pool;
import :srv_seedlist;
import :server_address;

namespace cnetmod::mongodb {
topology_connection_pool::topology_connection_pool(io_context& context,
    topology_connection_pool_options options)
    : context_(context), options_(std::move(options)), topology_(options_.replica_set_name)
{
    next_srv_refresh_ = std::chrono::steady_clock::now() + std::max(options_.srv_refresh_interval, std::chrono::seconds{60});
    if (options_.seeds.empty())
        options_.seeds.push_back({"127.0.0.1", 27017});
}

auto topology_connection_pool::pool_for(const server_address& address)
    -> std::shared_ptr<connection_pool>
{
    concurrent_containers::exclusive_latch_guard lock{pools_latch_};
    if (closed_.load())
        return {};
    auto found = pools_.find(address);
    if (found != pools_.end())
        return found->second;
    auto pool_options = options_.per_server_pool;
    pool_options.connection.host = address.host;
    pool_options.connection.port = address.port;
    auto inserted = pools_.emplace(address,
        std::make_shared<connection_pool>(context_, std::move(pool_options)));
    return inserted.first->second;
}

void topology_connection_pool::mark_unknown_if_current(const server_address& address,
    const std::shared_ptr<connection_pool>& pool, const error& failure)
{
    concurrent_containers::shared_latch_guard lock{pools_latch_};
    const auto current = pools_.find(address);
    if (current != pools_.end() && current->second == pool)
        topology_.mark_unknown(address, failure);
}

auto topology_connection_pool::refresh(cancel_token* cancellation) -> task<result<void>>
{
    co_await refresh_mutex_.lock();
    async_lock_guard refresh_guard{refresh_mutex_, std::adopt_lock};
    if (closed_.load())
        co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
    if (cancellation && cancellation->is_cancelled())
        co_return std::unexpected(make_error(error_code::operation_cancelled, "MongoDB topology refresh cancelled"));
    const auto kind = topology_.kind();
    if (options_.refresh_srv && std::chrono::steady_clock::now() >= next_srv_refresh_ &&
        (kind == topology_kind::unknown || kind == topology_kind::sharded))
    {
        result<srv_seedlist> refreshed;
        try
        {
            refreshed = co_await options_.refresh_srv(options_.seeds);
        }
        catch (...)
        {
            refreshed = std::unexpected(make_error(error_code::server_selection_failed, "MongoDB SRV refresh provider failed"));
        }
        if (closed_.load())
            co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
        if (refreshed)
        {
            options_.seeds = std::move(refreshed->seeds);
            options_.srv_refresh_interval = std::max(refreshed->ttl, std::chrono::seconds{60});
            topology_.retain_servers(options_.seeds);
            concurrent_containers::exclusive_latch_guard lock{pools_latch_};
            std::erase_if(retired_pools_, [](const auto& pool)
                {
                    return pool->checked_out_count() == 0 && pool->connecting_count() == 0;
                });
            std::erase_if(pools_, [&](const auto& item)
                {
                    if (std::ranges::find(options_.seeds, item.first) != options_.seeds.end())
                        return false;
                    item.second->close();
                    retired_pools_.push_back(item.second);
                    return true;
                });
        }
        // DNS polling failure preserves the previous topology and retries later.
        next_srv_refresh_ = std::chrono::steady_clock::now() + (refreshed ? options_.srv_refresh_interval : std::chrono::seconds{60});
    }
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
        if (closed_.load() || (cancellation && cancellation->is_cancelled()))
            co_return std::unexpected(make_error(closed_.load() ? error_code::connection_closed : error_code::operation_cancelled, "MongoDB topology refresh stopped"));
        auto connection_options = options_.per_server_pool.connection;
        connection_options.host = address.host;
        connection_options.port = address.port;
        try
        {
            auto checked = co_await topology_.check_server(
                context_, std::move(connection_options), cancellation);
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
    auto node_pool = pool_for(selected->address);
    if (!node_pool)
        co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
    auto acquired = co_await node_pool->acquire();
    if (!acquired)
    {
        mark_unknown_if_current(selected->address, node_pool, acquired.error());
        auto refreshed = co_await refresh();
        if (!refreshed)
            co_return std::unexpected(refreshed.error());
        auto reselection = topology_.select_server(selection);
        if (!reselection)
            co_return std::unexpected(reselection.error());
        selected.emplace(std::move(*reselection));
        node_pool = pool_for(selected->address);
        if (!node_pool)
            co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
        co_return co_await node_pool->acquire();
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
    auto node_pool = pool_for(selected->address);
    if (!node_pool)
        co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
    auto acquired = co_await node_pool->acquire();
    if (!acquired)
    {
        mark_unknown_if_current(selected->address, node_pool, acquired.error());
        co_return std::unexpected(acquired.error());
    }
    if (selection.preference != read_preference::primary)
    {
        constexpr std::array<std::string_view, 5> names{
            "primary", "primaryPreferred", "secondary", "secondaryPreferred", "nearest"};
        bson_document preference{{"mode", names[static_cast<std::size_t>(selection.preference)]}};
        if (!selection.tag_sets.empty())
        {
            bson_array tags;
            for (const auto& set : selection.tag_sets)
            {
                bson_document tag;
                for (const auto& [key, value] : set)
                    tag.append(key, value);
                tags.emplace_back(std::move(tag));
            }
            preference.append("tags", std::move(tags));
        }
        if (selection.maximum_staleness.count() > 0)
            preference.append("maxStalenessSeconds", static_cast<std::int64_t>(selection.maximum_staleness.count()));
        document.set("$readPreference", std::move(preference));
    }
    auto response = co_await (*acquired)->command(database, std::move(document));
    if (!response && (!(*acquired)->is_open() || response.error().labels.contains("RetryableWriteError") || response.error().labels.contains("NotPrimaryError") || response.error().server_code == 10107 || response.error().server_code == 13435 || response.error().server_code == 11600 || response.error().server_code == 11602))
    {
        mark_unknown_if_current(selected->address, node_pool, response.error());
        acquired->discard();
    }
    co_return response;
}

auto topology_connection_pool::run_monitoring(std::stop_token stop,
    std::chrono::milliseconds heartbeat) -> task<void>
{
    heartbeat = std::max(heartbeat, std::chrono::milliseconds{500});
    topology_.set_heartbeat_frequency(heartbeat);
    cancel_token timer;
    std::stop_callback wake(stop, [&timer]
        {
            timer.cancel();
        });
    while (!stop.stop_requested())
    {
        auto ignored = co_await refresh(&timer);
        (void)ignored;
        timer.reset();
        if (stop.stop_requested())
            break;
        (void)co_await async_timer_wait(context_, heartbeat, timer);
    }
}

auto topology_connection_pool::topology() noexcept -> topology_monitor&
{
    return topology_;
}

auto topology_connection_pool::statistics() -> topology_connection_pool_statistics
{
    concurrent_containers::shared_latch_guard lock{pools_latch_};
    topology_connection_pool_statistics result;
    result.server_pool_count = pools_.size();
    std::vector<std::shared_ptr<connection_pool>> all = retired_pools_;
    for (const auto& [_, pool] : pools_)
        all.push_back(pool);
    for (const auto& pool : all)
    {
        result.connection_count += pool->size();
        result.idle_connection_count += pool->idle_count();
        result.checked_out_connection_count += pool->checked_out_count();
        result.waiting_request_count += pool->waiter_count();
        result.connecting_count += pool->connecting_count();
    }
    return result;
}

void topology_connection_pool::close() noexcept
{
    closed_.store(true);
    concurrent_containers::exclusive_latch_guard lock{pools_latch_};
    for (auto& [_, pool] : pools_)
        pool->close();
    for (auto& pool : retired_pools_)
        pool->close();
}
} // namespace cnetmod::mongodb
