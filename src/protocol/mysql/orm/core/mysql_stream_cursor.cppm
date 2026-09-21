export module cnetmod.protocol.mysql:orm_stream_cursor;

import std;
import :types;
import :connection_client;
import :orm_mysql_result_adapter;
import cnetmod.orm.database_session;
import cnetmod.orm.model_metadata;
import cnetmod.orm.query_wrapper;
import cnetmod.orm.result_mapper;
import cnetmod.orm.sql_parameters;
import cnetmod.coro.cancel;
import cnetmod.coro.task;

export namespace cnetmod::orm {

template <class Client>
concept mysql_stream_client = requires(Client& client,
    cnetmod::database::parameterized_query statement,
    cnetmod::mysql::execution_state& state,
    std::size_t max_rows) {
    client.start_execution(std::move(statement), state);
    client.read_some_rows(state, max_rows);
    client.read_resultset_head(state);
    client.close();
    { client.is_open() } -> std::convertible_to<bool>;
};

/**
 * @brief Consumes one MySQL result stream without OFFSET pagination.
 *
 * The cursor starts exactly one protocol execution and advances only when
 * `next()` is awaited. Destruction, cancellation, deadline expiry or a mapping
 * failure closes an unfinished connection so unread packets can never be
 * returned to a pool as reusable state.
 */
template <Model T, mysql_stream_client Client>
class mysql_stream_cursor
{
public:
    mysql_stream_cursor(Client& client,
        std::expected<parameterized_query, std::string> statement,
        cursor_options options = {})
        : client_(&client), options_(options)
    {
        options_.batch_size = std::max<std::size_t>(1, options_.batch_size);
        if (statement)
            statement_.emplace(std::move(*statement));
        else
            preparation_error_ = std::move(statement.error());
    }

    mysql_stream_cursor(const mysql_stream_cursor&) = delete;
    auto operator=(const mysql_stream_cursor&) -> mysql_stream_cursor& = delete;

    mysql_stream_cursor(mysql_stream_cursor&& other) noexcept
        : client_(std::exchange(other.client_, nullptr)),
          statement_(std::move(other.statement_)), state_(std::move(other.state_)),
          options_(other.options_), preparation_error_(std::move(other.preparation_error_)),
          delivered_(other.delivered_), started_(other.started_), done_(other.done_)
    {
        other.done_ = true;
    }

    auto operator=(mysql_stream_cursor&& other) noexcept
        -> mysql_stream_cursor&
    {
        if (this == &other)
            return *this;
        abandon_unfinished();
        client_ = std::exchange(other.client_, nullptr);
        statement_ = std::move(other.statement_);
        state_ = std::move(other.state_);
        options_ = other.options_;
        preparation_error_ = std::move(other.preparation_error_);
        delivered_ = other.delivered_;
        started_ = other.started_;
        done_ = other.done_;
        other.done_ = true;
        return *this;
    }

    ~mysql_stream_cursor()
    {
        abandon_unfinished();
    }

    /**
     * @brief Reads and maps the next protocol batch.
     */
    auto next() -> task<model_result<T>>
    {
        co_return co_await next_impl(nullptr);
    }

    /**
     * @brief Reads the next protocol batch with cooperative cancellation.
     */
    auto next(cancel_token& cancellation) -> task<model_result<T>>
    {
        co_return co_await next_impl(&cancellation);
    }

    [[nodiscard]] auto done() const noexcept -> bool
    {
        return done_;
    }

private:
    auto next_impl(cancel_token* cancellation) -> task<model_result<T>>
    {
        if (done_)
            co_return model_result<T>{};
        if (!preparation_error_.empty())
        {
            done_ = true;
            co_return failure(preparation_error_,
                std::make_error_code(std::errc::invalid_argument));
        }
        if (cancelled(cancellation))
        {
            abandon_unfinished();
            done_ = true;
            co_return failure("MySQL stream cursor cancelled",
                std::make_error_code(std::errc::operation_canceled));
        }
        if (deadline_expired())
        {
            abandon_unfinished();
            done_ = true;
            co_return failure("MySQL stream cursor deadline exceeded",
                std::make_error_code(std::errc::timed_out));
        }
        if (delivered_ >= options_.max_rows)
        {
            abandon_unfinished();
            done_ = true;
            co_return model_result<T>{};
        }
        if (!client_ || (!started_ && !statement_))
        {
            done_ = true;
            co_return failure("MySQL stream cursor has no execution state",
                std::make_error_code(std::errc::bad_file_descriptor));
        }

        try
        {
            if (!started_)
            {
                started_ = true;
                co_await client_->start_execution(std::move(*statement_), state_);
                statement_.reset();
            }
            while (state_.should_read_head())
                co_await client_->read_resultset_head(state_);
            if (!state_.error_msg().empty())
            {
                done_ = true;
                co_return protocol_failure();
            }
            if (state_.is_complete())
            {
                done_ = true;
                co_return model_result<T>{};
            }

            const auto remaining = options_.max_rows - delivered_;
            const auto limit = std::min(options_.batch_size, remaining);
            auto rows = co_await client_->read_some_rows(state_, limit);
            if (!state_.error_msg().empty())
            {
                done_ = true;
                co_return protocol_failure();
            }

            cnetmod::mysql::result_set protocol_result;
            protocol_result.columns = state_.columns();
            protocol_result.rows = std::move(rows);
            protocol_result.affected_rows = state_.affected_rows();
            protocol_result.last_insert_id = state_.last_insert_id();
            protocol_result.warning_count = state_.warning_count();
            protocol_result.info = state_.info();

            auto adapted = mysql_adapt_result(protocol_result);
            model_result<T> result;
            result.affected_rows = adapted.affected_rows;
            result.last_insert_id = adapted.last_insert_id;
            result.data = from_result_set<T>(adapted);
            delivered_ += result.data.size();

            if (state_.is_complete())
                done_ = true;
            else if (delivered_ >= options_.max_rows)
            {
                abandon_unfinished();
                done_ = true;
            }
            co_return result;
        }
        catch (const std::exception& error)
        {
            abandon_unfinished();
            done_ = true;
            co_return failure(error.what(),
                std::make_error_code(std::errc::io_error));
        }
        catch (...)
        {
            abandon_unfinished();
            done_ = true;
            co_return failure("MySQL stream cursor failed",
                std::make_error_code(std::errc::io_error));
        }
    }

    [[nodiscard]] auto protocol_failure() const -> model_result<T>
    {
        auto result = failure(std::string(state_.error_msg()));
        result.error_code = state_.error_code();
        return result;
    }

    [[nodiscard]] static auto failure(std::string message,
        std::error_code framework_error = {}) -> model_result<T>
    {
        model_result<T> result;
        result.error_msg = std::move(message);
        result.framework_error = framework_error;
        return result;
    }

    [[nodiscard]] auto cancelled(cancel_token* cancellation) const noexcept
        -> bool
    {
        return cancellation && cancellation->is_cancelled();
    }

    [[nodiscard]] auto deadline_expired() const noexcept -> bool
    {
        return options_.deadline &&
            std::chrono::steady_clock::now() >= *options_.deadline;
    }

    void abandon_unfinished() noexcept
    {
        if (client_ && started_ && !state_.is_complete() && client_->is_open())
            client_->close();
    }

    Client* client_{};
    std::optional<parameterized_query> statement_;
    cnetmod::mysql::execution_state state_;
    cursor_options options_;
    std::string preparation_error_;
    std::size_t delivered_{};
    bool started_{};
    bool done_{};
};

/**
 * @brief Opens a MySQL protocol cursor from an ORM session.
 */
template <Model T, typename Session>
auto open_mysql_stream_cursor(Session& session, query_wrapper<T> query = {},
    cursor_options options = {})
{
    using client_type = std::remove_reference_t<decltype(session.underlying())>;
    return mysql_stream_cursor<T, client_type>{session.underlying(),
        session.template prepare_select<T>(query), options};
}

/**
 * @brief Service streaming strategy backed by MySQL multi-function execution.
 */
struct mysql_stream_strategy
{
    template <Model T, typename Session, typename Handler>
    static auto for_each(Session& session, const query_wrapper<T>& query,
        Handler handler, stream_options options, cancel_token* cancellation)
        -> task<std::expected<void, std::string>>
    {
        auto cursor = open_mysql_stream_cursor<T>(session, query,
            cursor_options{.batch_size = options.batch_size,
                .max_rows = options.max_rows, .deadline = options.deadline});
        while (!cursor.done())
        {
            auto batch = cancellation ? co_await cursor.next(*cancellation)
                                      : co_await cursor.next();
            if (batch.is_err())
                co_return std::unexpected(batch.error_msg);
            for (const auto& model : batch.data)
            {
                auto handled = co_await handler(model);
                if (!handled)
                    co_return std::unexpected(handled.error());
            }
        }
        co_return std::expected<void, std::string>{};
    }
};

} // namespace cnetmod::orm
