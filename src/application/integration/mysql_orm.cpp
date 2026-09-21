module cnetmod.application.mysql_orm;

#if defined(CNETMOD_HAS_PROTOCOL_MYSQL) && defined(CNETMOD_HAS_ORM)
import std;
import cnetmod.application.mysql;
import cnetmod.application.service_registry;
import cnetmod.coro.task;
import cnetmod.orm.sharding.shard_strategy;
import cnetmod.orm.sql_dialect;
import cnetmod.protocol.mysql;

namespace cnetmod::application {

auto make_mysql_session_gateway(mysql_service& service)
    -> mysql_session_gateway
{
    return mysql_session_gateway{
        orm::sql_dialect::mysql,
        []() -> task<std::expected<void, std::string>>
        {
            co_return std::expected<void, std::string>{};
        },
        [&service]()
            -> task<std::expected<mysql::pooled_connection, std::string>>
        {
            auto connection = co_await service.pool().async_get_connection();
            if (!connection)
                co_return std::unexpected(connection.error().message());
            co_return std::move(*connection);
        },
        [](mysql::pooled_connection& connection) -> mysql::client&
        {
            return connection.get();
        }};
}

auto make_mysql_sharded_session_gateway(service_registry& services,
    std::shared_ptr<const orm::shard_catalog> catalog)
    -> std::expected<mysql_sharded_session_gateway, std::error_code>
{
    if (!catalog || !catalog->frozen())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    std::unordered_map<std::string, mysql_service*> pools;
    for (const auto& instance : catalog->instances())
    {
        auto* service = services.find<mysql_service>(instance);
        if (!service)
            return std::unexpected(
                std::make_error_code(std::errc::no_such_file_or_directory));
        pools.emplace(instance, service);
    }

    return mysql_sharded_session_gateway{
        std::move(catalog), orm::sql_dialect::mysql,
        [pools = std::move(pools)](std::string_view instance)
            -> task<std::expected<mysql::pooled_connection, std::string>>
        {
            const auto found = pools.find(std::string{instance});
            if (found == pools.end())
                co_return std::unexpected("mysql shard is not registered");
            auto connection = co_await found->second->pool().async_get_connection();
            if (!connection)
                co_return std::unexpected(connection.error().message());
            co_return std::move(*connection);
        },
        [](mysql::pooled_connection& connection) -> mysql::client&
        {
            return connection.get();
        }};
}

auto auto_configure_mysql_sharding(
    const orm_sharding_configuration& configuration,
    service_registry& services) -> std::expected<void, std::error_code>
{
    if (!configuration.enabled)
        return {};
    try
    {
        for (const auto& [name, topology] : configuration.topologies)
        {
            auto catalog = std::make_shared<orm::shard_catalog>();
            for (const auto& instance : topology.databases)
            {
                auto added = catalog->add_database(instance);
                if (!added)
                    return std::unexpected(added.error());
            }
            auto frozen = catalog->freeze(topology.logical_table,
                topology.table_count,
                std::make_shared<orm::hash_shard_strategy>());
            if (!frozen)
                return std::unexpected(frozen.error());

            std::unordered_map<std::string, mysql_service*> pools;
            for (const auto& instance : catalog->instances())
            {
                auto* service = services.find<mysql_service>(instance);
                if (!service)
                    return std::unexpected(std::make_error_code(
                        std::errc::no_such_file_or_directory));
                pools.emplace(instance, service);
            }
            auto gateway = std::make_shared<mysql_sharded_session_gateway>(
                catalog, orm::sql_dialect::mysql,
                [pools = std::move(pools)](std::string_view instance)
                    -> task<std::expected<mysql::pooled_connection, std::string>>
                {
                    const auto found = pools.find(std::string{instance});
                    if (found == pools.end())
                        co_return std::unexpected("mysql shard is not registered");
                    auto connection = co_await found->second->pool()
                                          .async_get_connection();
                    if (!connection)
                        co_return std::unexpected(connection.error().message());
                    co_return std::move(*connection);
                },
                [](mysql::pooled_connection& connection) -> mysql::client&
                {
                    return connection.get();
                },
                topology.scatter_gather,
                topology.distributed_transactions);
            auto registered = services.add_named<mysql_sharded_session_gateway>(
                name, std::move(gateway));
            if (!registered)
                return registered;
        }
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
}

} // namespace cnetmod::application
#endif
