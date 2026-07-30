module cnetmod.protocol.mysql;

import std;
import :types;
import :orm_mysql_result_adapter;

namespace cnetmod::orm {

auto mysql_adapt_field(const cnetmod::mysql::field_value& source) -> field_value
{
    using mysql_kind = cnetmod::mysql::field_kind;
    switch (source.kind())
    {
    case mysql_kind::null:
        return field_value::null();
    case mysql_kind::int64:
        return field_value::from_int64(source.as_int64());
    case mysql_kind::uint64:
        return field_value::from_uint64(source.as_uint64());
    case mysql_kind::string:
        return field_value::from_string(std::string(source.as_string()));
    case mysql_kind::blob:
    {
        const auto bytes = source.as_blob();
        if (bytes.empty())
            return field_value::from_blob({});
        return field_value::from_blob(std::string(
            reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    case mysql_kind::float_:
        return field_value::from_float(source.as_float());
    case mysql_kind::double_:
        return field_value::from_double(source.as_double());
    case mysql_kind::date:
    {
        const auto& value = source.as_date();
        return field_value::from_date({value.year, value.month, value.day});
    }
    case mysql_kind::datetime:
    {
        const auto& value = source.as_datetime();
        return field_value::from_datetime({value.year, value.month, value.day,
            value.hour, value.minute, value.second, value.microsecond});
    }
    case mysql_kind::time:
    {
        const auto& value = source.as_time();
        return field_value::from_time({value.negative, value.hours,
            value.minutes, value.seconds, value.microsecond});
    }
    }
    return field_value::null();
}

auto mysql_adapt_parameter(const query_parameter& source)
    -> cnetmod::mysql::param_value
{
    using source_kind = query_parameter::kind_t;
    using destination = cnetmod::mysql::param_value;
    switch (source.kind)
    {
    case source_kind::null_kind:
        return destination::null();
    case source_kind::int64_kind:
        return destination::from_int(source.int_val);
    case source_kind::uint64_kind:
        return destination::from_uint(source.uint_val);
    case source_kind::double_kind:
        return destination::from_double(source.double_val);
    case source_kind::string_kind:
        return destination::from_string(source.str_val);
    case source_kind::blob_kind:
        return destination::from_blob(source.str_val);
    case source_kind::date_kind:
        return destination::from_date({source.date_val.year,
            source.date_val.month, source.date_val.day});
    case source_kind::datetime_kind:
        return destination::from_datetime({source.datetime_val.year,
            source.datetime_val.month, source.datetime_val.day,
            source.datetime_val.hour, source.datetime_val.minute,
            source.datetime_val.second, source.datetime_val.microsecond});
    case source_kind::time_kind:
        return destination::from_time({source.time_val.negative,
            source.time_val.hours, source.time_val.minutes,
            source.time_val.seconds, source.time_val.microsecond});
    }
    return destination::null();
}

auto mysql_adapt_parameter(const cnetmod::mysql::param_value& source)
    -> query_parameter
{
    using source_kind = cnetmod::mysql::param_value::kind_t;
    switch (source.kind)
    {
    case source_kind::null_kind:
        return query_parameter::null();
    case source_kind::int64_kind:
        return query_parameter::from_int(source.int_val);
    case source_kind::uint64_kind:
        return query_parameter::from_uint(source.uint_val);
    case source_kind::double_kind:
        return query_parameter::from_double(source.double_val);
    case source_kind::string_kind:
        return query_parameter::from_string(source.str_val);
    case source_kind::blob_kind:
        return query_parameter::from_blob(source.str_val);
    case source_kind::date_kind:
        return query_parameter::from_date({source.date_val.year,
            source.date_val.month, source.date_val.day});
    case source_kind::datetime_kind:
        return query_parameter::from_datetime({source.datetime_val.year,
            source.datetime_val.month, source.datetime_val.day,
            source.datetime_val.hour, source.datetime_val.minute,
            source.datetime_val.second, source.datetime_val.microsecond});
    case source_kind::time_kind:
        return query_parameter::from_time({source.time_val.negative,
            source.time_val.hours, source.time_val.minutes,
            source.time_val.seconds, source.time_val.microsecond});
    }
    return query_parameter::null();
}

auto mysql_adapt_result(const cnetmod::mysql::result_set& source) -> query_result
{
    query_result result;
    result.columns.reserve(source.columns.size());
    for (const auto& column : source.columns)
    {
        result.columns.push_back(column_metadata{
            .database = column.database,
            .table = column.table,
            .original_table = column.org_table,
            .name = column.name,
            .original_name = column.org_name,
            .native_type = static_cast<std::uint32_t>(column.type),
            .nullable = !column.is_not_null()});
    }
    result.rows.reserve(source.rows.size());
    for (const auto& source_row : source.rows)
    {
        row destination;
        destination.reserve(source_row.size());
        for (const auto& value : source_row)
            destination.push_back(mysql_adapt_field(value));
        result.rows.push_back(std::move(destination));
    }
    result.affected_rows = source.affected_rows;
    result.last_insert_id = source.last_insert_id;
    result.info = source.info;
    result.error_msg = source.error_msg;
    result.error_code = source.error_code;
    result.sql_state = source.sql_state;
    return result;
}

} // namespace cnetmod::orm
