module cnetmod.orm.model_metadata;

namespace cnetmod::orm {
auto column_def::is_pk() const noexcept -> bool
{
    return has_flag(flags, col_flag::primary_key);
}

auto column_def::is_auto() const noexcept -> bool
{
    return has_flag(flags, col_flag::auto_increment);
}

auto column_def::is_nullable() const noexcept -> bool
{
    return has_flag(flags, col_flag::nullable);
}

auto column_def::is_unique() const noexcept -> bool
{
    return has_flag(flags, col_flag::unique);
}

auto column_def::is_uuid() const noexcept -> bool
{
    return strategy == id_strategy::uuid;
}

auto column_def::is_snowflake() const noexcept -> bool
{
    return strategy == id_strategy::snowflake;
}

auto sql_type_str(column_type type) noexcept -> std::string_view
{
    switch (type)
    {
    case column_type::tinyint:
        return "TINYINT";
    case column_type::smallint:
        return "SMALLINT";
    case column_type::mediumint:
        return "MEDIUMINT";
    case column_type::int_:
        return "INT";
    case column_type::bigint:
        return "BIGINT";
    case column_type::float_:
        return "FLOAT";
    case column_type::double_:
        return "DOUBLE";
    case column_type::decimal:
        return "DECIMAL";
    case column_type::bit:
        return "BIT";
    case column_type::year:
        return "YEAR";
    case column_type::time:
        return "TIME";
    case column_type::date:
        return "DATE";
    case column_type::datetime:
        return "DATETIME";
    case column_type::timestamp:
        return "TIMESTAMP";
    case column_type::char_:
        return "CHAR(255)";
    case column_type::varchar:
        return "VARCHAR(255)";
    case column_type::binary:
        return "BINARY(255)";
    case column_type::varbinary:
        return "VARBINARY(255)";
    case column_type::text:
        return "TEXT";
    case column_type::blob:
        return "BLOB";
    case column_type::enum_:
        return "VARCHAR(64)";
    case column_type::set:
        return "VARCHAR(255)";
    case column_type::json:
        return "JSON";
    case column_type::geometry:
        return "GEOMETRY";
    default:
        return "TEXT";
    }
}
} // namespace cnetmod::orm

namespace cnetmod::orm::detail {
namespace {

    [[nodiscard]] auto json_type_error()
        -> std::expected<void, std::error_code>
    {
        return std::unexpected(cnetmod::json::make_error_code(
            cnetmod::json::errc::type_mismatch));
    }

    template <typename T>
    [[nodiscard]] auto parse_decimal(
        std::string_view input, std::size_t offset, std::size_t length, T& output)
        -> bool
    {
        if (offset + length > input.size() || length == 0)
            return false;
        unsigned value{};
        const auto* first = input.data() + offset;
        const auto* last = first + length;
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last ||
            value > static_cast<unsigned>(std::numeric_limits<T>::max()))
            return false;
        output = static_cast<T>(value);
        return true;
    }

    [[nodiscard]] auto parse_date(
        std::string_view input, calendar_date& output) -> bool
    {
        if (input.size() != 10 || input[4] != '-' || input[7] != '-')
            return false;
        if (!parse_decimal(input, 0, 4, output.year) ||
            !parse_decimal(input, 5, 2, output.month) ||
            !parse_decimal(input, 8, 2, output.day))
            return false;
        const std::chrono::year_month_day date{std::chrono::year{output.year},
            std::chrono::month{output.month}, std::chrono::day{output.day}};
        return date.ok();
    }

    [[nodiscard]] auto parse_datetime(
        std::string_view input, calendar_datetime& output) -> bool
    {
        if ((input.size() != 19 && input.size() != 26) ||
            (input[10] != ' ' && input[10] != 'T') || input[13] != ':' ||
            input[16] != ':')
            return false;
        calendar_date date;
        if (!parse_date(input.substr(0, 10), date) ||
            !parse_decimal(input, 11, 2, output.hour) ||
            !parse_decimal(input, 14, 2, output.minute) ||
            !parse_decimal(input, 17, 2, output.second) || output.hour > 23 ||
            output.minute > 59 || output.second > 59)
            return false;
        output.year = date.year;
        output.month = date.month;
        output.day = date.day;
        output.microsecond = 0;
        return input.size() == 19 ||
            (input[19] == '.' &&
                parse_decimal(input, 20, 6, output.microsecond));
    }

    [[nodiscard]] auto parse_time(
        std::string_view input, clock_time& output) -> bool
    {
        output = {};
        std::size_t offset{};
        if (!input.empty() && input.front() == '-')
        {
            output.negative = true;
            offset = 1;
        }
        const auto first_colon = input.find(':', offset);
        const auto second_colon = first_colon == std::string_view::npos
            ? std::string_view::npos
            : input.find(':', first_colon + 1);
        if (first_colon == std::string_view::npos ||
            second_colon == std::string_view::npos ||
            !parse_decimal(input, offset, first_colon - offset, output.hours) ||
            !parse_decimal(input, first_colon + 1, 2, output.minutes) ||
            second_colon != first_colon + 3 || output.minutes > 59)
            return false;
        const auto fraction = input.find('.', second_colon + 1);
        const auto seconds_length = fraction == std::string_view::npos
            ? input.size() - second_colon - 1
            : fraction - second_colon - 1;
        if (seconds_length != 2 ||
            !parse_decimal(input, second_colon + 1, 2, output.seconds) ||
            output.seconds > 59)
            return false;
        if (fraction == std::string_view::npos)
            return true;
        return fraction + 7 == input.size() &&
            parse_decimal(input, fraction + 1, 6, output.microsecond);
    }

} // namespace

auto encode_json_member(const calendar_date& value)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    return cnetmod::json::document(value.to_string());
}

auto encode_json_member(const calendar_datetime& value)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    return cnetmod::json::document(value.to_string());
}

auto encode_json_member(const clock_time& value)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    return cnetmod::json::document(value.to_string());
}

auto encode_json_member(const std::optional<calendar_datetime>& value)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    return value ? encode_json_member(*value)
                 : std::expected<cnetmod::json::document, std::error_code>{
                       cnetmod::json::document(nullptr)};
}

auto encode_json_member(const uuid& value)
    -> std::expected<cnetmod::json::document, std::error_code>
{
    return cnetmod::json::document(value.to_string());
}

auto decode_json_member(
    calendar_date& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>
{
    if (!source.is_string() ||
        !parse_date(source.get<std::string>(), member))
        return json_type_error();
    return {};
}

auto decode_json_member(
    calendar_datetime& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>
{
    if (!source.is_string() ||
        !parse_datetime(source.get<std::string>(), member))
        return json_type_error();
    return {};
}

auto decode_json_member(
    clock_time& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>
{
    if (!source.is_string() ||
        !parse_time(source.get<std::string>(), member))
        return json_type_error();
    return {};
}

auto decode_json_member(std::optional<calendar_datetime>& member,
    const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>
{
    if (source.is_null())
    {
        member.reset();
        return {};
    }
    calendar_datetime value;
    auto decoded = decode_json_member(value, source);
    if (!decoded)
        return decoded;
    member = value;
    return {};
}

auto decode_json_member(
    uuid& member, const cnetmod::json::document& source)
    -> std::expected<void, std::error_code>
{
    if (!source.is_string())
        return json_type_error();
    auto parsed = uuid::from_string(source.get<std::string>());
    if (!parsed)
        return json_type_error();
    member = *parsed;
    return {};
}

void set_member(std::int64_t& m, const field_value& v)
{
    if (v.is_int64())
        m = v.get_int64();
    else if (v.is_uint64())
        m = static_cast<std::int64_t>(v.get_uint64());
    else if (v.is_string())
        std::from_chars(v.get_string().data(),
            v.get_string().data() + v.get_string().size(), m);
    else if (v.is_datetime())
    {
        if (const auto seconds =
                database::unix_seconds_from_datetime(v.get_datetime()))
            m = *seconds;
    }
}

void set_member(std::uint64_t& m, const field_value& v)
{
    if (v.is_uint64())
        m = v.get_uint64();
    else if (v.is_int64())
        m = static_cast<std::uint64_t>(v.get_int64());
    else if (v.is_string())
        std::from_chars(v.get_string().data(),
            v.get_string().data() + v.get_string().size(), m);
}

void set_member(int& m, const field_value& v)
{
    std::int64_t n{};
    set_member(n, v);
    m = static_cast<int>(n);
}

void set_member(std::uint32_t& m, const field_value& v)
{
    std::uint64_t n{};
    set_member(n, v);
    m = static_cast<std::uint32_t>(n);
}

void set_member(float& m, const field_value& v)
{
    if (v.is_float())
        m = v.get_float();
    else if (v.is_double())
        m = static_cast<float>(v.get_double());
}

void set_member(double& m, const field_value& v)
{
    if (v.is_double())
        m = v.get_double();
    else if (v.is_float())
        m = v.get_float();
}

void set_member(std::string& m, const field_value& v)
{
    if (v.is_string())
        m = v.get_string();
    else if (!v.is_null())
        m = v.to_string();
}

void set_member(bool& m, const field_value& v)
{
    if (v.is_int64())
        m = v.get_int64() != 0;
    else if (v.is_uint64())
        m = v.get_uint64() != 0;
}

void set_member(calendar_date& m, const field_value& v)
{
    if (v.is_date())
        m = v.get_date();
}

void set_member(calendar_datetime& m, const field_value& v)
{
    if (v.is_datetime())
        m = v.get_datetime();
}

void set_member(clock_time& m, const field_value& v)
{
    if (v.is_time())
        m = v.get_time();
}

void set_member(std::optional<std::string>& m, const field_value& v)
{
    if (v.is_null())
        m = {};
    else if (v.is_string())
        m = std::string(v.get_string());
    else
        m = v.to_string();
}

void set_member(std::optional<std::int64_t>& m, const field_value& v)
{
    if (v.is_null())
        m = {};
    else
    {
        std::int64_t n{};
        set_member(n, v);
        m = n;
    }
}

void set_member(std::optional<double>& m, const field_value& v)
{
    if (v.is_null())
        m = {};
    else
    {
        double n{};
        set_member(n, v);
        m = n;
    }
}

void set_member(std::optional<calendar_datetime>& m, const field_value& v)
{
    if (v.is_null())
        m.reset();
    else if (v.is_datetime())
        m = v.get_datetime();
}

void set_member(uuid& m, const field_value& v)
{
    if (v.is_string())
        if (auto r = uuid::from_string(v.get_string()))
            m = *r;
}

auto get_member(std::int64_t v) -> param_value
{
    return param_value::from_int(v);
}

auto get_member(std::uint64_t v) -> param_value
{
    return param_value::from_uint(v);
}

auto get_member(int v) -> param_value
{
    return param_value::from_int(v);
}

auto get_member(std::uint32_t v) -> param_value
{
    return param_value::from_uint(v);
}

auto get_member(float v) -> param_value
{
    return param_value::from_double(v);
}

auto get_member(double v) -> param_value
{
    return param_value::from_double(v);
}

auto get_member(const std::string& v) -> param_value
{
    return param_value::from_string(v);
}

auto get_member(std::string_view v) -> param_value
{
    return param_value::from_string(std::string(v));
}

auto get_member(bool v) -> param_value
{
    return param_value::from_int(v ? 1 : 0);
}

auto get_member(const calendar_date& v) -> param_value
{
    return param_value::from_date(v);
}

auto get_member(const calendar_datetime& v) -> param_value
{
    return param_value::from_datetime(v);
}

auto get_member(const clock_time& v) -> param_value
{
    return param_value::from_time(v);
}

auto get_member(const std::optional<std::string>& v) -> param_value
{
    return v ? param_value::from_string(*v) : param_value::null();
}

auto get_member(const std::optional<std::int64_t>& v) -> param_value
{
    return v ? param_value::from_int(*v) : param_value::null();
}

auto get_member(const std::optional<double>& v) -> param_value
{
    return v ? param_value::from_double(*v) : param_value::null();
}

auto get_member(const std::optional<calendar_datetime>& v) -> param_value
{
    return v ? param_value::from_datetime(*v) : param_value::null();
}

auto get_member(const uuid& v) -> param_value
{
    return param_value::from_string(v.to_string());
}
} // namespace cnetmod::orm::detail
