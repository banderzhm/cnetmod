export module cnetmod.orm.mapper;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.orm.database_session;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.sql_parameters;

namespace cnetmod::orm {

/**
 * @brief Typed SQL mapper for one model and one database session.
 *
 * The mapper is deliberately thin: SQL generation, result mapping and native
 * diagnostics remain implemented by the session backend. The mapper provides
 * the stable BaseMapper-like typed surface consumed by repositories.
 */
export template <Model T, typename Session>
class mapper
{
public:
    explicit mapper(Session& session) noexcept
        : session_(&session)
    {
    }

    auto select_by_id(param_value id) -> task<model_result<T>>
    {
        return session_->template find_by_id<T>(std::move(id));
    }

    auto select_one(const query_wrapper<T>& query)
        -> task<model_result<T>>
    {
        return session_->template find_one<T>(query);
    }

    auto select_list(const query_wrapper<T>& query = {})
        -> task<model_result<T>>
    {
        return session_->template find<T>(query);
    }

    template <typename Id>
    auto select_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        return session_->template find_by_ids<T>(ids);
    }

    auto exists(const query_wrapper<T>& query) -> task<model_result<bool>>
    {
        return session_->template exists<T>(query);
    }

    auto select_page(std::size_t page, std::size_t page_size,
        const query_wrapper<T>& query = {}) -> task<page_result<T>>
    {
        return session_->template page<T>(page, page_size, query);
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

    auto select_maps_page(std::size_t page, std::size_t page_size,
        const query_wrapper<T>& query = {})
        -> task<page_result<projection_row>>
    {
        return session_->template page_maps<T>(page, page_size, query);
    }

    auto insert(T& model) -> task<model_result<T>>
    {
        return session_->insert(model);
    }

    auto update_by_id(const T& model) -> task<model_result<T>>
    {
        return session_->update(model);
    }

    auto update(const update_wrapper<T>& query) -> task<model_result<T>>
    {
        return session_->template update<T>(query);
    }

    auto save_or_update(T& model) -> task<model_result<T>>
    {
        return session_->template save_or_update<T>(model);
    }

    auto upsert(T& model) -> task<model_result<T>>
    {
        return session_->template upsert<T>(model);
    }

    auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        return session_->template remove<T>(query);
    }

    auto remove_by_id(param_value id) -> task<model_result<T>>
    {
        return session_->template remove_by_id<T>(std::move(id));
    }

    auto insert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return session_->template insert_batch<T>(models, batch_size);
    }

    auto update_batch(std::span<const T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return session_->template update_batch_by_id<T>(models, batch_size);
    }

    auto save_or_update_batch(std::span<T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        return session_->template save_or_update_batch<T>(models, batch_size);
    }

    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return session_->template upsert_batch<T>(models, batch_size);
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options = {}) -> task<std::expected<void, std::string>>
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
        return session_->template for_each_map<T>(query, std::move(handler),
            options);
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

private:
    Session* session_;
};

} // namespace cnetmod::orm
