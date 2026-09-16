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
        sql_dialect dialect, acquire_operation acquire, client_accessor client)
        : catalog_(std::move(catalog)), dialect_(dialect), acquire_(std::move(acquire)), client_(std::move(client))
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
};

} // namespace cnetmod::orm
