module;

#include <cnetmod/config.hpp>

/**
 * @brief Relational repositories as injectable application components.
 *
 * Repositories are created by repository_factory<T>, registered through
 * add_repository<T>() and injected by the component container. The factory
 * resolves its managed data source once during build, so a missing or
 * ambiguous data source fails the build instead of the first request.
 *
 * - shared(): one repository without request scope, reused for the process;
 * - for_request(): a repository bound to the request's tenant and data
 *   permission scopes, suitable for row-level security.
 *
 * Strict SaaS mode is read from orm.tenant_scope_required and frozen when the
 * factory is created.
 */
export module cnetmod.application.data;

#if defined(CNETMOD_HAS_ORM) && \
    (defined(CNETMOD_HAS_PROTOCOL_MYSQL) || defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL))

import std;
import cnetmod.application.components;
import cnetmod.application.configuration;
import cnetmod.application.orm_repository;
import cnetmod.application.runtime;
import cnetmod.application.service_registry;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.data_permission;
import cnetmod.orm.model_metadata;
import cnetmod.orm.multi_tenant;
import cnetmod.protocol.http;
    #if defined(CNETMOD_HAS_PROTOCOL_MYSQL)
import cnetmod.application.mysql;
import cnetmod.application.mysql_orm;
    #endif
    #if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL)
import cnetmod.application.postgresql;
import cnetmod.application.postgresql_orm;
    #endif

namespace cnetmod::application {

/**
 * @brief Selects the managed relational database behind a repository.
 */
export enum class database_provider
{
    automatic,
    mysql,
    postgresql
};

    #if defined(CNETMOD_HAS_PROTOCOL_MYSQL) && \
        defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL)
export template <orm::Model T>
using managed_repository = application_repository<T,
    mysql_repository_handle<T>, postgresql_repository_handle<T>>;
    #elif defined(CNETMOD_HAS_PROTOCOL_MYSQL)
export template <orm::Model T>
using managed_repository = application_repository<T,
    mysql_repository_handle<T>>;
    #else
export template <orm::Model T>
using managed_repository = application_repository<T,
    postgresql_repository_handle<T>>;
    #endif

/**
 * @brief Data source and policies of one repository binding.
 */
export struct repository_options
{
    /// Managed data source instance, for example "primary".
    std::string instance{"default"};
    /// Model policies: logical delete, field fill, tenant, safety.
    orm::automatic_interceptor_options policies;
    database_provider provider = database_provider::automatic;
};

/**
 * @brief Creates repositories of T for one data source and policy set.
 */
export template <orm::Model T>
class repository_factory
{
public:
    /**
     * @brief Resolves the data source and creates the shared repository.
     */
    [[nodiscard]] static auto create(service_registry& services,
        const application_configuration& configuration,
        repository_options options)
        -> std::expected<repository_factory, std::error_code>
    {
        repository_factory factory{services, std::move(options),
            configuration.orm.tenant_scope_required};
        auto provider = factory.select_provider();
        if (!provider)
            return std::unexpected(provider.error());
        factory.options_.provider = *provider;
        if (factory.tenant_scope_required_ && !factory.options_.policies.tenant &&
            factory.is_tenant_model())
        {
            // The shared repository has no request scope; strict SaaS mode
            // permits only request-bound access to tenant models.
            factory.shared_.reset();
            return factory;
        }
        auto shared = factory.make(factory.options_.policies);
        if (!shared)
            return std::unexpected(shared.error());
        factory.shared_ = std::make_shared<managed_repository<T>>(
            std::move(*shared));
        return factory;
    }

    /**
     * @brief Returns the process-wide repository without request scope.
     *
     * Throws std::logic_error in strict SaaS mode for tenant models, which
     * require for_request().
     */
    [[nodiscard]] auto shared() const -> managed_repository<T>&
    {
        if (!shared_)
            throw std::logic_error(
                "tenant model repositories require a request scope in strict "
                "SaaS mode; use for_request()");
        return *shared_;
    }

    /**
     * @brief Shared ownership of the process-wide repository, or null in
     *        strict SaaS mode for tenant models.
     */
    [[nodiscard]] auto shared_handle() const noexcept
        -> std::shared_ptr<managed_repository<T>>
    {
        return shared_;
    }

    /**
     * @brief Creates a repository bound to the request's tenant and data scope.
     *
     * The repository owns snapshots of both scopes. When authentication bound
     * no data_permission_scope, an empty scope is applied, which denies every
     * row of models marked DATA_PARTITION or DATA_OWNER (fail closed).
     */
    [[nodiscard]] auto for_request(const http::request_context& request) const
        -> std::expected<managed_repository<T>, std::error_code>
    {
        auto policies = options_.policies;
        if (const auto* tenant = request.scope().template find<orm::tenant_scope>())
            policies.tenant = std::make_shared<const orm::tenant_scope>(*tenant);
        if (const auto* scope =
                request.scope().template find<orm::data_permission_scope>())
            policies.data_permission =
                std::make_shared<const orm::data_permission_scope>(*scope);
        else
            policies.data_permission =
                std::make_shared<const orm::data_permission_scope>();
        return make(std::move(policies));
    }

    [[nodiscard]] auto options() const noexcept -> const repository_options&
    {
        return options_;
    }

private:
    repository_factory(service_registry& services, repository_options options,
        bool tenant_scope_required) noexcept
        : services_(&services), options_(std::move(options)),
          tenant_scope_required_(tenant_scope_required)
    {
    }

    [[nodiscard]] auto is_tenant_model() const noexcept -> bool
    {
        for (const auto& field : orm::model_traits<T>::meta().fields)
            if (orm::has_flag(field.col.flags, orm::col_flag::tenant_id) ||
                field.col.column_name == "tenant_id")
                return true;
        return false;
    }

    [[nodiscard]] auto select_provider() const
        -> std::expected<database_provider, std::error_code>
    {
    #if defined(CNETMOD_HAS_PROTOCOL_MYSQL)
        const bool mysql_available =
            services_->find<mysql_service>(options_.instance) != nullptr;
    #else
        constexpr bool mysql_available = false;
    #endif
    #if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL)
        const bool postgresql_available =
            services_->find<postgresql_service>(options_.instance) != nullptr;
    #else
        constexpr bool postgresql_available = false;
    #endif
        switch (options_.provider)
        {
        case database_provider::automatic:
            if (mysql_available == postgresql_available)
                return std::unexpected(std::make_error_code(mysql_available
                        ? std::errc::address_in_use
                        : std::errc::no_such_file_or_directory));
            return mysql_available ? database_provider::mysql
                                   : database_provider::postgresql;
        case database_provider::mysql:
            if (!mysql_available)
                return std::unexpected(
                    std::make_error_code(std::errc::no_such_file_or_directory));
            return database_provider::mysql;
        case database_provider::postgresql:
            if (!postgresql_available)
                return std::unexpected(
                    std::make_error_code(std::errc::no_such_file_or_directory));
            return database_provider::postgresql;
        }
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
    }

    [[nodiscard]] auto make(orm::automatic_interceptor_options policies) const
        -> std::expected<managed_repository<T>, std::error_code>
    {
        if (tenant_scope_required_)
        {
            // Strict SaaS mode: a tenant model without a tenant snapshot would
            // otherwise run unscoped, so refuse before any SQL is built.
            if (!policies.tenant && is_tenant_model())
                return std::unexpected(
                    std::make_error_code(std::errc::permission_denied));
            policies.multi_tenant = true;
            policies.tenant_scope_required = true;
        }
    #if defined(CNETMOD_HAS_PROTOCOL_MYSQL)
        if (options_.provider == database_provider::mysql)
        {
            auto* mysql = services_->find<mysql_service>(options_.instance);
            if (!mysql)
                return std::unexpected(
                    std::make_error_code(std::errc::no_such_file_or_directory));
            auto handle = make_mysql_repository_handle<T>(*mysql, std::move(policies));
            if (!handle)
                return std::unexpected(handle.error());
            return managed_repository<T>{std::move(*handle)};
        }
    #endif
    #if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL)
        if (options_.provider == database_provider::postgresql)
        {
            auto* postgresql = services_->find<postgresql_service>(options_.instance);
            if (!postgresql)
                return std::unexpected(
                    std::make_error_code(std::errc::no_such_file_or_directory));
            auto handle = make_postgresql_repository_handle<T>(*postgresql,
                std::move(policies));
            if (!handle)
                return std::unexpected(handle.error());
            return managed_repository<T>{std::move(*handle)};
        }
    #endif
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
    }

    service_registry* services_;
    repository_options options_;
    bool tenant_scope_required_;
    std::shared_ptr<managed_repository<T>> shared_;
};

/**
 * @brief Registers repository_factory<T> and managed_repository<T>.
 *
 * Both are registered under `name`. Resolving managed_repository<T> yields
 * the factory's shared repository; resolving repository_factory<T> gives
 * access to request-scoped repositories.
 */
export template <orm::Model T>
auto add_repository(component_collection& components,
    repository_options options = {}, std::string name = {})
    -> component_collection&
{
    components.singleton<repository_factory<T>>(name,
        [options = std::move(options)](component_resolver& resolver)
        {
            auto& runtime = resolver.get<application_runtime>();
            auto created = repository_factory<T>::create(
                resolver.get<service_registry>(), runtime.configuration(), options);
            if (!created)
                throw std::system_error(created.error(),
                    std::format("repository data source '{}' is unavailable",
                        options.instance));
            return std::make_shared<repository_factory<T>>(std::move(*created));
        });
    const auto factory_name = name;
    components.singleton<managed_repository<T>>(std::move(name),
        [factory_name](component_resolver& resolver)
            -> std::shared_ptr<managed_repository<T>>
        {
            auto factory = resolver.shared<repository_factory<T>>(factory_name);
            auto shared = factory->shared_handle();
            if (!shared)
                throw std::logic_error(
                    "tenant model repositories are request-scoped in strict "
                    "SaaS mode; inject repository_factory<T> instead");
            return shared;
        });
    return components;
}

} // namespace cnetmod::application

#endif
