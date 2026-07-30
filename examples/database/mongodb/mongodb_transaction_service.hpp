#pragma once

namespace mongodb_example {

class transaction_service
{
public:
    transaction_service(cnetmod::io_context& context, const service_config& config,
        cnetmod::mongodb::topology_connection_pool& topology_pool)
        : context_(context), config_(config), topology_pool_(topology_pool) {}

    auto transfer_event(std::string_view run_id, std::int64_t amount)
        -> cnetmod::task<bool>
    {
        namespace mongo = cnetmod::mongodb;
        constexpr std::size_t maximum_attempts = 5;
        for (std::size_t attempt = 1; attempt <= maximum_attempts; ++attempt)
        {
            auto refreshed = co_await topology_pool_.refresh();
            if (!refreshed)
            {
                logger::warn("MongoDB transaction topology refresh failed: attempt={}, error={}", attempt, refreshed.error().message);
                continue;
            }
            auto primary = topology_pool_.topology().select_server();
            if (!primary)
            {
                logger::warn("MongoDB transaction primary selection failed: attempt={}, error={}", attempt, primary.error().message);
                continue;
            }
            mongo::connection_pool_options options;
            options.connection = config_.connection_options_for(primary->address);
            options.minimum_size = 1;
            options.maximum_size = 2;
            options.maximum_connecting = 1;
            options.wait_queue_timeout = config_.pool_wait_timeout;
            mongo::connection_pool transaction_pool(context_, std::move(options));
            mongo::client_session session;
            auto started = session.start_transaction();
            if (!started)
            {
                logger::error("MongoDB transaction start failed: {}", started.error().message);
                co_return false;
            }
            mongo::bson_document debit{{"update", "cnetmod_transaction_accounts"},
                {"updates", mongo::bson_array{mongo::bson_document{{"q", mongo::bson_document{{"run_id", run_id}, {"account", "source"}}}, {"u", mongo::bson_document{{"$inc", mongo::bson_document{{"balance", -amount}}}}}, {"upsert", true}}}}};
            auto debit_result = co_await session.command(transaction_pool, config_.database, std::move(debit));
            mongo::result<mongo::bson_document> credit_result = std::unexpected(
                mongo::make_error(mongo::error_code::transaction_failed, "debit failed"));
            if (debit_result)
            {
                mongo::bson_document credit{{"update", "cnetmod_transaction_accounts"},
                    {"updates", mongo::bson_array{mongo::bson_document{{"q", mongo::bson_document{{"run_id", run_id}, {"account", "destination"}}}, {"u", mongo::bson_document{{"$inc", mongo::bson_document{{"balance", amount}}}}}, {"upsert", true}}}}};
                credit_result = co_await session.command(transaction_pool, config_.database, std::move(credit));
            }
            if (debit_result && credit_result)
            {
                auto committed = co_await session.commit_transaction(transaction_pool);
                transaction_pool.close();
                if (committed)
                {
                    logger::info("MongoDB transaction committed: primary={}, attempt={}", primary->address.host, attempt);
                    co_return true;
                }
                if (!retryable(committed.error()))
                {
                    logger::error("MongoDB transaction commit failed: error={}", committed.error().message);
                    co_return false;
                }
            }
            else
            {
                const auto& failure = debit_result ? credit_result.error() : debit_result.error();
                auto ignored = co_await session.abort_transaction(transaction_pool);
                (void)ignored;
                transaction_pool.close();
                if (!retryable(failure))
                {
                    logger::error("MongoDB transaction command failed: error={}", failure.message);
                    co_return false;
                }
            }
            logger::warn("MongoDB transient transaction retry: primary={}, attempt={}", primary->address.host, attempt);
            co_await cnetmod::async_sleep(context_, std::chrono::milliseconds{25 * attempt});
        }
        co_return false;
    }

    auto verify_abort_isolation(std::string_view run_id) -> cnetmod::task<bool>
    {
        namespace mongo = cnetmod::mongodb;
        auto refreshed = co_await topology_pool_.refresh();
        if (!refreshed)
            co_return false;
        auto primary = topology_pool_.topology().select_server();
        if (!primary)
            co_return false;
        mongo::connection_pool_options options;
        options.connection = config_.connection_options_for(primary->address);
        options.minimum_size = 1;
        options.maximum_size = 1;
        options.wait_queue_timeout = config_.pool_wait_timeout;
        mongo::connection_pool transaction_pool(context_, std::move(options));
        mongo::client_session session;
        if (!session.start_transaction())
            co_return false;
        auto inserted = co_await session.command(transaction_pool, config_.database,
            mongo::bson_document{{"insert", "cnetmod_aborted_transactions"},
                {"documents", mongo::bson_array{mongo::bson_document{{"run_id", run_id}}}}});
        if (!inserted)
        {
            transaction_pool.close();
            co_return false;
        }
        auto aborted = co_await session.abort_transaction(transaction_pool);
        transaction_pool.close();
        if (!aborted)
            co_return false;
        auto visible = co_await mongo::execute_retryable_command(topology_pool_, config_.database,
            mongo::bson_document{{"count", "cnetmod_aborted_transactions"},
                {"query", mongo::bson_document{{"run_id", run_id}}}},
            mongo::operation_kind::read);
        if (!visible)
            co_return false;
        const auto* count = visible->find("n");
        const auto count_value = count && count->get_if<std::int64_t>()
            ? *count->get_if<std::int64_t>()
            : count && count->get_if<std::int32_t>() ? *count->get_if<std::int32_t>()
                                                     : -1;
        co_return count_value == 0;
    }

private:
    [[nodiscard]] static auto retryable(
        const cnetmod::mongodb::error& error) noexcept -> bool
    {
        using cnetmod::mongodb::error_code;
        return error.labels.contains("TransientTransactionError") ||
            error.labels.contains("UnknownTransactionCommitResult") ||
            error.code == error_code::connection_failed ||
            error.code == error_code::connection_closed ||
            error.code == error_code::operation_timed_out ||
            error.code == error_code::operation_cancelled ||
            error.code == error_code::server_selection_failed ||
            error.code == error_code::pool_exhausted;
    }

    cnetmod::io_context& context_;
    const service_config& config_;
    cnetmod::mongodb::topology_connection_pool& topology_pool_;
};

} // namespace mongodb_example
