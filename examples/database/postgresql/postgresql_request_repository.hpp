#pragma once

namespace postgresql_example {

struct request_record
{
    std::int64_t id{};
    std::string request_id;
    std::int64_t sequence_number{};
    std::string payload;
};

} // namespace postgresql_example

CNETMOD_MODEL(postgresql_example::request_record, "cnetmod_http_requests",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(request_id, "request_id", varchar, UNIQUE_KEY),
    CNETMOD_FIELD(sequence_number, "sequence_number", bigint),
    CNETMOD_FIELD(payload, "payload", varchar))

namespace postgresql_example {

/// Domain repository backed entirely by cnetmod::orm. Business code describes
/// entities and mapped fields; PostgreSQL SQL generation belongs to the ORM
/// adapter rather than leaking into this layer.
class request_repository
{
public:
    using result = cnetmod::orm::postgresql_orm_result<request_record>;

    auto create_idempotently(cnetmod::orm::postgresql_session& session,
        std::string_view request_id, std::int64_t sequence_number)
        -> cnetmod::task<result>
    {
        request_record record{
            .request_id = std::string(request_id),
            .sequence_number = sequence_number,
            .payload = std::format("request-{}", sequence_number)};
        co_return co_await session.insert_or_get(record, "request_id");
    }

    auto find(cnetmod::orm::postgresql_session& session, std::string_view request_id)
        -> cnetmod::task<result>
    {
        co_return co_await session.find_one_by<request_record>("request_id",
            cnetmod::orm::param_value::from_string(std::string(request_id)));
    }

    auto update(cnetmod::orm::postgresql_session& session,
        std::string_view request_id, std::int64_t sequence_number)
        -> cnetmod::task<result>
    {
        request_record record{
            .request_id = std::string(request_id),
            .sequence_number = sequence_number,
            .payload = std::format("request-{}", sequence_number)};
        co_return co_await session.update_by(record, "request_id",
            cnetmod::orm::param_value::from_string(std::string(request_id)));
    }

    auto remove(cnetmod::orm::postgresql_session& session, std::string_view request_id)
        -> cnetmod::task<result>
    {
        co_return co_await session.remove_by<request_record>("request_id",
            cnetmod::orm::param_value::from_string(std::string(request_id)));
    }
};

} // namespace postgresql_example
