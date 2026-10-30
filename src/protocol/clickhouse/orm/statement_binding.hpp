#pragma once

namespace cnetmod::clickhouse::orm_detail {
struct bound_statement
{
    std::string sql;
    query_options request;
};

auto bind_statement(database::parameterized_query) -> std::expected<bound_statement, std::string>;
auto adapt_result(std::expected<query_result, error>, const orm_value_decoder& = {}) -> database::query_result;
} // namespace cnetmod::clickhouse::orm_detail
