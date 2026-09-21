export module cnetmod.orm.repository_impl;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.database_session;
import cnetmod.orm.mapper;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.repository_contract;
import cnetmod.orm.sql_parameters;
import cnetmod.orm.model_reflection;
import cnetmod.orm.xml_mapper;
import cnetmod.orm.xml_mapper_registry;

export namespace cnetmod::orm {

/**
 * @brief Default provider-neutral stream strategy.
 *
 * Protocol modules may replace this strategy with a wire cursor while keeping
 * the service API unchanged.
 */
struct session_stream_strategy
{
    template <Model T, typename Session, typename Handler>
    static auto for_each(Session& session, const query_wrapper<T>& query,
        Handler handler, stream_options options, cancel_token* cancellation)
        -> task<std::expected<void, std::string>>
    {
        mapper<T, Session> models{session};
        if (cancellation)
        {
            co_return co_await models.for_each(query, std::move(handler),
                options, *cancellation);
        }
        co_return co_await models.for_each(query, std::move(handler), options);
    }
};

/**
 * @brief Application-facing repository over a lease-owning session gateway.
 *
 * Every operation acquires its session through the gateway and installs the
 * standard model policies before executing. Callers therefore do not manage
 * pool leases, transactions or interceptor setup themselves.
 */
template <Model T, typename Gateway,
    typename StreamStrategy = session_stream_strategy>
class repository_impl
{
public:
    explicit repository_impl(Gateway& gateway,
        automatic_interceptor_options interceptors = {}) noexcept
        : gateway_(&gateway), interceptors_(interceptors)
    {
    }

    /**
     * @brief Executes a cross-model unit of work on one database transaction.
     *
     * The callback receives a transaction gateway and may obtain multiple
     * typed mappers from it. Connection leases and commit/rollback remain
     * owned by the gateway; business services never handle sessions directly.
     */
    template <typename Result, class Operation>
    auto transaction(Operation&& operation)
        -> task<std::expected<Result, std::string>>
    {
        co_return co_await gateway_->template transaction<Result>(
            [this, operation = std::forward<Operation>(operation)](
                auto& unit) mutable -> task<std::expected<Result, std::string>>
            {
                configured_transaction transaction{unit, interceptors_};
                co_return co_await operation(transaction);
            });
    }

    auto get_by_id(param_value id) -> task<model_result<T>>
    {
        co_return co_await read_model(
            [id = std::move(id)](auto& session) mutable
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .select_by_id(std::move(id));
            });
    }

    auto get_one(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        co_return co_await read_model([query](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .select_one(query);
            });
    }

    auto list(const query_wrapper<T>& query = {}) -> task<model_result<T>>
    {
        co_return co_await read_model([query](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .select_list(query);
            });
    }

    template <typename Id>
    auto list_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        co_return co_await read_model([ids](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .select_by_ids(ids);
            });
    }

    auto list_by_map(
        std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        co_return co_await read_model([values](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .select_by_map(values);
            });
    }

    auto exists(const query_wrapper<T>& query) -> task<model_result<bool>>
    {
        auto outcome = co_await gateway_->template read<model_result<bool>>(
            [this, query](auto& session)
                -> task<std::expected<model_result<bool>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                {
                    model_result<bool> result;
                    result.error_msg = configured.error();
                    result.framework_error = std::make_error_code(
                        std::errc::invalid_argument);
                    co_return result;
                }
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .exists(query);
            });
        if (outcome)
            co_return std::move(*outcome);
        model_result<bool> result;
        result.error_msg = outcome.error();
        result.framework_error = std::make_error_code(std::errc::io_error);
        co_return result;
    }

    auto count(const query_wrapper<T>& query = {})
        -> task<model_result<std::int64_t>>
    {
        auto outcome = co_await gateway_->template read<
            model_result<std::int64_t>>(
            [this, query](auto& session)
                -> task<std::expected<model_result<std::int64_t>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                {
                    model_result<std::int64_t> result;
                    result.error_msg = configured.error();
                    result.framework_error = std::make_error_code(
                        std::errc::invalid_argument);
                    co_return result;
                }
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .count(query);
            });
        if (outcome)
            co_return std::move(*outcome);
        model_result<std::int64_t> result;
        result.error_msg = outcome.error();
        result.framework_error = std::make_error_code(std::errc::io_error);
        co_return result;
    }

    auto page(std::size_t page_number, std::size_t page_size,
        const query_wrapper<T>& query = {}) -> task<page_result<T>>
    {
        auto outcome = co_await gateway_->template read<page_result<T>>(
            [this, page_number, page_size, query](auto& session)
                -> task<std::expected<page_result<T>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .select_page(page_number, page_size, query);
            });
        if (outcome)
            co_return std::move(*outcome);
        page_result<T> result;
        result.records = failure(outcome.error());
        co_return result;
    }

    auto select_maps(const query_wrapper<T>& query = {})
        -> task<model_result<projection_row>>
    {
        auto outcome = co_await gateway_->template read<
            model_result<projection_row>>(
            [this, query](auto& session)
                -> task<std::expected<model_result<projection_row>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                {
                    model_result<projection_row> result;
                    result.error_msg = configured.error();
                    result.framework_error = std::make_error_code(
                        std::errc::invalid_argument);
                    co_return result;
                }
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .select_maps(query);
            });
        if (outcome)
            co_return std::move(*outcome);
        co_return failure_projection(outcome.error());
    }

    auto select_objects(const query_wrapper<T>& query = {})
        -> task<model_result<field_value>>
    {
        auto outcome = co_await gateway_->template read<
            model_result<field_value>>(
            [this, query](auto& session)
                -> task<std::expected<model_result<field_value>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                {
                    model_result<field_value> result;
                    result.error_msg = configured.error();
                    result.framework_error = std::make_error_code(
                        std::errc::invalid_argument);
                    co_return result;
                }
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .select_objects(query);
            });
        if (outcome)
            co_return std::move(*outcome);
        model_result<field_value> result;
        result.error_msg = outcome.error();
        result.framework_error = std::make_error_code(std::errc::io_error);
        co_return result;
    }

    auto page_maps(std::size_t page_number, std::size_t page_size,
        const query_wrapper<T>& query = {})
        -> task<page_result<projection_row>>
    {
        auto outcome = co_await gateway_->template read<
            page_result<projection_row>>(
            [this, page_number, page_size, query](auto& session)
                -> task<std::expected<page_result<projection_row>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                {
                    page_result<projection_row> result;
                    result.records.error_msg = configured.error();
                    result.records.framework_error = std::make_error_code(
                        std::errc::invalid_argument);
                    co_return result;
                }
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .select_maps_page(page_number, page_size, query);
            });
        if (outcome)
            co_return std::move(*outcome);
        page_result<projection_row> result;
        result.records.error_msg = outcome.error();
        result.records.framework_error = std::make_error_code(std::errc::io_error);
        co_return result;
    }

    /**
     * @brief Executes a typed select defined by a MyBatis-style XML mapper.
     */
    auto select_xml(const mapper_registry& registry,
        std::string_view statement_id, const param_context& parameters)
        -> task<model_result<T>>
    {
        co_return co_await read_model(
            [&registry, statement_id = std::string{statement_id},
                parameters](auto& session) mutable -> task<model_result<T>>
            {
                xml_mapper<T, std::remove_reference_t<decltype(session)>>
                    statements{session, registry};
                co_return co_await statements.select(statement_id, parameters);
            });
    }

    /**
     * @brief Executes a strict single-row XML select.
     */
    auto get_one_xml(const mapper_registry& registry,
        std::string_view statement_id, const param_context& parameters,
        single_result_policy policy = single_result_policy::require_unique)
        -> task<model_result<T>>
    {
        co_return co_await read_model(
            [&registry, statement_id = std::string{statement_id}, parameters,
                policy](auto& session) mutable -> task<model_result<T>>
            {
                xml_mapper<T, std::remove_reference_t<decltype(session)>>
                    statements{session, registry};
                co_return co_await statements.select_one(
                    statement_id, parameters, policy);
            });
    }

    auto save(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .insert(model);
            });
    }

    auto update_by_id(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .update_by_id(model);
            });
    }

    auto update(const update_wrapper<T>& query) -> task<model_result<T>>
    {
        co_return co_await write_model([query](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .update(query);
            });
    }

    auto update(const update_wrapper<T>& query, allow_full_table_t permission)
        -> task<model_result<T>>
    {
        co_return co_await write_model([query, permission](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .update(query, permission);
            });
    }

    auto save_or_update(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .save_or_update(model);
            });
    }

    auto upsert(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .upsert(model);
            });
    }

    auto remove_by_id(param_value id) -> task<model_result<T>>
    {
        co_return co_await write_model(
            [id = std::move(id)](auto& session) mutable
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .remove_by_id(std::move(id));
            });
    }

    auto remove(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        co_return co_await write_model([query](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .remove(query);
            });
    }

    auto remove(const query_wrapper<T>& query, allow_full_table_t permission)
        -> task<model_result<T>>
    {
        co_return co_await write_model([query, permission](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .remove(query, permission);
            });
    }

    template <typename Id>
    auto remove_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        co_return co_await write_model([ids](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .remove_by_ids(ids);
            });
    }

    auto remove_by_map(
        std::span<const std::pair<std::string, param_value>> values)
        -> task<model_result<T>>
    {
        co_return co_await write_model([values](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .remove_by_map(values);
            });
    }

    /**
     * @brief Executes an XML insert, update or delete in a managed transaction.
     */
    auto execute_xml(const mapper_registry& registry,
        std::string_view statement_id, const param_context& parameters)
        -> task<model_result<T>>
    {
        co_return co_await write_model(
            [&registry, statement_id = std::string{statement_id},
                parameters](auto& session) mutable -> task<model_result<T>>
            {
                xml_mapper<T, std::remove_reference_t<decltype(session)>>
                    statements{session, registry};
                co_return co_await statements.execute(statement_id, parameters);
            });
    }

    auto save_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .insert_batch(models, batch_size);
            });
    }

    auto update_batch_by_id(std::span<const T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .update_batch(models, batch_size);
            });
    }

    auto save_or_update_batch(std::span<T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .save_or_update_batch(models, batch_size);
            });
    }

    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}
                    .upsert_batch(models, batch_size);
            });
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        co_return co_await gateway_->template stream<T>(
            [this, query, handler = std::move(handler), options](auto& session) mutable
                -> task<std::expected<void, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await StreamStrategy::template for_each<T>(session,
                    query, std::move(handler), options, nullptr);
            });
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)>
            handler,
        stream_options options = {})
        -> task<std::expected<void, std::string>>
    {
        co_return co_await gateway_->template stream<T>(
            [this, query, handler = std::move(handler), options](auto& session) mutable
                -> task<std::expected<void, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .for_each_map(query, std::move(handler), options);
            });
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)>
            handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        co_return co_await gateway_->template stream<T>(
            [this, query, handler = std::move(handler), options,
                &cancellation](auto& session) mutable
                -> task<std::expected<void, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await mapper<T,
                    std::remove_reference_t<decltype(session)>>{session}
                    .for_each_map(query, std::move(handler), options,
                        cancellation);
            });
    }

    auto for_each(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(const T&)> handler,
        stream_options options, cancel_token& cancellation)
        -> task<std::expected<void, std::string>>
    {
        co_return co_await gateway_->template stream<T>(
            [this, query, handler = std::move(handler), options,
                &cancellation](auto& session) mutable
                -> task<std::expected<void, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await StreamStrategy::template for_each<T>(session,
                    query, std::move(handler), options, &cancellation);
            });
    }

private:
    template <typename Unit>
    class configured_transaction
    {
    public:
        configured_transaction(Unit& unit,
            automatic_interceptor_options interceptors) noexcept
            : unit_(&unit), interceptors_(interceptors)
        {
        }

        /**
         * @brief Creates a mapper with the repository policy chain installed.
         *
         * Policy construction failures propagate through the transaction's
         * existing exception boundary. Obtain the mapper immediately before
         * its operation when a transaction uses more than one model type.
         */
        template <Model U>
        auto mapper()
            -> decltype(std::declval<Unit&>().template mapper<U>())
        {
            return unit_->template mapper<U>(interceptors_);
        }

        /**
         * @brief Creates an XML mapper sharing this transaction and policies.
         */
        template <Model U>
        auto xml(const mapper_registry& registry)
            -> decltype(std::declval<Unit&>().template xml<U>(registry,
                std::declval<automatic_interceptor_options>()))
        {
            return unit_->template xml<U>(registry, interceptors_);
        }

    private:
        Unit* unit_;
        automatic_interceptor_options interceptors_;
    };

    template <typename Session>
    auto configure(Session& session) const -> std::expected<void, std::string>
    {
        mapper<T, Session> models{session};
        return models.configure(interceptors_);
    }

    template <typename Operation>
    auto read_model(Operation operation) -> task<model_result<T>>
    {
        auto outcome = co_await gateway_->template read<model_result<T>>(
            [this, operation = std::move(operation)](auto& session) mutable
                -> task<std::expected<model_result<T>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await operation(session);
            });
        co_return outcome ? std::move(*outcome) : failure(outcome.error());
    }

    template <typename Operation>
    auto write_model(Operation operation) -> task<model_result<T>>
    {
        // The gateway owns the lease.  The repository owns the model-level
        // transaction so the complete model_result, including native SQLSTATE
        // and vendor error number, can survive rollback unchanged.
        auto outcome = co_await gateway_->template read<model_result<T>>(
            [this, operation = std::move(operation)](auto& session) mutable
                -> task<std::expected<model_result<T>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                auto started = co_await session.begin_transaction();
                if (!started)
                    co_return failure(started.error());
                auto result = co_await operation(session);
                if (result.is_err())
                {
                    auto rolled_back = co_await session.rollback_transaction();
                    if (!rolled_back)
                    {
                        result.error_msg += "; rollback failed: ";
                        result.error_msg += rolled_back.error();
                    }
                    co_return result;
                }
                auto committed = co_await session.commit_transaction();
                if (!committed)
                    co_return failure(committed.error());
                co_return result;
            });
        co_return outcome ? std::move(*outcome) : failure(outcome.error());
    }

    template <typename Operation>
    auto owned_transaction_model(Operation operation) -> task<model_result<T>>
    {
        auto outcome = co_await gateway_->template read<model_result<T>>(
            [this, operation = std::move(operation)](auto& session) mutable
                -> task<std::expected<model_result<T>, std::string>>
            {
                auto configured = this->configure(session);
                if (!configured)
                    co_return std::unexpected(configured.error());
                co_return co_await operation(session);
            });
        co_return outcome ? std::move(*outcome) : failure(outcome.error());
    }

    [[nodiscard]] static auto failure(std::string message) -> model_result<T>
    {
        model_result<T> result;
        result.error_msg = std::move(message);
        result.framework_error = std::make_error_code(std::errc::io_error);
        return result;
    }

    [[nodiscard]] static auto failure_projection(std::string message)
        -> model_result<projection_row>
    {
        model_result<projection_row> result;
        result.error_msg = std::move(message);
        result.framework_error = std::make_error_code(std::errc::io_error);
        return result;
    }

    Gateway* gateway_;
    automatic_interceptor_options interceptors_;
};

} // namespace cnetmod::orm
