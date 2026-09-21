export module cnetmod.application.orm_repository;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.database_session;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.repository_contract;
import cnetmod.orm.repository;
import cnetmod.orm.repository_impl;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_reflection;
import cnetmod.orm.xml_mapper_registry;

namespace cnetmod::application {

/**
 * @brief Owns a gateway-backed repository without exposing database details.
 */
export template <orm::Model T, typename Gateway,
    typename StreamStrategy = orm::session_stream_strategy>
class orm_repository_handle
{
public:
    orm_repository_handle(std::shared_ptr<Gateway> gateway,
        orm::automatic_interceptor_options interceptors = {})
        : gateway_(std::move(gateway)),
          repository_(std::make_unique<orm::repository<T, Gateway,
                  StreamStrategy>>(*gateway_, interceptors))
    {
    }

    [[nodiscard]] auto get() noexcept
        -> orm::repository<T, Gateway, StreamStrategy>&
    {
        return *repository_;
    }

    [[nodiscard]] auto get() const noexcept
        -> const orm::repository<T, Gateway, StreamStrategy>&
    {
        return *repository_;
    }

    [[nodiscard]] auto operator->() noexcept
        -> orm::repository<T, Gateway, StreamStrategy>*
    {
        return repository_.get();
    }

    [[nodiscard]] auto operator->() const noexcept
        -> const orm::repository<T, Gateway, StreamStrategy>*
    {
        return repository_.get();
    }

private:
    std::shared_ptr<Gateway> gateway_;
    std::unique_ptr<orm::repository<T, Gateway, StreamStrategy>> repository_;
};

/**
 * @brief Provider-neutral application repository over supported database handles.
 *
 * Backend selection happens once when the runtime resolves a named data source.
 * Every subsequent operation dispatches to the same concrete repository while
 * preserving the common Mapper/Repository result contract.
 */
export template <orm::Model T, typename... Handles>
class application_repository
{
public:
    template <typename Handle>
    requires((std::same_as<std::remove_cvref_t<Handle>, Handles> || ...))
    explicit application_repository(Handle&& handle)
        : handle_(std::forward<Handle>(handle))
    {
    }

    auto get_by_id(orm::param_value id) -> task<orm::model_result<T>>
    {
        return dispatch([id = std::move(id)](auto& repository) mutable
            {
                return repository.get_by_id(std::move(id));
            });
    }

    auto get_one(const orm::query_wrapper<T>& query)
        -> task<orm::model_result<T>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.get_one(query);
            });
    }

    auto list(const orm::query_wrapper<T>& query = {})
        -> task<orm::model_result<T>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.list(query);
            });
    }

    template <typename Id>
    auto list_by_ids(std::span<const Id> ids) -> task<orm::model_result<T>>
    {
        return dispatch([ids](auto& repository)
            {
                return repository.list_by_ids(ids);
            });
    }

    auto list_by_map(
        std::span<const std::pair<std::string, orm::param_value>> values)
        -> task<orm::model_result<T>>
    {
        return dispatch([values](auto& repository)
            {
                return repository.list_by_map(values);
            });
    }

    auto exists(const orm::query_wrapper<T>& query)
        -> task<orm::model_result<bool>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.exists(query);
            });
    }

    auto count(const orm::query_wrapper<T>& query = {})
        -> task<orm::model_result<std::int64_t>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.count(query);
            });
    }

    auto page(std::size_t page_number, std::size_t page_size,
        const orm::query_wrapper<T>& query = {}) -> task<orm::page_result<T>>
    {
        return dispatch([page_number, page_size, &query](auto& repository)
            {
                return repository.page(page_number, page_size, query);
            });
    }

    auto select_maps(const orm::query_wrapper<T>& query = {})
        -> task<orm::model_result<orm::projection_row>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.select_maps(query);
            });
    }

    auto select_objects(const orm::query_wrapper<T>& query = {})
        -> task<orm::model_result<orm::field_value>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.select_objects(query);
            });
    }

    auto page_maps(std::size_t page_number, std::size_t page_size,
        const orm::query_wrapper<T>& query = {})
        -> task<orm::page_result<orm::projection_row>>
    {
        return dispatch([page_number, page_size, &query](auto& repository)
            {
                return repository.page_maps(page_number, page_size, query);
            });
    }

    auto select_xml(const orm::mapper_registry& registry,
        std::string_view statement_id, const orm::param_context& parameters)
        -> task<orm::model_result<T>>
    {
        return dispatch([&registry, statement_id,
                            &parameters](auto& repository)
            {
                return repository.select_xml(
                    registry, statement_id, parameters);
            });
    }

    auto get_one_xml(const orm::mapper_registry& registry,
        std::string_view statement_id, const orm::param_context& parameters,
        orm::single_result_policy policy =
            orm::single_result_policy::require_unique)
        -> task<orm::model_result<T>>
    {
        return dispatch([&registry, statement_id, &parameters,
                            policy](auto& repository)
            {
                return repository.get_one_xml(
                    registry, statement_id, parameters, policy);
            });
    }

    auto save(T& model) -> task<orm::model_result<T>>
    {
        return dispatch([&model](auto& repository)
            {
                return repository.save(model);
            });
    }

    auto update_by_id(T& model) -> task<orm::model_result<T>>
    {
        return dispatch([&model](auto& repository)
            {
                return repository.update_by_id(model);
            });
    }

    auto update(const orm::update_wrapper<T>& query)
        -> task<orm::model_result<T>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.update(query);
            });
    }

    auto update(const orm::update_wrapper<T>& query,
        orm::allow_full_table_t permission) -> task<orm::model_result<T>>
    {
        return dispatch([&query, permission](auto& repository)
            {
                return repository.update(query, permission);
            });
    }

    auto save_or_update(T& model) -> task<orm::model_result<T>>
    {
        return dispatch([&model](auto& repository)
            {
                return repository.save_or_update(model);
            });
    }

    auto upsert(T& model) -> task<orm::model_result<T>>
    {
        return dispatch([&model](auto& repository)
            {
                return repository.upsert(model);
            });
    }

    auto remove_by_id(orm::param_value id) -> task<orm::model_result<T>>
    {
        return dispatch([id = std::move(id)](auto& repository) mutable
            {
                return repository.remove_by_id(std::move(id));
            });
    }

    auto remove(const orm::query_wrapper<T>& query)
        -> task<orm::model_result<T>>
    {
        return dispatch([&query](auto& repository)
            {
                return repository.remove(query);
            });
    }

    auto remove(const orm::query_wrapper<T>& query,
        orm::allow_full_table_t permission) -> task<orm::model_result<T>>
    {
        return dispatch([&query, permission](auto& repository)
            {
                return repository.remove(query, permission);
            });
    }

    template <typename Id>
    auto remove_by_ids(std::span<const Id> ids) -> task<orm::model_result<T>>
    {
        return dispatch([ids](auto& repository)
            {
                return repository.remove_by_ids(ids);
            });
    }

    auto remove_by_map(
        std::span<const std::pair<std::string, orm::param_value>> values)
        -> task<orm::model_result<T>>
    {
        return dispatch([values](auto& repository)
            {
                return repository.remove_by_map(values);
            });
    }

    auto execute_xml(const orm::mapper_registry& registry,
        std::string_view statement_id, const orm::param_context& parameters)
        -> task<orm::model_result<T>>
    {
        return dispatch([&registry, statement_id,
                            &parameters](auto& repository)
            {
                return repository.execute_xml(
                    registry, statement_id, parameters);
            });
    }

    auto save_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<orm::model_result<T>>
    {
        return dispatch([models, batch_size](auto& repository)
            {
                return repository.save_batch(models, batch_size);
            });
    }

    auto update_batch_by_id(std::span<const T> models,
        std::size_t batch_size = 256) -> task<orm::model_result<T>>
    {
        return dispatch([models, batch_size](auto& repository)
            {
                return repository.update_batch_by_id(models, batch_size);
            });
    }

    auto save_or_update_batch(std::span<T> models,
        std::size_t batch_size = 256) -> task<orm::model_result<T>>
    {
        return dispatch([models, batch_size](auto& repository)
            {
                return repository.save_or_update_batch(models, batch_size);
            });
    }

    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<orm::model_result<T>>
    {
        return dispatch([models, batch_size](auto& repository)
            {
                return repository.upsert_batch(models, batch_size);
            });
    }

    auto for_each(const orm::query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        orm::stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        return dispatch([&query, handler = std::move(handler), options](
                            auto& repository) mutable
            {
                return repository.for_each(query, std::move(handler), options);
            });
    }

    auto for_each_map(const orm::query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const orm::projection_row&)>
            handler,
        orm::stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        return dispatch([&query, handler = std::move(handler), options](
                            auto& repository) mutable
            {
                return repository.for_each_map(
                    query, std::move(handler), options);
            });
    }

    auto for_each_map(const orm::query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const orm::projection_row&)>
            handler,
        orm::stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        return dispatch([&query, handler = std::move(handler), options,
                            &cancellation](auto& repository) mutable
            {
                return repository.for_each_map(query, std::move(handler),
                    options, cancellation);
            });
    }

    auto for_each(const orm::query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        orm::stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        return dispatch([&query, handler = std::move(handler), options,
                            &cancellation](auto& repository) mutable
            {
                return repository.for_each(query, std::move(handler), options,
                    cancellation);
            });
    }

    template <typename Result, class Operation>
    auto transaction(Operation operation)
        -> task<std::expected<Result, std::string>>
    {
        return dispatch([operation = std::move(operation)](
                            auto& repository) mutable
            {
                return repository.template transaction<Result>(
                    std::move(operation));
            });
    }

private:
    template <typename Operation>
    decltype(auto) dispatch(Operation&& operation)
    {
        return std::visit(
            [&operation](auto& handle) -> decltype(auto)
            {
                return std::forward<Operation>(operation)(handle.get());
            },
            handle_);
    }

    std::variant<Handles...> handle_;
};

/**
 * @brief Creates a repository handle around an already-bound gateway.
 */
export template <orm::Model T, typename Gateway,
    typename StreamStrategy = orm::session_stream_strategy>
[[nodiscard]] auto make_orm_repository_handle(
    std::shared_ptr<Gateway> gateway,
    orm::automatic_interceptor_options interceptors = {})
    -> std::expected<orm_repository_handle<T, Gateway, StreamStrategy>,
        std::error_code>
{
    if (!gateway)
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        return orm_repository_handle<T, Gateway, StreamStrategy>{
            std::move(gateway), interceptors};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
}

} // namespace cnetmod::application
