export module cnetmod.orm.repository;

import std;
import cnetmod.orm.database_session;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.model_metadata;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.repository_impl;
import cnetmod.coro.task;
import cnetmod.coro.cancel;

namespace cnetmod::orm {

/**
 * @brief Low-level migration facade for code that already owns a session.
 *
 * This type is intentionally not an application entry point. New application
 * code uses the gateway-backed `repository<T, Gateway>`, which acquires a
 * lease, applies interceptors and owns the transaction boundary. Keep this
 * facade only for protocol adapters and incremental migrations.
 */
export template <Model T, typename Session>
class session_repository
{
public:
    using session_type = Session;

    explicit session_repository(session_type& session) noexcept
        : session_(&session)
    {
    }

    auto get_by_id(param_value id) -> task<model_result<T>>
    {
        return session_->template find_by_id<T>(std::move(id));
    }

    auto get_one(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        return session_->template find_one<T>(query);
    }

    auto list(const query_wrapper<T>& query = {}) -> task<model_result<T>>
    {
        return session_->template find<T>(query);
    }

    auto exists(const query_wrapper<T>& query) -> task<model_result<bool>>
    {
        return session_->template exists<T>(query);
    }

    template <typename Id>
    auto list_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        return session_->template find_by_ids<T>(ids);
    }

    auto page(std::size_t page_number, std::size_t page_size,
        const query_wrapper<T>& query = {}) -> task<page_result<T>>
    {
        return session_->template page<T>(page_number, page_size, query);
    }

    auto select_maps(const query_wrapper<T>& query = {})
        -> task<model_result<projection_row>>
    {
        return session_->template select_maps<T>(query);
    }

    auto select_objects(const query_wrapper<T>& query = {})
        -> task<model_result<field_value>>
    {
        return session_->template select_objects<T>(query);
    }

    auto page_maps(std::size_t page_number, std::size_t page_size,
        const query_wrapper<T>& query = {})
        -> task<page_result<projection_row>>
    {
        return session_->template page_maps<T>(page_number, page_size, query);
    }

    /**
     * @brief Opens a stateful cursor owned by the underlying session.
     */
    auto open_cursor(query_wrapper<T> query = {}, cursor_options options = {})
        -> typename session_type::template cursor<T>
    {
        return session_->template open_cursor<T>(std::move(query), options);
    }

    /**
     * @brief Runs a repository operation in one session transaction.
     */
    template <typename Result, typename Operation>
    auto transaction(Operation operation)
        -> task<std::expected<Result, std::string>>
    {
        co_return co_await session_->template transaction<Result>(
            [operation = std::move(operation), this]() mutable
                -> task<std::expected<Result, std::string>>
            {
                co_return co_await operation(*this);
            });
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        return session_->template for_each<T>(query, std::move(handler), options);
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        return session_->template for_each<T>(query, std::move(handler), options,
            cancellation);
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)> handler,
        stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        return session_->template for_each_map<T>(
            query, std::move(handler), options);
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)> handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        return session_->template for_each_map<T>(query, std::move(handler),
            options, cancellation);
    }

    auto save(T& model) -> task<model_result<T>>
    {
        return session_->insert(model);
    }

    /**
     * @brief Inserts models in bounded transactional batches.
     */
    auto save_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return session_->template insert_batch<T>(models, batch_size);
    }

    auto save_or_update(T& model) -> task<model_result<T>>
    {
        return session_->template save_or_update<T>(model);
    }

    auto upsert(T& model) -> task<model_result<T>>
    {
        return session_->template upsert<T>(model);
    }

    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return session_->template upsert_batch<T>(models, batch_size);
    }

    /**
     * @brief Saves or updates models in bounded transactional batches.
     */
    auto save_or_update_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return session_->template save_or_update_batch<T>(models, batch_size);
    }

    auto update_by_id(const T& model) -> task<model_result<T>>
    {
        return session_->update(model);
    }

    auto update_by_wrapper(const update_wrapper<T>& wrapper)
        -> task<model_result<T>>
    {
        return session_->template update<T>(wrapper);
    }

    auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        return session_->template remove<T>(query);
    }

    auto remove_by_id(param_value id) -> task<model_result<T>>
    {
        return session_->template remove_by_id<T>(std::move(id));
    }

private:
    static auto failure(std::string message) -> model_result<T>
    {
        model_result<T> result;
        result.error_msg = std::move(message);
        return result;
    }

    session_type* session_;
};

/**
 * @brief Gateway-backed persistence facade for application code.
 *
 * This is the cnetmod equivalent of MyBatis-Plus IService/ServiceImpl. It
 * owns no SQL and delegates model work through the session gateway.
 */
export template <Model T, typename Gateway,
    typename StreamStrategy = session_stream_strategy>
using repository = repository_impl<T, Gateway, StreamStrategy>;

} // namespace cnetmod::orm
