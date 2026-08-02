export module cnetmod.database.sql_parameters;

import std;
import cnetmod.database.sql_query_data;

export namespace cnetmod::database {

struct query_parameter
{
    enum class kind_t : std::uint8_t
    {
        null_kind,
        int64_kind,
        uint64_kind,
        double_kind,
        string_kind,
        blob_kind,
        date_kind,
        datetime_kind,
        time_kind
    };
    kind_t kind = kind_t::null_kind;
    std::int64_t int_val{};
    std::uint64_t uint_val{};
    double double_val{};
    std::string str_val;
    calendar_date date_val;
    calendar_datetime datetime_val;
    clock_time time_val;

    static auto null() -> query_parameter
    {
        return {};
    }

    static auto from_int(std::int64_t v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::int64_kind;
        r.int_val = v;
        return r;
    }

    static auto from_uint(std::uint64_t v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::uint64_kind;
        r.uint_val = v;
        return r;
    }

    static auto from_double(double v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::double_kind;
        r.double_val = v;
        return r;
    }

    static auto from_string(std::string v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::string_kind;
        r.str_val = std::move(v);
        return r;
    }

    static auto from_blob(std::string v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::blob_kind;
        r.str_val = std::move(v);
        return r;
    }

    static auto from_date(calendar_date v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::date_kind;
        r.date_val = v;
        return r;
    }

    static auto from_datetime(calendar_datetime v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::datetime_kind;
        r.datetime_val = v;
        return r;
    }

    static auto from_time(clock_time v) -> query_parameter
    {
        query_parameter r;
        r.kind = kind_t::time_kind;
        r.time_val = v;
        return r;
    }
};

struct parameterized_query
{
    std::string_view query;
    std::vector<query_parameter> args;
};

inline auto with_params(std::string_view sql, std::initializer_list<query_parameter> args) -> parameterized_query
{
    return {sql, args};
}

inline auto with_params(std::string_view sql, std::vector<query_parameter> args) -> parameterized_query
{
    return {sql, std::move(args)};
}

struct sql_format_options
{
    bool backslash_escapes = false;
};

using param_value = query_parameter;
using format_options = sql_format_options;

inline auto to_query_parameter(std::int64_t value) -> query_parameter
{
    return query_parameter::from_int(value);
}

inline auto to_query_parameter(std::uint64_t value) -> query_parameter
{
    return query_parameter::from_uint(value);
}

inline auto to_query_parameter(int value) -> query_parameter
{
    return query_parameter::from_int(value);
}

inline auto to_query_parameter(std::uint32_t value) -> query_parameter
{
    return query_parameter::from_uint(value);
}

inline auto to_query_parameter(double value) -> query_parameter
{
    return query_parameter::from_double(value);
}

inline auto to_query_parameter(float value) -> query_parameter
{
    return query_parameter::from_double(value);
}

inline auto to_query_parameter(const std::string& value) -> query_parameter
{
    return query_parameter::from_string(value);
}

inline auto to_query_parameter(std::string_view value) -> query_parameter
{
    return query_parameter::from_string(std::string(value));
}

inline auto to_query_parameter(const char* value) -> query_parameter
{
    return query_parameter::from_string(std::string(value));
}

inline auto to_query_parameter(bool value) -> query_parameter
{
    return query_parameter::from_int(value ? 1 : 0);
}

inline auto to_query_parameter(const calendar_date& value) -> query_parameter
{
    return query_parameter::from_date(value);
}

inline auto to_query_parameter(const calendar_datetime& value) -> query_parameter
{
    return query_parameter::from_datetime(value);
}

inline auto to_query_parameter(const clock_time& value) -> query_parameter
{
    return query_parameter::from_time(value);
}

} // namespace cnetmod::database
