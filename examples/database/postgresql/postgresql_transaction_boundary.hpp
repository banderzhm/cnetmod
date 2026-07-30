#pragma once

namespace postgresql_example {

class transaction_boundary
{
public:
    transaction_boundary(cnetmod::io_context& context,
        failover_connection_manager& connections, const service_config& config)
        : context_(context), connections_(connections), config_(config) {}

    template <class Operation>
    auto execute(Operation operation) -> cnetmod::task<cnetmod::postgresql::result_set>
    {
        co_return co_await execute_typed<cnetmod::postgresql::result_set>(
            std::move(operation));
    }

    template <cnetmod::orm::Model Model, class Operation>
    auto execute_model(Operation operation)
        -> cnetmod::task<cnetmod::orm::postgresql_orm_result<Model>>
    {
        co_return co_await execute_typed<
            cnetmod::orm::postgresql_orm_result<Model>>(std::move(operation));
    }

private:
    class rollback_requested final : public std::exception
    {
    public:
        [[nodiscard]] auto what() const noexcept -> const char* override
        {
            return "ORM operation requested transaction rollback";
        }
    };

    template <class Result, class Operation>
    auto execute_typed(Operation operation) -> cnetmod::task<Result>
    {
        Result last_failure;
        const auto maximum_attempts = std::max<std::size_t>(1,
            config_.failover_attempts * config_.endpoints.size());
        for (std::size_t attempt = 0; attempt < maximum_attempts; ++attempt)
        {
            auto acquired = co_await connections_.acquire();
            if (!acquired)
            {
                last_failure.error_msg = acquired.error().message();
                co_await backoff(attempt);
                continue;
            }

            Result result;
            cnetmod::orm::postgresql_session session(acquired->connection.get());
            try
            {
                auto transaction_result = co_await session.transaction(
                    [&]() -> cnetmod::task<void>
                    {
                        result = co_await operation(session);
                        if (result.is_err())
                            throw rollback_requested{};
                    });
                if (result.ok() && transaction_result.is_err())
                    result = from_wire_failure<Result>(std::move(transaction_result));
            }
            catch (const std::exception& error)
            {
                result.error_msg = std::format("transaction operation failed: {}", error.what());
            }
            if (result.is_err())
            {
                const bool retryable = is_retryable(result);
                if (is_connection_failure(result))
                    connections_.report_connection_failure(*acquired);
                else if (result.sql_state == "25006")
                    connections_.advance_endpoint(acquired->endpoint_index);
                last_failure = std::move(result);
                if (!retryable)
                    co_return last_failure;
                co_await backoff(attempt);
                continue;
            }

            co_return result;
        }
        if (last_failure.error_msg.empty())
            last_failure.error_msg = "PostgreSQL transaction retry budget exhausted";
        co_return last_failure;
    }

    template <class Result>
    static auto from_wire_failure(cnetmod::postgresql::result_set failure) -> Result
    {
        if constexpr (std::same_as<Result, cnetmod::postgresql::result_set>)
            return failure;
        else
        {
            Result result;
            result.error_msg = std::move(failure.error_msg);
            result.sql_state = std::move(failure.sql_state);
            result.affected_rows = failure.affected_rows;
            return result;
        }
    }

    template <class Result>
    [[nodiscard]] static auto is_connection_failure(
        const Result& result) noexcept -> bool
    {
        return result.sql_state.empty() || result.sql_state.starts_with("08");
    }

    template <class Result>
    [[nodiscard]] static auto is_retryable(
        const Result& result) noexcept -> bool
    {
        return is_connection_failure(result) || result.sql_state == "25006" ||
            result.sql_state == "40001" || result.sql_state == "40P01";
    }

    auto backoff(std::size_t attempt) -> cnetmod::task<void>
    {
        const auto multiplier = std::size_t{1} << std::min<std::size_t>(attempt, 6);
        co_await cnetmod::async_sleep(context_, config_.retry_backoff * multiplier);
    }

    cnetmod::io_context& context_;
    failover_connection_manager& connections_;
    const service_config& config_;
};

} // namespace postgresql_example
