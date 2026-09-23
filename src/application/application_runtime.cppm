module;

#include <cnetmod/config.hpp>

/**
 * @brief Controlled access to application-owned execution and telemetry.
 */
export module cnetmod.application.runtime;

import std;
import cnetmod.application.async_file_template;
import cnetmod.application.configuration;
import cnetmod.application.rest_template;
import cnetmod.application.json_template;
import cnetmod.application.orm_repository;
import cnetmod.application.recovery_policy;
import cnetmod.application.service_registry;
import cnetmod.application.task_supervisor;
import cnetmod.coro.bridge;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.pool;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.compress;
#ifdef CNETMOD_HAS_SSL
import cnetmod.security.jwt;
#endif
#ifdef CNETMOD_HAS_CHAT_MODEL
import cnetmod.application.chat_model_service;
import cnetmod.application.chat_model_template;
#endif
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import cnetmod.application.redis;
import cnetmod.protocol.redis;
#endif
#if defined(CNETMOD_HAS_PROTOCOL_MYSQL) && defined(CNETMOD_HAS_ORM)
import cnetmod.application.mysql;
import cnetmod.application.mysql_orm;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.model_metadata;
import cnetmod.orm.data_permission;
#endif
#if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL) && defined(CNETMOD_HAS_ORM)
import cnetmod.application.postgresql;
import cnetmod.application.postgresql_orm;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.model_metadata;
import cnetmod.orm.data_permission;
#endif

namespace cnetmod::application {

#if defined(CNETMOD_HAS_ORM) &&             \
    (defined(CNETMOD_HAS_PROTOCOL_MYSQL) || \
        defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL))
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
#endif

/**
 * @brief Provides supervised background execution without exposing raw I/O state.
 *
 * The facade does not own its dependencies and remains valid only while its
 * application host is alive. CPU work is returned to the host event loop after
 * completion. Long-running operations must use spawn_managed() so shutdown can
 * cancel and join them deterministically.
 */
export class application_runtime
{
public:
    /**
     * @brief Binds the facade to application-owned runtime components.
     *
     * Application hosts construct this object after all referenced components.
     * Callers must preserve those components for the facade lifetime.
     */
    application_runtime(io_context& io, thread_pool& cpu_pool,
        task_supervisor& supervisor,
        observability::telemetry_hub& telemetry,
        service_registry& services, std::stop_token cancellation,
        const application_configuration& configuration) noexcept;

    application_runtime(const application_runtime&) = delete;
    auto operator=(const application_runtime&) -> application_runtime& = delete;

    /**
     * @brief Registers a uniquely named, cancellable background operation.
     * @return Success when ownership was accepted by the task supervisor.
     */
    [[nodiscard]] auto spawn_managed(std::string name,
        supervised_task operation, recovery_policy recovery = {},
        bool required = true, std::function<void()> stop_request = {},
        deadline recovery_deadline = {})
        -> std::expected<void, std::error_code>;

    /**
     * @brief Executes CPU-bound work on the host pool and resumes on its event loop.
     *
     * The callable must not retain references whose lifetime ends before this
     * operation completes. Exceptions are propagated to the awaiting coroutine.
     */
    template <typename Function>
    requires std::invocable<std::decay_t<Function>>
    auto offload(Function&& function)
        -> task<std::invoke_result_t<std::decay_t<Function>>>
    {
        if (stop_requested())
            throw std::system_error(
                std::make_error_code(std::errc::operation_canceled));
        if constexpr (std::is_void_v<
                          std::invoke_result_t<std::decay_t<Function>>>)
        {
            co_await blocking_invoke(cpu_pool_, io_,
                std::decay_t<Function>(std::forward<Function>(function)));
            co_return;
        }
        else
        {
            co_return co_await blocking_invoke(cpu_pool_, io_,
                std::decay_t<Function>(std::forward<Function>(function)));
        }
    }

    /**
     * @brief Schedules the awaiting route coroutine on the managed CPU pool.
     *
     * Pair this operation with resume_to_event_loop() before accessing HTTP
     * request or response state. Prefer offload() when the CPU operation can
     * be expressed as a callable because it restores the event loop even when
     * the callable throws.
     */
    [[nodiscard]] auto schedule_on_cpu() noexcept -> pool_post_awaitable;

    /**
     * @brief Resumes the awaiting coroutine on the application event loop.
     *
     * This removes the need for route code to retain or pass a raw io_context.
     */
    [[nodiscard]] auto resume_to_event_loop() noexcept -> post_awaitable;

    /**
     * @brief Returns application-managed asynchronous file operations.
     */
    [[nodiscard]] auto files() noexcept -> async_file_template&;

    /**
     * @brief Returns pooled and observable outbound HTTP operations.
     */
    [[nodiscard]] auto rest() noexcept -> rest_template&;

    /**
     * @brief Returns typed JSON operations offloaded to the application CPU pool.
     */
    [[nodiscard]] auto json() noexcept -> json_template&;

#ifdef CNETMOD_HAS_SSL
    /** Signs a JWT on the managed CPU executor. */
    [[nodiscard]] auto sign_jwt(const security::jwt_sign_options& options,
        std::string_view secret)
        -> task<std::expected<std::string, std::string>>;

    /**
     * @brief Verifies and decodes a JWT on the managed CPU executor.
     */
    [[nodiscard]] auto verify_jwt(std::string_view token,
        std::string_view secret)
        -> task<std::expected<security::jwt_claims, std::string>>;
#endif

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
    /**
     * @brief Resolves a namespaced Redis template from a managed service.
     *
     * The returned template borrows the service-owned connection pool and
     * remains valid for the application host lifetime.
     */
    [[nodiscard]] auto redis(std::string_view instance = "default",
        redis::template_options options = {})
        -> std::expected<redis::redis_template, std::error_code>;
#endif

#ifdef CNETMOD_HAS_CHAT_MODEL
    /**
     * @brief Resolves a named managed provider as a chat model template.
     *
     * Resolve the template when handling a request or after build() completes,
     * because auto-configuration registers managed services during host build.
     */
    [[nodiscard]] auto chat_model(std::string_view instance = "default",
        chat_model_template_options options = {})
        -> std::expected<chat_model_template, std::error_code>;

    /**
     * @brief Atomically reloads one named chat model provider generation.
     *
     * Provider validation or connection failure leaves the active generation
     * untouched. In-flight requests finish on their existing generation.
     */
    [[nodiscard]] auto reconfigure_chat_model(std::string_view instance,
        chat_model_reconfiguration configuration,
        cancel_token* cancellation = nullptr)
        -> task<std::expected<void, std::error_code>>;
#endif

#if defined(CNETMOD_HAS_ORM) &&             \
    (defined(CNETMOD_HAS_PROTOCOL_MYSQL) || \
        defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL))
    /**
     * @brief Resolves a provider-neutral repository from a managed data source.
     *
     * Automatic selection succeeds only when exactly one supported provider
     * owns the requested instance name. Specify a provider to disambiguate two
     * data sources that deliberately share an instance name.
     */
    template <orm::Model T>
    [[nodiscard]] auto repository(std::string_view instance = "default",
        orm::automatic_interceptor_options interceptors = {},
        database_provider provider = database_provider::automatic)
        -> std::expected<managed_repository<T>, std::error_code>
    {
    #if defined(CNETMOD_HAS_PROTOCOL_MYSQL)
        auto* mysql = services_.find<mysql_service>(instance);
        const bool mysql_available = mysql != nullptr;
    #else
        constexpr bool mysql_available = false;
    #endif
    #if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL)
        auto* postgresql = services_.find<postgresql_service>(instance);
        const bool postgresql_available = postgresql != nullptr;
    #else
        constexpr bool postgresql_available = false;
    #endif

        if (provider == database_provider::automatic)
        {
            if (mysql_available == postgresql_available)
            {
                return std::unexpected(std::make_error_code(mysql_available
                        ? std::errc::address_in_use
                        : std::errc::no_such_file_or_directory));
            }
            provider = mysql_available ? database_provider::mysql
                                       : database_provider::postgresql;
        }

    #if defined(CNETMOD_HAS_PROTOCOL_MYSQL)
        if (provider == database_provider::mysql)
        {
            if (!mysql)
                return std::unexpected(std::make_error_code(
                    std::errc::no_such_file_or_directory));
            auto handle = make_mysql_repository_handle<T>(*mysql, interceptors);
            if (!handle)
                return std::unexpected(handle.error());
            return managed_repository<T>{std::move(*handle)};
        }
    #endif

    #if defined(CNETMOD_HAS_PROTOCOL_POSTGRESQL)
        if (provider == database_provider::postgresql)
        {
            if (!postgresql)
                return std::unexpected(std::make_error_code(
                    std::errc::no_such_file_or_directory));
            auto handle = make_postgresql_repository_handle<T>(*postgresql,
                interceptors);
            if (!handle)
                return std::unexpected(handle.error());
            return managed_repository<T>{std::move(*handle)};
        }
    #endif

        return std::unexpected(
            std::make_error_code(std::errc::operation_not_supported));
    }

    /**
     * @brief Resolves a repository with request-level data permission enabled.
     *
     * Authentication middleware may bind `orm::data_permission_scope` to the
     * HTTP request scope. The resulting repository owns a snapshot of that
     * value, so every typed, XML and transactional operation uses the same
     * frozen interceptor chain without thread-local state.
     */
    template <orm::Model T>
    [[nodiscard]] auto repository(const http::request_context& request,
        std::string_view instance = "default",
        orm::automatic_interceptor_options interceptors = {},
        database_provider provider = database_provider::automatic)
        -> std::expected<managed_repository<T>, std::error_code>
    {
        if (const auto* scope =
                request.scope().template find<orm::data_permission_scope>())
            interceptors.data_permission =
                std::make_shared<const orm::data_permission_scope>(*scope);
        else
            interceptors.data_permission =
                std::make_shared<const orm::data_permission_scope>();
        return repository<T>(instance, std::move(interceptors), provider);
    }
#endif

    /**
     * @brief Creates response compression managed by the application CPU pool.
     *
     * The returned middleware uses bounded concurrency, request cancellation,
     * and the application measurement sink. A compression call already running
     * inside a native codec cannot be preempted; its result is discarded after
     * cancellation and the bounded operation is allowed to finish.
     */
    [[nodiscard]] auto compression(compress_options options = {})
        -> http::middleware_fn;

    /**
     * @brief Reports whether application shutdown has been requested.
     */
    [[nodiscard]] auto stop_requested() const noexcept -> bool;

    /**
     * @brief Returns the broadcast application shutdown signal.
     *
     * The token is safe to copy and supports stop callbacks. Individual I/O
     * operations must continue to use their own cancel_token instances.
     */
    [[nodiscard]] auto cancellation() const noexcept -> std::stop_token;

    /**
     * @brief Returns the task supervisor used by this application.
     */
    [[nodiscard]] auto tasks() noexcept -> task_supervisor&;

    /**
     * @brief Returns the application telemetry composition root.
     */
    [[nodiscard]] auto telemetry() noexcept
        -> observability::telemetry_hub&;

    /**
     * @brief Returns the validated immutable-at-runtime application settings.
     *
     * Restart-only fields, including credentials, remain unchanged during a
     * hot reload. Callers must not copy secrets into logs or telemetry.
     */
    [[nodiscard]] auto configuration() const noexcept
        -> const application_configuration&;

private:
    io_context& io_;
    thread_pool& cpu_pool_;
    task_supervisor& supervisor_;
    observability::telemetry_hub& telemetry_;
    service_registry& services_;
    std::stop_token cancellation_;
    const application_configuration& configuration_;
    async_file_template files_;
    rest_template rest_;
    json_template json_;
};

} // namespace cnetmod::application
