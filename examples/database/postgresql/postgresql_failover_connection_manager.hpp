#pragma once

namespace postgresql_example {

struct database_connection_lease
{
    cnetmod::postgresql::pooled_connection connection;
    std::size_t endpoint_index{};
};

class failover_connection_manager
{
public:
    failover_connection_manager(cnetmod::io_context& context,
        const service_config& config, service_health& health)
        : context_(context), config_(config), health_(health)
    {
        pools_.reserve(config_.endpoints.size());
        for (std::size_t index = 0; index < config_.endpoints.size(); ++index)
        {
            cnetmod::postgresql::connection_pool_options options;
            options.connection = config_.connection_options(index,
                std::format("cnetmod-http-pool-{}", index));
            options.minimum_connections = config_.minimum_pool_connections;
            options.maximum_connections = config_.maximum_pool_connections;
            options.acquire_timeout = config_.acquire_timeout;
            pools_.push_back(std::make_unique<cnetmod::postgresql::connection_pool>(
                context_, std::move(options)));
        }
    }

    auto warm_up() -> cnetmod::task<bool>
    {
        for (std::size_t attempt = 0; attempt < config_.failover_attempts; ++attempt)
        {
            for (std::size_t index = 0; index < pools_.size(); ++index)
            {
                auto result = co_await pools_[index]->warm_up();
                if (result.ok() && co_await endpoint_is_writable(index))
                {
                    activate(index);
                    logger::info("PostgreSQL pool ready: endpoint={}, size={}, idle={}, checked_out={}, waiting={}",
                        config_.endpoints[index].display_name(), pools_[index]->size(),
                        pools_[index]->idle_count(), pools_[index]->checked_out_count(),
                        pools_[index]->waiter_count());
                    co_return true;
                }
                logger::warn("PostgreSQL endpoint is unavailable or read-only: " "endpoint={}, attempt={}, error={}",
                    config_.endpoints[index].display_name(), attempt + 1,
                    result.error_msg.empty() ? "not writable" : result.error_msg);
            }
            co_await cnetmod::async_sleep(context_, retry_delay(attempt));
        }
        health_.set_ready(false);
        co_return false;
    }

    auto acquire() -> cnetmod::task<std::expected<database_connection_lease,
        std::error_code>>
    {
        const auto starting_endpoint = active_endpoint_.load(std::memory_order_acquire);
        for (std::size_t offset = 0; offset < pools_.size(); ++offset)
        {
            const auto index = (starting_endpoint + offset) % pools_.size();
            auto acquired = co_await pools_[index]->acquire();
            if (acquired)
            {
                activate(index);
                co_return database_connection_lease{std::move(*acquired), index};
            }
            logger::warn("PostgreSQL pool acquire failed: endpoint={}, error={}",
                config_.endpoints[index].display_name(), acquired.error().message());
        }
        health_.set_ready(false);
        co_return std::unexpected(std::make_error_code(std::errc::host_unreachable));
    }

    void report_connection_failure(database_connection_lease& lease) noexcept
    {
        lease.connection.discard();
        advance_endpoint(lease.endpoint_index);
    }

    void advance_endpoint(std::size_t current_endpoint) noexcept
    {
        const auto next = (current_endpoint + 1) % pools_.size();
        active_endpoint_.store(next, std::memory_order_release);
        health_.set_active_endpoint(next);
        health_.set_ready(false);
    }

    auto probe() -> cnetmod::task<bool>
    {
        for (std::size_t attempt = 0; attempt < pools_.size(); ++attempt)
        {
            auto acquired = co_await acquire();
            if (!acquired)
                continue;
            auto result = co_await acquired->connection->query(
                "SELECT CASE WHEN pg_is_in_recovery() THEN 0 ELSE 1 END");
            if (result.is_err())
            {
                report_connection_failure(*acquired);
                continue;
            }
            if (!is_writable_result(result))
            {
                advance_endpoint(acquired->endpoint_index);
                continue;
            }
            health_.set_ready(true);
            co_return true;
        }
        health_.set_ready(false);
        co_return false;
    }

    auto close() -> cnetmod::task<void>
    {
        health_.set_ready(false);
        for (auto& pool : pools_)
            co_await pool->close();
    }

    [[nodiscard]] auto active_endpoint() const noexcept -> std::size_t
    {
        return active_endpoint_.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto pool_size() const noexcept -> std::size_t
    {
        return pools_[active_endpoint()]->size();
    }

    [[nodiscard]] auto idle_connections() const noexcept -> std::size_t
    {
        return pools_[active_endpoint()]->idle_count();
    }

    [[nodiscard]] auto checked_out_connections() const noexcept -> std::size_t
    {
        return pools_[active_endpoint()]->checked_out_count();
    }

    [[nodiscard]] auto waiting_requests() const noexcept -> std::size_t
    {
        return pools_[active_endpoint()]->waiter_count();
    }

private:
    auto endpoint_is_writable(std::size_t index) -> cnetmod::task<bool>
    {
        auto acquired = co_await pools_[index]->acquire();
        if (!acquired)
            co_return false;
        auto result = co_await (*acquired)->query(
            "SELECT CASE WHEN pg_is_in_recovery() THEN 0 ELSE 1 END");
        co_return result.ok() && is_writable_result(result);
    }

    [[nodiscard]] static auto is_writable_result(
        const cnetmod::postgresql::result_set& result) noexcept -> bool
    {
        if (result.rows.empty() || result.rows.front().empty())
            return false;
        const auto& value = result.rows.front().front();
        if (value.is_int64())
            return value.get_int64() == 1;
        if (value.is_uint64())
            return value.get_uint64() == 1;
        return value.get_string() == "1";
    }

    void activate(std::size_t index) noexcept
    {
        active_endpoint_.store(index, std::memory_order_release);
        health_.set_active_endpoint(index);
        health_.set_ready(true);
    }

    [[nodiscard]] auto retry_delay(std::size_t attempt) const
        -> std::chrono::milliseconds
    {
        const auto multiplier = std::size_t{1} << std::min<std::size_t>(attempt, 6);
        return config_.retry_backoff * multiplier;
    }

    cnetmod::io_context& context_;
    const service_config& config_;
    service_health& health_;
    std::vector<std::unique_ptr<cnetmod::postgresql::connection_pool>> pools_;
    std::atomic_size_t active_endpoint_{0};
};

} // namespace postgresql_example
