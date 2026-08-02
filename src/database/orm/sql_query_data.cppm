export module cnetmod.orm.sql_query_data;

export import cnetmod.database.sql_query_data;

// Compatibility surface for existing cnetmod::orm model and mapper APIs.
// Protocol clients consume cnetmod::database directly and therefore do not
// depend on the ORM component.
export namespace cnetmod::orm {

using database::bad_field_access;
using database::calendar_date;
using database::calendar_datetime;
using database::clock_time;
using database::column_meta;
using database::column_metadata;
using database::column_type;
using database::field_kind;
using database::field_value;
using database::isolation_level;
using database::query_result;
using database::result_set;
using database::row;

} // namespace cnetmod::orm
