export module cnetmod.orm.session_gateway;

import std;
import cnetmod.coro.task;
import cnetmod.orm.database_session;
import cnetmod.orm.sql_dialect;

export namespace cnetmod::orm {

template <asynchronous_database_client Client, class Lease,
    class Session = database_session<Client>>
class session_gateway
{
public:
    using session_type = Session;
    using start_operation = std::function<task<std::expected<void, std::string>>() >;
    using acquire_operation =
        std::function<task<std::expected<Lease, std::string>>() >;
    using client_accessor = std::function<Client&(Lease&)>;

    session_gateway(sql_dialect dialect, start_operation start,
        acquire_operation acquire, client_accessor client)
        : dialect_(dialect), start_(std::move(start)),
          acquire_(std::move(acquire)), client_(std::move(client))
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

    template <class T, class Operation>
    auto read(Operation&& operation) -> task<std::expected<T, std::string>>
    {
        co_return co_await std::forward<Operation>(operation)(session_);
    }

    template <class T, class Operation>
    auto write(Operation&& operation) -> task<std::expected<T, std::string>>
    {
        co_return co_await std::forward<Operation>(operation)(session_);
    }

private:
    Session& session_;
};

} // namespace cnetmod::orm
