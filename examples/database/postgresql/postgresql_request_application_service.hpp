#pragma once

namespace postgresql_example {

class request_application_service
{
public:
    request_application_service(transaction_boundary& transactions,
        request_repository& repository, service_health& health)
        : transactions_(transactions), repository_(repository), health_(health) {}

    auto create(std::string request_id, std::int64_t sequence_number)
        -> cnetmod::task<R<request_record>>
    {
        co_return co_await execute_record_operation(
            [this, request_id = std::move(request_id), sequence_number](cnetmod::orm::postgresql_session& session)
            {
                return repository_.create_idempotently(
                    session, request_id, sequence_number);
            });
    }

    auto find(std::string request_id) -> cnetmod::task<R<request_record>>
    {
        co_return co_await execute_record_operation(
            [this, request_id = std::move(request_id)](cnetmod::orm::postgresql_session& session)
            {
                return repository_.find(session, request_id);
            });
    }

    auto update(std::string request_id, std::int64_t sequence_number)
        -> cnetmod::task<R<request_record>>
    {
        co_return co_await execute_record_operation(
            [this, request_id = std::move(request_id), sequence_number](cnetmod::orm::postgresql_session& session)
            {
                return repository_.update(session, request_id, sequence_number);
            });
    }

    auto remove(std::string request_id) -> cnetmod::task<R<empty_response>>
    {
        request_health_guard guard(health_);
        auto result = co_await transactions_.execute_model<request_record>(
            [this, request_id = std::move(request_id)](cnetmod::orm::postgresql_session& session)
            {
                return repository_.remove(session, request_id);
            });
        if (result.is_err())
            co_return R<empty_response>::error(
                application_error_code::service_unavailable,
                "database unavailable", std::move(result.error_msg));
        guard.mark_succeeded();
        if (result.affected_rows == 0)
            co_return R<empty_response>::error(
                application_error_code::resource_not_found,
                "request not found");
        co_return R<empty_response>::ok(empty_response{});
    }

private:
    template <class Operation>
    auto execute_record_operation(Operation operation)
        -> cnetmod::task<R<request_record>>
    {
        request_health_guard guard(health_);
        auto result = co_await transactions_.execute_model<request_record>(
            std::move(operation));
        if (result.is_err())
            co_return R<request_record>::error(
                application_error_code::service_unavailable,
                "database unavailable", std::move(result.error_msg));
        auto record = result.first();
        if (!record)
        {
            guard.mark_succeeded();
            co_return R<request_record>::error(
                application_error_code::resource_not_found,
                "request not found");
        }
        guard.mark_succeeded();
        co_return R<request_record>::ok(std::move(*record));
    }

    transaction_boundary& transactions_;
    request_repository& repository_;
    service_health& health_;
};

} // namespace postgresql_example
