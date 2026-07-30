export module cnetmod.database.sql_query_data;

import std;

export namespace cnetmod::database {

enum class field_kind : std::uint8_t
{
    null,
    int64,
    uint64,
    string,
    blob,
    float_,
    double_,
    date,
    datetime,
    time
};

enum class column_type
{
    tinyint,
    smallint,
    mediumint,
    int_,
    bigint,
    float_,
    double_,
    decimal,
    bit,
    year,
    time,
    date,
    datetime,
    timestamp,
    char_,
    varchar,
    binary,
    varbinary,
    text,
    blob,
    enum_,
    set,
    json,
    geometry,
    boolean,
    uuid,
    array,
    unknown
};

struct calendar_date
{
    std::uint16_t year{};
    std::uint8_t month{};
    std::uint8_t day{};

    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return year > 0 && year <= 9999 && month > 0 && month <= 12 && day > 0 && day <= 31;
    }

    [[nodiscard]] auto to_string() const -> std::string
    {
        return std::format("{:04}-{:02}-{:02}", year, month, day);
    }
};

struct calendar_datetime
{
    std::uint16_t year{};
    std::uint8_t month{}, day{}, hour{}, minute{}, second{};
    std::uint32_t microsecond{};

    [[nodiscard]] auto to_string() const -> std::string
    {
        auto base = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", year, month, day, hour, minute, second);
        if (microsecond)
            base += std::format(".{:06}", microsecond);
        return base;
    }
};

struct clock_time
{
    bool negative{};
    std::uint32_t hours{};
    std::uint8_t minutes{}, seconds{};
    std::uint32_t microsecond{};

    [[nodiscard]] auto to_string() const -> std::string
    {
        auto base = std::format("{}{:02}:{:02}:{:02}", negative ? "-" : "", hours, minutes, seconds);
        if (microsecond)
            base += std::format(".{:06}", microsecond);
        return base;
    }
};

class bad_field_access final : public std::exception
{
public:
    [[nodiscard]] auto what() const noexcept -> const char* override
    {
        return "bad SQL field access";
    }
};

struct field_value
{
    field_kind kind_ = field_kind::null;
    std::int64_t int_val{};
    std::uint64_t uint_val{};
    float float_val{};
    double double_val{};
    std::string str_val;
    calendar_date date_val;
    calendar_datetime datetime_val;
    clock_time time_val;

    [[nodiscard]] auto kind() const noexcept
    {
        return kind_;
    }

    [[nodiscard]] auto is_null() const noexcept
    {
        return kind_ == field_kind::null;
    }

    [[nodiscard]] auto is_int64() const noexcept
    {
        return kind_ == field_kind::int64;
    }

    [[nodiscard]] auto is_uint64() const noexcept
    {
        return kind_ == field_kind::uint64;
    }

    [[nodiscard]] auto is_string() const noexcept
    {
        return kind_ == field_kind::string;
    }

    [[nodiscard]] auto is_blob() const noexcept
    {
        return kind_ == field_kind::blob;
    }

    [[nodiscard]] auto is_float() const noexcept
    {
        return kind_ == field_kind::float_;
    }

    [[nodiscard]] auto is_double() const noexcept
    {
        return kind_ == field_kind::double_;
    }

    [[nodiscard]] auto is_date() const noexcept
    {
        return kind_ == field_kind::date;
    }

    [[nodiscard]] auto is_datetime() const noexcept
    {
        return kind_ == field_kind::datetime;
    }

    [[nodiscard]] auto is_time() const noexcept
    {
        return kind_ == field_kind::time;
    }

    [[nodiscard]] auto as_int64() const -> std::int64_t
    {
        check(field_kind::int64);
        return int_val;
    }

    [[nodiscard]] auto as_uint64() const -> std::uint64_t
    {
        check(field_kind::uint64);
        return uint_val;
    }

    [[nodiscard]] auto as_float() const -> float
    {
        check(field_kind::float_);
        return float_val;
    }

    [[nodiscard]] auto as_double() const -> double
    {
        check(field_kind::double_);
        return double_val;
    }

    [[nodiscard]] auto as_string() const -> std::string_view
    {
        if (!is_string() && !is_blob())
            throw bad_field_access{};
        return str_val;
    }

    [[nodiscard]] auto as_blob() const -> std::span<const unsigned char>
    {
        check(field_kind::blob);
        return {reinterpret_cast<const unsigned char*>(str_val.data()), str_val.size()};
    }

    [[nodiscard]] auto as_date() const -> const calendar_date&
    {
        check(field_kind::date);
        return date_val;
    }

    [[nodiscard]] auto as_datetime() const -> const calendar_datetime&
    {
        check(field_kind::datetime);
        return datetime_val;
    }

    [[nodiscard]] auto as_time() const -> const clock_time&
    {
        check(field_kind::time);
        return time_val;
    }

    [[nodiscard]] auto get_int64() const noexcept
    {
        return int_val;
    }

    [[nodiscard]] auto get_uint64() const noexcept
    {
        return uint_val;
    }

    [[nodiscard]] auto get_float() const noexcept
    {
        return float_val;
    }

    [[nodiscard]] auto get_double() const noexcept
    {
        return double_val;
    }

    [[nodiscard]] auto get_string() const noexcept -> std::string_view
    {
        return str_val;
    }

    [[nodiscard]] auto get_date() const noexcept -> const calendar_date&
    {
        return date_val;
    }

    [[nodiscard]] auto get_datetime() const noexcept -> const calendar_datetime&
    {
        return datetime_val;
    }

    [[nodiscard]] auto get_time() const noexcept -> const clock_time&
    {
        return time_val;
    }

    [[nodiscard]] auto to_string() const -> std::string
    {
        switch (kind_)
        {
        case field_kind::null:
            return "NULL";
        case field_kind::int64:
            return std::to_string(int_val);
        case field_kind::uint64:
            return std::to_string(uint_val);
        case field_kind::float_:
            return std::format("{}", float_val);
        case field_kind::double_:
            return std::format("{}", double_val);
        case field_kind::string:
        case field_kind::blob:
            return str_val;
        case field_kind::date:
            return date_val.to_string();
        case field_kind::datetime:
            return datetime_val.to_string();
        case field_kind::time:
            return time_val.to_string();
        }
        return {};
    }

    static auto null() -> field_value
    {
        return {};
    }

    static auto from_int64(std::int64_t v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::int64;
        r.int_val = v;
        return r;
    }

    static auto from_uint64(std::uint64_t v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::uint64;
        r.uint_val = v;
        return r;
    }

    static auto from_float(float v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::float_;
        r.float_val = v;
        return r;
    }

    static auto from_double(double v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::double_;
        r.double_val = v;
        return r;
    }

    static auto from_string(std::string v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::string;
        r.str_val = std::move(v);
        return r;
    }

    static auto from_blob(std::string v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::blob;
        r.str_val = std::move(v);
        return r;
    }

    static auto from_date(calendar_date v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::date;
        r.date_val = v;
        return r;
    }

    static auto from_datetime(calendar_datetime v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::datetime;
        r.datetime_val = v;
        return r;
    }

    static auto from_time(clock_time v) -> field_value
    {
        field_value r;
        r.kind_ = field_kind::time;
        r.time_val = v;
        return r;
    }

private:
    void check(field_kind expected) const
    {
        if (kind_ != expected)
            throw bad_field_access{};
    }
};

using row = std::vector<field_value>;

struct column_metadata
{
    std::string database, table, original_table, name, original_name;
    std::uint32_t native_type{};
    std::int16_t format_code{};
    std::int16_t type_size{};
    std::int32_t type_modifier{};
    bool nullable = true;
};

struct query_result
{
    std::vector<column_metadata> columns;
    std::vector<row> rows;
    std::uint64_t affected_rows{};
    std::uint64_t last_insert_id{};
    std::string info, error_msg, sql_state;
    std::uint32_t error_code{};

    [[nodiscard]] auto ok() const noexcept
    {
        return error_msg.empty();
    }

    [[nodiscard]] auto is_err() const noexcept
    {
        return !ok();
    }

    [[nodiscard]] auto has_rows() const noexcept
    {
        return !rows.empty();
    }
};

enum class isolation_level
{
    read_uncommitted,
    read_committed,
    repeatable_read,
    serializable
};

using column_meta = column_metadata;
using result_set = query_result;

} // namespace cnetmod::database
