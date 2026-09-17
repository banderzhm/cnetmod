/**
 * @brief Routes ORM sessions to named database shards and physical tables.
 */
export module cnetmod.orm.sharding.session_gateway;

import std;
import cnetmod.coro.task;
import cnetmod.orm.database_session;
import cnetmod.orm.sharding.shard_catalog;
import cnetmod.orm.sharding.shard_key;
import cnetmod.orm.sql_dialect;

export namespace cnetmod::orm {

/**
 * @brief Result of one physical shard in an explicit scatter-gather read.
 */
template <class T>
struct shard_read_result
{
    shard_route route;
    std::expected<T, std::string> result;
};

/**
 * @brief Sessions participating in one coordinated transaction.
 *
 * Each entry is pinned to one physical table. The context is valid only for
 * the duration of the transaction callback and cannot outlive its leases.
 */
template <class Session>
class distributed_session_context
{
public:
    struct entry
    {
        shard_route route;
        Session session;
    };

    distributed_session_context(std::shared_ptr<const shard_catalog> catalog,
        std::vector<entry> entries)
        : catalog_(std::move(catalog)), entries_(std::move(entries))
    {
    }

    /**
     * @brief Returns all distinct routed sessions in deterministic order.
     */
    [[nodiscard]] auto entries() noexcept -> std::span<entry>
    {
        return entries_;
    }

    /**
     * @brief Resolves a shard key to its pinned session.
     */
    [[nodiscard]] auto find(const shard_key& key) noexcept -> Session*
    {
        if (!catalog_)
            return nullptr;
        const auto routed = catalog_->route(key);
        if (!routed)
            return nullptr;
        const auto found = std::ranges::find_if(entries_, [&](const entry& value)
            {
                return value.route.database_shard == routed->database_shard &&
                    value.route.table_shard == routed->table_shard;
            });
        return found == entries_.end() ? nullptr : &found->session;
    }

private:
    std::shared_ptr<const shard_catalog> catalog_;
    std::vector<entry> entries_;
};

/**
 * @brief Acquires one database lease after selecting a deterministic shard.
 *
 * A routed write owns exactly one lease and one transaction. The operation
 * receives only the pinned session, so a transaction cannot silently cross a
 * database shard or physical table boundary.
 */
template <asynchronous_database_client Client, class Lease,
    class Session = database_session<Client>>
class sharded_session_gateway
{
public:
    using session_type = Session;
    using acquire_operation = std::function<
        task<std::expected<Lease, std::string>>(std::string_view)>;
    using client_accessor = std::function<Client&(Lease&)>;

    /**
     * @brief Creates a gateway over an immutable shard catalog.
     */
    sharded_session_gateway(std::shared_ptr<const shard_catalog> catalog,
        sql_dialect dialect, acquire_operation acquire, client_accessor client,
        bool scatter_gather_enabled = true,
        bool distributed_transactions_enabled = true)
        : catalog_(std::move(catalog)), dialect_(dialect), acquire_(std::move(acquire)), client_(std::move(client)), scatter_gather_enabled_(scatter_gather_enabled), distributed_transactions_enabled_(distributed_transactions_enabled)
    {
    }

    /**
     * @brief Executes a routed read without opening a transaction.
     */
    template <class T, class Operation>
    auto read(const shard_key& key, Operation&& operation)
        -> task<std::expected<T, std::string>>
    {
        auto routed = route(key);
        if (!routed)
            co_return std::unexpected(routed.error());

        auto lease = co_await acquire_(routed->instance);
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), routed->physical_table, dialect_};
        co_return co_await std::forward<Operation>(operation)(session);
    }

    /**
     * @brief Executes a routed write in one shard-local transaction.
     */
    template <class T, class Operation>
    auto write(const shard_key& key, Operation&& operation)
        -> task<std::expected<T, std::string>>
    {
        auto routed = route(key);
        if (!routed)
            co_return std::unexpected(routed.error());

        auto lease = co_await acquire_(routed->instance);
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), routed->physical_table, dialect_};
        co_return co_await session.template transaction<T>(
            [&]() -> task<std::expected<T, std::string>>
            {
                co_return co_await operation(session);
            });
    }

    /**
     * @brief Reads every physical shard and retains each shard's outcome.
     *
     * Scatter-gather is never implicit. Callers choose the operation and then
     * merge, sort, aggregate, or reject partial results explicitly.
     */
    template <class T, class Operation>
    auto scatter_read(Operation operation)
        -> task<std::expected<std::vector<shard_read_result<T>>, std::string>>
    {
        if (!scatter_gather_enabled_)
            co_return std::unexpected("scatter-gather is disabled");
        if (!catalog_)
            co_return std::unexpected("shard catalog is not configured");
        auto destinations = catalog_->routes();
        if (!destinations)
            co_return std::unexpected(destinations.error().message());

        std::vector<shard_read_result<T>> results;
        try
        {
            results.reserve(destinations->size());
        }
        catch (const std::bad_alloc&)
        {
            co_return std::unexpected("scatter-gather allocation failed");
        }
        for (const auto& destination : *destinations)
        {
            auto lease = co_await acquire_(destination.instance);
            if (!lease)
            {
                results.push_back({destination,
                    std::unexpected(std::move(lease.error()))});
                continue;
            }
            session_type session{client_(*lease), destination.physical_table,
                dialect_};
            try
            {
                results.push_back({destination,
                    co_await operation(destination, session)});
            }
            catch (const std::exception& error)
            {
                results.push_back({destination,
                    std::unexpected(std::string{error.what()})});
            }
            catch (...)
            {
                results.push_back({destination,
                    std::unexpected("scatter-gather operation failed")});
            }
        }
        co_return results;
    }

    /**
     * @brief Executes an explicit scatter read and applies a caller-owned merge.
     *
     * The merger receives all per-shard outcomes, including failures. This
     * keeps partial-result, ordering, pagination, and aggregation policy out of
     * the routing layer.
     */
    template <class Item, class Result, class Operation, class Merger>
    auto scatter_gather(Operation operation, Merger merger)
        -> task<std::expected<Result, std::string>>
    {
        auto scattered = co_await scatter_read<Item>(std::move(operation));
        if (!scattered)
            co_return std::unexpected(std::move(scattered.error()));
        try
        {
            co_return merger(std::move(*scattered));
        }
        catch (const std::exception& error)
        {
            co_return std::unexpected(std::string{error.what()});
        }
        catch (...)
        {
            co_return std::unexpected("scatter-gather merge failed");
        }
    }

    /**
     * @brief Executes one atomic transaction across routed database shards.
     *
     * A single database uses a regular local transaction. Multiple databases
     * use MySQL XA two-phase commit. The callback receives only sessions whose
     * leases are held until commit or rollback completes.
     */
    template <class T, class Operation>
    auto distributed_transaction(std::span<const shard_key> keys,
        Operation operation)
        -> task<std::expected<T, std::string>>
    {
        if (!distributed_transactions_enabled_)
            co_return std::unexpected("distributed transactions are disabled");
        if (dialect_ != sql_dialect::mysql)
            co_return std::unexpected("distributed transactions require MySQL XA");
        if (keys.empty())
            co_return std::unexpected("distributed transaction requires a shard key");

        std::vector<shard_route> routes;
        for (const auto& key : keys)
        {
            auto destination = route(key);
            if (!destination)
                co_return std::unexpected(destination.error());
            const auto duplicate = std::ranges::find_if(routes,
                [&](const shard_route& existing)
                {
                    return existing.database_shard == destination->database_shard &&
                        existing.table_shard == destination->table_shard;
                });
            if (duplicate == routes.end())
                routes.push_back(std::move(*destination));
        }

        struct database_lease
        {
            std::string instance;
            std::string branch;
            Lease lease;
            session_type control;
            bool started = false;
            bool ended = false;
        };

        std::vector<database_lease> leases;
        leases.reserve(routes.size());
        for (const auto& destination : routes)
        {
            if (std::ranges::any_of(leases, [&](const database_lease& value)
                    {
                        return value.instance == destination.instance;
                    }))
                continue;
            auto lease = co_await acquire_(destination.instance);
            if (!lease)
                co_return std::unexpected(lease.error());
            auto& client = client_(*lease);
            leases.push_back({destination.instance,
                std::format("branch-{}", leases.size()), std::move(*lease),
                session_type{client, destination.physical_table, dialect_}});
        }

        std::vector<typename distributed_session_context<session_type>::entry>
            sessions;
        sessions.reserve(routes.size());
        for (const auto& destination : routes)
        {
            auto found = std::ranges::find_if(leases,
                [&](const database_lease& value)
                {
                    return value.instance == destination.instance;
                });
            sessions.push_back({destination,
                session_type{client_(found->lease), destination.physical_table,
                    dialect_}});
        }
        distributed_session_context<session_type> context{catalog_,
            std::move(sessions)};

        static std::atomic<std::uint64_t> sequence{};
        const auto xid = std::format("cnetmod-{:x}-{:x}",
            static_cast<std::uint64_t>(std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()),
            sequence.fetch_add(1, std::memory_order_relaxed));
        const bool xa = leases.size() > 1;

        auto command = [](session_type& session, std::string sql)
            -> task<std::expected<void, std::string>>
        {
            auto result = co_await session.execute(sql);
            if (result.is_err())
                co_return std::unexpected(std::move(result.error_msg));
            co_return {};
        };
        auto rollback = [&](std::string original) -> task<std::expected<T, std::string>>
        {
            for (auto& item : leases)
            {
                if (!item.started)
                    continue;
                if (xa && !item.ended)
                    (void)co_await command(item.control,
                        std::format("XA END '{}','{}',1", xid, item.branch));
                (void)co_await command(item.control,
                    xa ? std::format("XA ROLLBACK '{}','{}',1", xid, item.branch)
                       : "ROLLBACK");
            }
            co_return std::unexpected(std::move(original));
        };

        for (auto& item : leases)
        {
            auto started = co_await command(item.control,
                xa ? std::format("XA START '{}','{}',1", xid, item.branch)
                   : "START TRANSACTION");
            if (!started)
                co_return co_await rollback(std::move(started.error()));
            item.started = true;
        }

        auto invoke_operation = [&]() -> task<std::expected<T, std::string>>
        {
            try
            {
                co_return co_await operation(context);
            }
            catch (const std::exception& error)
            {
                co_return std::unexpected(std::string{error.what()});
            }
            catch (...)
            {
                co_return std::unexpected("transaction callback failed");
            }
        };
        auto value = co_await invoke_operation();
        if (!value)
            co_return co_await rollback(std::move(value.error()));

        if (xa)
        {
            for (auto& item : leases)
            {
                auto ended = co_await command(item.control,
                    std::format("XA END '{}','{}',1", xid, item.branch));
                if (!ended)
                    co_return co_await rollback(std::move(ended.error()));
                item.ended = true;
                auto prepared = co_await command(item.control,
                    std::format("XA PREPARE '{}','{}',1", xid, item.branch));
                if (!prepared)
                    co_return co_await rollback(std::move(prepared.error()));
            }
        }
        for (auto& item : leases)
        {
            auto committed = co_await command(item.control,
                xa ? std::format("XA COMMIT '{}','{}',1", xid, item.branch)
                   : "COMMIT");
            if (!committed)
                co_return std::unexpected(std::format(
                    "distributed commit outcome is uncertain: {}",
                    committed.error()));
            item.started = false;
        }
        co_return std::move(*value);
    }

private:
    [[nodiscard]] auto route(const shard_key& key) const
        -> std::expected<shard_route, std::string>
    {
        if (!catalog_)
            return std::unexpected("shard catalog is not configured");
        auto destination = catalog_->route(key);
        if (!destination)
            return std::unexpected(destination.error().message());
        return std::move(*destination);
    }

    std::shared_ptr<const shard_catalog> catalog_;
    sql_dialect dialect_;
    acquire_operation acquire_;
    client_accessor client_;
    bool scatter_gather_enabled_ = true;
    bool distributed_transactions_enabled_ = true;
};

} // namespace cnetmod::orm
