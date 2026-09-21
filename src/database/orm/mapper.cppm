export module cnetmod.orm.mapper;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.database_session;
import cnetmod.orm.mapper_operations;
import cnetmod.orm.model_metadata;
import cnetmod.orm.model_reflection;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.repository_contract;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.xml_mapper;
import cnetmod.orm.xml_mapper_registry;

namespace cnetmod::orm {

/**
 * @brief Typed SQL mapper for one model and one database session.
 *
 * This is the only public model-aware persistence surface. The session stays
 * responsible for raw execution and transaction state, while mapper owns the
 * stable BaseMapper-like contract consumed by repositories.
 */
export template <Model T, typename Session>
class mapper
{
public:
    explicit mapper(Session& session) noexcept
        : session_(&session)
    {
    }

    /**
     * @brief Installs model-aware automatic persistence policies.
     */
    auto configure(automatic_interceptor_options options = {})
        -> std::expected<void, std::string>
    {
        return detail::mapper_session_access::template configure<T>(
            *session_, options);
    }

    /**
     * @brief Builds the intercepted statement used by protocol cursors.
     */
    auto prepare_select(const query_wrapper<T>& query) const
    {
        return detail::mapper_session_access::template prepare_select<T>(
            *session_, query);
    }

    /**
     * @brief Opens a bounded cursor whose lifetime is tied to this mapper's session.
     */
    auto open_cursor(query_wrapper<T> query = {}, cursor_options options = {})
    {
        return detail::mapper_session_access::template open_cursor<T>(
            *session_, std::move(query), options);
    }

    auto select_by_id(param_value id) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template select_by_id<T>(
            *session_, std::move(id));
    }

    auto select_one(const query_wrapper<T>& query)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template select_one<T>(*session_, query);
    }

    auto select_first(const query_wrapper<T>& query)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template select_first<T>(
            *session_, query);
    }

    auto select_list(const query_wrapper<T>& query = {})
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template select_list<T>(*session_, query);
    }

    template <typename Id>
    auto select_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template select_by_ids<T>(*session_, ids);
    }

    auto select_by_map(
        std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template select_by_map<T>(
            *session_, values);
    }

    auto exists(const query_wrapper<T>& query) -> task<model_result<bool>>
    {
        return detail::mapper_session_access::template exists<T>(*session_, query);
    }

    auto count(const query_wrapper<T>& query = {})
        -> task<model_result<std::int64_t>>
    {
        return detail::mapper_session_access::template count<T>(*session_, query);
    }

    auto select_page(std::size_t page, std::size_t page_size,
        const query_wrapper<T>& query = {}) -> task<page_result<T>>
    {
        return detail::mapper_session_access::template select_page<T>(
            *session_, page, page_size, query);
    }

    auto select_maps(const query_wrapper<T>& query = {})
        -> task<model_result<projection_row>>
    {
        return detail::mapper_session_access::template select_maps<T>(*session_, query);
    }

    auto select_objects(const query_wrapper<T>& query = {})
        -> task<model_result<field_value>>
    {
        return detail::mapper_session_access::template select_objects<T>(
            *session_, query);
    }

    auto select_maps_page(std::size_t page, std::size_t page_size,
        const query_wrapper<T>& query = {})
        -> task<page_result<projection_row>>
    {
        return detail::mapper_session_access::template select_maps_page<T>(
            *session_, page, page_size, query);
    }

    /**
     * @brief Executes a model select declared in a MyBatis-style XML mapper.
     *
     * XML statements are an extension of this mapper rather than a parallel
     * persistence facade. They therefore share the same session, policies,
     * transaction and result mapping as the built-in CRUD operations.
     */
    auto select_xml(const mapper_registry& registry,
        std::string_view statement_id, const param_context& parameters)
        -> task<model_result<T>>
    {
        return xml_mapper<T, Session>{*session_, registry}.select(
            statement_id, parameters);
    }

    /**
     * @brief Executes a cardinality-checked XML select for this model.
     */
    auto select_one_xml(const mapper_registry& registry,
        std::string_view statement_id, const param_context& parameters,
        single_result_policy policy = single_result_policy::require_unique)
        -> task<model_result<T>>
    {
        return xml_mapper<T, Session>{*session_, registry}.select_one(
            statement_id, parameters, policy);
    }

    /**
     * @brief Executes an XML insert, update or delete on this mapper's session.
     */
    auto execute_xml(const mapper_registry& registry,
        std::string_view statement_id, const param_context& parameters)
        -> task<model_result<T>>
    {
        return xml_mapper<T, Session>{*session_, registry}.execute(
            statement_id, parameters);
    }

    auto insert(T& model) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template insert<T>(*session_, model);
    }

    auto update_by_id(T& model) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template update_by_id<T>(*session_, model);
    }

    auto update_by_id(const T& model) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template update_by_id<T>(*session_, model);
    }

    auto update(const update_wrapper<T>& query) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template update<T>(*session_, query);
    }

    auto update(const update_wrapper<T>& query, allow_full_table_t permission)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template update<T>(
            *session_, query, permission);
    }

    auto save_or_update(T& model) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template save_or_update<T>(
            *session_, model);
    }

    auto upsert(T& model) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template upsert<T>(*session_, model);
    }

    auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template remove<T>(*session_, query);
    }

    auto remove(const query_wrapper<T>& query, allow_full_table_t permission)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template remove<T>(
            *session_, query, permission);
    }

    auto remove_by_id(param_value id) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template remove_by_id<T>(
            *session_, std::move(id));
    }

    template <typename Id>
    auto remove_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template remove_by_ids<T>(
            *session_, ids);
    }

    auto remove_by_map(
        std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template remove_by_map<T>(
            *session_, values);
    }

    auto insert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template insert_batch<T>(
            *session_, models, batch_size);
    }

    auto update_batch(std::span<const T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template update_batch<T>(
            *session_, models, batch_size);
    }

    auto save_or_update_batch(std::span<T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        return detail::mapper_session_access::template save_or_update_batch<T>(
            *session_, models, batch_size);
    }

    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        return detail::mapper_session_access::template upsert_batch<T>(
            *session_, models, batch_size);
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options = {}) -> task<std::expected<void, std::string>>
    {
        return detail::mapper_session_access::template for_each<T>(
            *session_, query, std::move(handler), options);
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        return detail::mapper_session_access::template for_each<T>(
            *session_, query, std::move(handler), options, cancellation);
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)>
            handler,
        stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        return detail::mapper_session_access::template for_each_map<T>(
            *session_, query, std::move(handler), options);
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)>
            handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        return detail::mapper_session_access::template for_each_map<T>(
            *session_, query, std::move(handler), options, cancellation);
    }

private:
    Session* session_;
};

} // namespace cnetmod::orm
