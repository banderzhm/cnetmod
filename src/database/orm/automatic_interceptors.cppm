export module cnetmod.orm.automatic_interceptors;

import std;
import cnetmod.orm.interceptor_chain;
import cnetmod.orm.logical_delete;
import cnetmod.orm.multi_tenant;
import cnetmod.orm.model_metadata;
import cnetmod.orm.automatic_field_fill;
import cnetmod.orm.data_permission;
import cnetmod.orm.sql_dialect;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::orm {

/**
 * @brief Publishes one frozen interceptor chain to every operation of a repository.
 *
 * The first operation builds the chain; concurrent first operations may each
 * build one, and the first published chain wins so all operations share it.
 * Uses the cnetmod reader/writer latch instead of std::atomic<std::shared_ptr>,
 * which is not available on every supported standard library.
 */
export class interceptor_chain_cache
{
public:
    interceptor_chain_cache() = default;
    interceptor_chain_cache(const interceptor_chain_cache&) = delete;
    auto operator=(const interceptor_chain_cache&) -> interceptor_chain_cache& = delete;

    [[nodiscard]] auto load() const -> std::shared_ptr<const interceptor_chain>
    {
        concurrent_containers::shared_latch_guard guard{latch_};
        return chain_;
    }

    /**
     * @brief Publishes `candidate` unless a chain exists; returns the winner.
     */
    [[nodiscard]] auto publish(std::shared_ptr<const interceptor_chain> candidate)
        -> std::shared_ptr<const interceptor_chain>
    {
        concurrent_containers::exclusive_latch_guard guard{latch_};
        if (!chain_)
            chain_ = std::move(candidate);
        return chain_;
    }

private:
    mutable concurrent_containers::atomic_rw_latch latch_;
    std::shared_ptr<const interceptor_chain> chain_;
};

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
    // Strict mode is opt-in for existing applications. When required, a
    // missing request snapshot rejects every operation on tenant models.
    bool tenant_scope_required = false;
    std::shared_ptr<const tenant_scope> tenant;
    sql_dialect dialect = sql_dialect::mysql;
    std::string mapped_table;
    /// Overrides the default logical-delete representation for this model.
    std::optional<logical_delete_config> logical_delete_policy;
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
    if (options.tenant)
        options.tenant = std::make_shared<const tenant_scope>(*options.tenant);
    if (options.data_permission)
        options.data_permission =
            std::make_shared<const data_permission_scope>(*options.data_permission);
    auto chain = std::make_shared<interceptor_chain>();
    if (options.multi_tenant)
    {
        auto added = chain->add("multi_tenant", 100,
            [options](sql_operation operation, intercepted_statement statement)
                -> std::expected<intercepted_statement, std::string>
            {
                auto& policy = global_multi_tenant_interceptor();
                if (options.tenant_scope_required || options.tenant)
                {
                    const auto& metadata = model_traits<T>::meta();
                    const column_def* tenant_column = nullptr;
                    for (const auto& field : metadata.fields)
                    {
                        if (!has_flag(field.col.flags, col_flag::tenant_id) &&
                            field.col.column_name != "tenant_id")
                            continue;
                        if (tenant_column)
                            return std::unexpected("model has multiple tenant columns");
                        tenant_column = &field.col;
                    }
                    if (!tenant_column)
                    {
                        for (const auto& field : metadata.fields)
                            if (has_flag(field.col.flags, col_flag::data_partition) ||
                                has_flag(field.col.flags, col_flag::data_owner))
                                return std::unexpected(
                                    "strict SaaS data-scope model requires a tenant column");
                        return statement;
                    }
                    if (!options.tenant)
                        return std::unexpected("tenant scope is required");
                    return apply_tenant_scope(operation, std::move(statement),
                        options.mapped_table.empty() ? metadata.table_name
                            : std::string_view{options.mapped_table},
                        tenant_column->column_name,
                        *options.tenant, options.dialect);
                }
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
        logical_delete_interceptor policy{options.logical_delete_policy.value_or(
            global_logical_delete_interceptor().config())};
        auto added = chain->add("logical_delete", 200,
            [policy = std::move(policy)](sql_operation operation,
                intercepted_statement statement)
                -> std::expected<intercepted_statement, std::string>
            {
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
        auto policy = data_permission_interceptor<T>{
            *options.data_permission, options.dialect,
            options.tenant_scope_required || static_cast<bool>(options.tenant),
            options.mapped_table};
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
    auto frozen = chain->freeze();
    if (!frozen)
        return std::unexpected(frozen.error());
    std::shared_ptr<const interceptor_chain> result = std::move(chain);
    return result;
}

} // namespace cnetmod::orm
