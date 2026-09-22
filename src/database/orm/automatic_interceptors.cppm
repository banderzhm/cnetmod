export module cnetmod.orm.automatic_interceptors;

import std;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.logical_delete;
import cnetmod.orm.multi_tenant;
import cnetmod.orm.model_metadata;
import cnetmod.orm.automatic_field_fill;
import cnetmod.orm.data_permission;

namespace cnetmod::orm {

/**
 * @brief Selects the built-in ORM policies installed by the default chain.
 */
export struct automatic_interceptor_options
{
    bool logical_delete = true;
    bool multi_tenant = true;
    bool sql_safety = true;
    bool field_fill = true;
    bool optimistic_lock = true;
    std::shared_ptr<const data_permission_scope> data_permission;
};

/**
 * @brief Builds the standard, frozen policy chain for a mapped model.
 *
 * Tenant predicates run before logical-delete predicates. Both policies keep
 * values in the statement parameter vector; neither interpolates tenant data.
 */
export template <Model T>
auto make_automatic_interceptor_chain(
    automatic_interceptor_options options = {})
    -> std::expected<std::shared_ptr<const interceptor_chain>, std::string>
{
    auto chain = std::make_shared<interceptor_chain>();
    if (options.multi_tenant)
    {
        auto added = chain->add("multi_tenant", 100,
            [](sql_operation operation, intercepted_statement statement)
                -> std::expected<intercepted_statement, std::string>
            {
                auto& policy = global_multi_tenant_interceptor();
                switch (operation)
                {
                case sql_operation::insert:
                    statement.sql = policy.template inject_tenant_insert<T>(
                        std::move(statement.sql), statement.parameters);
                    break;
                case sql_operation::query:
                case sql_operation::update:
                case sql_operation::remove:
                    statement.sql = policy.template inject_tenant_condition<T>(
                        std::move(statement.sql), statement.parameters);
                    break;
                case sql_operation::execute:
                    break;
                }
                return statement;
            });
        if (!added)
            return std::unexpected(added.error());
    }
    if (options.logical_delete)
    {
        auto added = chain->add("logical_delete", 200,
            [](sql_operation operation, intercepted_statement statement)
                -> std::expected<intercepted_statement, std::string>
            {
                auto& policy = global_logical_delete_interceptor();
                if (operation == sql_operation::query)
                    statement.sql = policy.template inject_select_condition<T>(
                        std::move(statement.sql));
                else if (operation == sql_operation::remove)
                    statement.sql = policy.template transform_delete_to_update<T>(
                        std::move(statement.sql));
                return statement;
            });
        if (!added)
            return std::unexpected(added.error());
    }
    if (options.data_permission)
    {
        auto policy = data_permission_interceptor<T>{*options.data_permission};
        auto added = chain->add("data_permission", 150,
            [policy = std::move(policy)](sql_operation operation,
                intercepted_statement statement) mutable
                -> std::expected<intercepted_statement, std::string>
            {
                return policy.apply(operation, std::move(statement));
            });
        if (!added)
            return std::unexpected(added.error());
    }
    if (options.sql_safety)
    {
        auto added = chain->add("sql_safety", 1000,
            [](sql_operation operation, intercepted_statement statement)
                -> std::expected<intercepted_statement, std::string>
            {
                std::string_view sql = statement.sql;
                while (!sql.empty() && std::isspace(
                    static_cast<unsigned char>(sql.front())))
                    sql.remove_prefix(1);
                while (!sql.empty() && std::isspace(
                    static_cast<unsigned char>(sql.back())))
                    sql.remove_suffix(1);
                if (sql.ends_with(';'))
                    sql.remove_suffix(1);
                if (sql.find(';') != std::string_view::npos)
                    return std::unexpected("multiple SQL statements are not allowed");
                if ((operation == sql_operation::update ||
                        operation == sql_operation::remove) &&
                    sql.find("WHERE") == std::string_view::npos &&
                    sql.find("where") == std::string_view::npos)
                {
                    return std::unexpected(
                        "unbounded UPDATE/DELETE rejected by ORM SQL safety policy");
                }
                return statement;
            });
        if (!added)
            return std::unexpected(added.error());
    }
    if (options.field_fill)
        global_auto_fill_interceptor().template register_from_metadata<T>();
    auto frozen = chain->freeze();
    if (!frozen)
        return std::unexpected(frozen.error());
    std::shared_ptr<const interceptor_chain> result = std::move(chain);
    return result;
}

} // namespace cnetmod::orm
