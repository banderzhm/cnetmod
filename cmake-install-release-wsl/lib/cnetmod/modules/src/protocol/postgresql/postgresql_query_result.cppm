export module cnetmod.protocol.postgresql:query_result;

import std;
export import cnetmod.database.sql_query_data;
export import cnetmod.database.sql_parameters;

export namespace cnetmod::postgresql {

using result_set = database::query_result;
using row = database::row;
using field_value = database::field_value;
using column_meta = database::column_metadata;
using param_value = database::query_parameter;
using format_options = database::sql_format_options;
using isolation_level = database::isolation_level;
using parameterized_query = database::parameterized_query;
using database::with_params;

struct prepared_statement
{
    std::string name;
    std::string sql;
    std::size_t parameter_count{};

    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return !sql.empty();
    }
};

} // namespace cnetmod::postgresql
