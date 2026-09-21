export module cnetmod.orm.session_gateway;

import std;
import cnetmod.coro.task;
import cnetmod.orm.automatic_interceptors;
import cnetmod.orm.database_session;
import cnetmod.orm.mapper;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.repository_contract;
import cnetmod.orm.sql_dialect;
import cnetmod.orm.xml_mapper;
import cnetmod.orm.xml_mapper_registry;

export namespace cnetmod::orm {

template <class Session>
class transaction_session_gateway;

/**
 * @brief Owns database leases and session lifetimes for repositories.
 */
template <asynchronous_database_client Client, class Lease,
    class Session = database_session<Client>>
class session_gateway
{
public:
    using session_type = Session;
    using start_operation = std::function<task<std::expected<void, std::string>>()>;
    using acquire_operation =
        std::function<task<std::expected<Lease, std::string>>()>;
    using client_accessor = std::function<Client&(Lease&)>;

    session_gateway(sql_dialect dialect, start_operation start,
        acquire_operation acquire, client_accessor client)
        : dialect_(dialect), start_(std::move(start)), acquire_(std::move(acquire)), client_(std::move(client))
    {
    }

    template <class T, class Operation>
    auto read(Operation&& operation) -> task<std::expected<T, std::string>>
    {
        auto lease = co_await acquire_lease();
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), dialect_};
        co_return co_await std::forward<Operation>(operation)(session);
    }

    template <class T, class Operation>
    auto write(Operation&& operation) -> task<std::expected<T, std::string>>
    {
        auto lease = co_await acquire_lease();
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), dialect_};
        co_return co_await session.template transaction<T>(
            [&]() -> task<std::expected<T, std::string>>
            {
                co_return co_await operation(session);
            });
    }

    /**
     * @brief Runs a stateful ORM cursor while retaining one pool lease.
     *
     * The lease scope encloses the callback, so a cursor cannot outlive the
     * physical connection that owns its result stream.
     */
    template <Model T, class Operation>
    auto cursor(Operation&& operation, query_wrapper<T> query = {},
        cursor_options options = {})
        -> task<std::expected<void, std::string>>
    {
        auto lease = co_await acquire_lease();
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), dialect_};
        cnetmod::orm::mapper<T, session_type> models{session};
        auto stream = models.open_cursor(std::move(query), options);
        try
        {
            co_return co_await std::forward<Operation>(operation)(stream);
        }
        catch (const std::exception& error)
        {
            co_return std::unexpected(error.what());
        }
        catch (...)
        {
            co_return std::unexpected("ORM cursor callback failed");
        }
    }

    /**
     * @brief Retains one lease while a caller configures and streams a session.
     *
     * Unlike `cursor()`, this overload lets an application facade install
     * model-specific interceptors before selecting a protocol cursor.
     */
    template <Model T, class Operation>
    auto stream(Operation&& operation)
        -> task<std::expected<void, std::string>>
    {
        auto lease = co_await acquire_lease();
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), dialect_};
        try
        {
            co_return co_await std::forward<Operation>(operation)(session);
        }
        catch (const std::exception& error)
        {
            co_return std::unexpected(error.what());
        }
        catch (...)
        {
            co_return std::unexpected("ORM stream callback failed");
        }
    }

    /**
     * @brief Runs a multi-model unit of work on one leased session.
     *
     * Every mapper obtained from the callback shares the same connection and
     * transaction. This is the only cross-model transaction entry point.
     */
    template <typename Result, class Operation>
    auto transaction(Operation&& operation)
        -> task<std::expected<Result, std::string>>
    {
        auto lease = co_await acquire_lease();
        if (!lease)
            co_return std::unexpected(lease.error());

        session_type session{client_(*lease), dialect_};
        transaction_session_gateway<session_type> unit{session};
        co_return co_await session.template transaction<Result>(
            [&]() -> task<std::expected<Result, std::string>>
            {
                co_return co_await std::forward<Operation>(operation)(unit);
            });
    }

private:
    auto acquire_lease() -> task<std::expected<Lease, std::string>>
    {
        auto started = co_await start_();
        if (!started)
            co_return std::unexpected(started.error());
        co_return co_await acquire_();
    }

    sql_dialect dialect_;
    start_operation start_;
    acquire_operation acquire_;
    client_accessor client_;
};

template <class Session>
class transaction_session_gateway
{
public:
    explicit transaction_session_gateway(Session& session) noexcept
        : session_(session)
    {
    }

    /**
     * @brief Creates a typed mapper sharing this unit of work's session.
     */
    template <Model T>
    auto mapper() noexcept -> cnetmod::orm::mapper<T, Session>
    {
        return cnetmod::orm::mapper<T, Session>{session_};
    }

    /**
     * @brief Creates a typed mapper and installs its model policies.
     *
     * A transaction can touch multiple model types. Call this overload at the
     * point of use so the shared session carries the policy chain belonging to
     * the mapper that executes the next statement.
     */
    template <Model T>
    auto mapper(automatic_interceptor_options options)
        -> cnetmod::orm::mapper<T, Session>
    {
        cnetmod::orm::mapper<T, Session> result{session_};
        auto configured = result.configure(options);
        if (!configured)
            throw std::runtime_error(configured.error());
        return result;
    }

    /**
     * @brief Creates an XML mapper on the same transaction session.
     */
    template <Model T>
    auto xml(const mapper_registry& registry,
        automatic_interceptor_options options)
        -> cnetmod::orm::xml_mapper<T, Session>
    {
        auto policies = mapper<T>(options);
        (void)policies;
        return cnetmod::orm::xml_mapper<T, Session>{session_, registry};
    }

private:
    Session& session_;
};

} // namespace cnetmod::orm
