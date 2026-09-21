export module cnetmod.orm.repository_impl;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.database_session;
import cnetmod.orm.mapper;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.sql_parameters;

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
        if (cancellation)
        {
            co_return co_await session.template for_each<T>(query,
                std::move(handler), options, *cancellation);
        }
        co_return co_await session.template for_each<T>(query,
            std::move(handler), options);
    }
};

/**
 * @brief Application-facing ORM service over a lease-owning session gateway.
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

    auto get_by_id(param_value id) -> task<model_result<T>>
    {
        co_return co_await read_model(
            [id = std::move(id)](auto& session) mutable
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.select_by_id(std::move(id));
            });
    }

    auto get_one(const query_wrapper<T>& query) -> task<model_result<T>>
    {
        co_return co_await read_model([query](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.select_one(query);
            });
    }

    auto list(const query_wrapper<T>& query = {}) -> task<model_result<T>>
    {
        co_return co_await read_model([query](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.select_list(query);
        });
    }

    template <typename Id>
    auto list_by_ids(std::span<const Id> ids) -> task<model_result<T>>
    {
        co_return co_await read_model([ids](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.select_by_ids(ids);
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    exists(query);
            });
        if (outcome)
            co_return std::move(*outcome);
        model_result<bool> result;
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    select_page(page_number, page_size, query);
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    select_maps(query);
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    select_objects(query);
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    select_maps_page(page_number, page_size, query);
            });
        if (outcome)
            co_return std::move(*outcome);
        page_result<projection_row> result;
        result.records.error_msg = outcome.error();
        result.records.framework_error = std::make_error_code(std::errc::io_error);
        co_return result;
    }

    auto save(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.insert(model);
            });
    }

    auto update_by_id(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.update_by_id(model);
            });
    }

    auto save_or_update(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.save_or_update(model);
            });
    }

    auto upsert(T& model) -> task<model_result<T>>
    {
        co_return co_await write_model([&model](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.upsert(model);
            });
    }

    auto remove_by_id(param_value id) -> task<model_result<T>>
    {
        co_return co_await write_model(
            [id = std::move(id)](auto& session) mutable
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.remove_by_id(std::move(id));
            });
    }

    auto save_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.insert_batch(models, batch_size);
            });
    }

    auto update_batch_by_id(std::span<const T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.update_batch(models, batch_size);
            });
    }

    auto save_or_update_batch(std::span<T> models,
        std::size_t batch_size = 256) -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.save_or_update_batch(models, batch_size);
            });
    }

    auto upsert_batch(std::span<T> models, std::size_t batch_size = 256)
        -> task<model_result<T>>
    {
        co_return co_await owned_transaction_model(
            [models, batch_size](auto& session)
            {
                return mapper<T, std::remove_reference_t<decltype(session)>>{
                    session}.upsert_batch(models, batch_size);
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
            const projection_row&)> handler,
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    for_each_map(query, std::move(handler), options);
            });
    }

    auto for_each_map(const query_wrapper<T>& query,
        std::function<task<std::expected<void, std::string>>(
            const projection_row&)> handler,
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
                    std::remove_reference_t<decltype(session)>>{session}.
                    for_each_map(query, std::move(handler), options,
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
    template <typename Session>
    auto configure(Session& session) const -> std::expected<void, std::string>
    {
        return session.template enable_automatic_interceptors<T>(interceptors_);
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
