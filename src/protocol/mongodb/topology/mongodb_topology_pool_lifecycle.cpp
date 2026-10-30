module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :error;
import :connection_pool;
import :topology_monitor;
import :topology_connection_pool;

namespace cnetmod::mongodb {
auto topology_connection_pool::warm_up(cancel_token& cancellation, server_selection_options selection) -> task<result<void>>
{
    auto refreshed = co_await refresh(&cancellation);
    if (!refreshed)
        co_return std::unexpected(refreshed.error());
    auto selected = topology_.select_server(selection);
    if (!selected)
        co_return std::unexpected(selected.error());
    auto pool = pool_for(selected->address);
    if (!pool)
        co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
    auto warmed = co_await pool->warm_up(cancellation);
    if (!warmed)
        co_return std::unexpected(warmed.error());
    co_return co_await pool->health_check(cancellation);
}

auto topology_connection_pool::health_check(cancel_token& cancellation, server_selection_options selection) -> task<result<void>>
{
    auto refreshed = co_await refresh(&cancellation);
    if (!refreshed)
        co_return std::unexpected(refreshed.error());
    auto selected = topology_.select_server(selection);
    if (!selected)
        co_return std::unexpected(selected.error());
    auto pool = pool_for(selected->address);
    if (!pool)
        co_return std::unexpected(make_error(error_code::connection_closed, "MongoDB topology pool is closed"));
    co_return co_await pool->health_check(cancellation);
}

auto topology_connection_pool::async_close() -> task<void>
{
    close();
    std::vector<std::shared_ptr<connection_pool>> pools;
    {
        concurrent_containers::shared_latch_guard lock{pools_latch_};
        pools = retired_pools_;
        for (const auto& [_, pool] : pools_)
            pools.push_back(pool);
    }
    for (auto& pool : pools)
        co_await pool->async_close();
}
} // namespace cnetmod::mongodb
