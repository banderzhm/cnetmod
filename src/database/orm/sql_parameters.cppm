export module cnetmod.orm.sql_parameters;

export import cnetmod.database.sql_parameters;
export import cnetmod.orm.sql_query_data;

// Backwards-compatible ORM spellings for the protocol-neutral SQL values.
export namespace cnetmod::orm {

using database::format_options;
using database::param_value;
using database::parameterized_query;
using database::query_parameter;
using database::sql_format_options;
using database::to_query_parameter;
using database::with_params;

} // namespace cnetmod::orm
