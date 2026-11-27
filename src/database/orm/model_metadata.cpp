module;
#include "../../utils/detail/simdjson_depth.hpp"

module cnetmod.orm.model_metadata;

import cnetmod.utils.charconv;

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
    static constexpr auto names = [] {
        std::array<std::string_view, static_cast<std::size_t>(column_type::unknown) + 1> result;
        result.fill("TEXT");
        const auto bind = [&](column_type kind, std::string_view spelling) {
            result[static_cast<std::size_t>(kind)] = spelling;
        };
        using enum column_type;
        bind(tinyint, "TINYINT");
        bind(smallint, "SMALLINT");
        bind(mediumint, "MEDIUMINT");
        bind(int_, "INT");
        bind(bigint, "BIGINT");
        bind(float_, "FLOAT");
        bind(double_, "DOUBLE");
        bind(decimal, "DECIMAL");
        bind(bit, "BIT");
        bind(year, "YEAR");
        bind(time, "TIME");
        bind(date, "DATE");
        bind(datetime, "DATETIME");
        bind(timestamp, "TIMESTAMP");
        bind(char_, "CHAR(255)");
        bind(varchar, "VARCHAR(255)");
        bind(binary, "BINARY(255)");
        bind(varbinary, "VARBINARY(255)");
        bind(text, "TEXT");
        bind(blob, "BLOB");
        bind(enum_, "VARCHAR(64)");
        bind(set, "VARCHAR(255)");
        bind(json, "JSON");
        bind(geometry, "GEOMETRY");
        return result;
    }();
    const auto index = static_cast<std::size_t>(type);
    return index < names.size() ? names[index] : std::string_view{"TEXT"};
}
} // namespace cnetmod::orm

namespace cnetmod::orm::detail {
namespace {

    [[nodiscard]] auto json_type_error()
        -> std::expected<void, std::error_code>
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
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

namespace {

auto read_record_scalar(simdjson::dom::element value)
    -> std::expected<record_json_scalar, std::error_code>
{
    using simdjson::dom::element_type;
    switch (value.type())
    {
    case element_type::NULL_VALUE:
        return record_json_scalar{nullptr};
    case element_type::BOOL:
        return record_json_scalar{value.get_bool().value_unsafe()};
    case element_type::INT64:
        return record_json_scalar{value.get_int64().value_unsafe()};
    case element_type::UINT64:
        return record_json_scalar{value.get_uint64().value_unsafe()};
    case element_type::DOUBLE:
        return record_json_scalar{value.get_double().value_unsafe()};
    case element_type::STRING:
        return record_json_scalar{value.get_string().value_unsafe()};
    case element_type::BIGINT:
    {
        const auto number = value.get_bigint().value_unsafe();
        double converted{};
        if (cnetmod::from_chars_double(number, converted) != std::errc{} ||
            !std::isfinite(converted))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return record_json_scalar{converted};
    }
    case element_type::ARRAY:
    case element_type::OBJECT:
        return record_json_scalar{std::monostate{}};
    }
    std::unreachable();
}

} // namespace

auto read_record_json(std::string_view input, std::span<const std::string_view> names,
    bool reject_unknown, void* context, record_json_setter assign)
    -> std::expected<void, std::error_code>
{
    simdjson::dom::parser parser;
    parser.number_as_string(true);
    simdjson::dom::element root;
    if (!cnetmod::detail::parse_bounded_json(parser, input, root) || !root.is_object())
        return json_type_error();

    // Keep only the last occurrence of a key before converting the field.
    // The tape views are consumed while the parser is still alive.
    std::vector<std::optional<simdjson::dom::element>> values(names.size());
    for (const auto field : root.get_object().value_unsafe())
    {
        const auto matches = std::ranges::equal_range(names, field.key);
        if (reject_unknown && (matches.empty() || matches.size() > 1))
            return json_type_error();
        if (field.value.type() == simdjson::dom::element_type::BIGINT &&
            !read_record_scalar(field.value))
            return json_type_error();
        for (auto it = matches.begin(); it != matches.end(); ++it)
            values[static_cast<std::size_t>(it - names.begin())] = field.value;
    }
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (!values[index])
        {
            if (auto assigned = assign(context, index, nullptr); !assigned)
                return assigned;
            continue;
        }
        auto scalar = read_record_scalar(*values[index]);
        if (!scalar)
            return std::unexpected(scalar.error());
        if (auto assigned = assign(context, index, &*scalar); !assigned)
            return assigned;
    }
    return {};
}

auto write_record_json(std::span<const std::string_view> names, bool emit_nulls,
    const void* context, record_json_getter get)
    -> std::expected<std::string, std::error_code>
{
    simdjson::builder::string_builder builder;
    builder.start_object();
    bool first = true;
    for (std::size_t index = 0; index < names.size();)
    {
        const auto key = names[index];
        record_json_scalar scalar;
        do
        {
            auto value = get(context, index++);
            if (emit_nulls || !std::holds_alternative<std::nullptr_t>(value))
                scalar = std::move(value);
        } while (index < names.size() && names[index] == key);
        if (std::holds_alternative<std::monostate>(scalar))
            continue;
        if (!std::exchange(first, false))
            builder.append_comma();
        std::visit([&](const auto& value)
        {
            using value_type = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<value_type, std::string>)
                builder.append_key_value(key, std::string_view{value});
            else if constexpr (std::same_as<value_type, std::monostate>)
                builder.append_key_value(key, nullptr);
            else if constexpr (std::same_as<value_type, double>)
            {
                if (std::isfinite(value))
                    builder.append_key_value(key, value);
                else
                    builder.append_key_value(key, nullptr);
            }
            else
                builder.append_key_value(key, value);
        }, scalar);
    }
    builder.end_object();
    std::string_view output;
    if (builder.view().get(output) || !builder.validate_unicode())
        return std::unexpected(std::make_error_code(std::errc::illegal_byte_sequence));
    return std::string{output};
}

auto decode_json_member(calendar_date& member, const record_json_scalar& source)
    -> std::expected<void, std::error_code>
{
    const auto* value = std::get_if<std::string_view>(&source);
    if (!value || !parse_date(*value, member))
        return json_type_error();
    return {};
}

auto decode_json_member(calendar_datetime& member, const record_json_scalar& source)
    -> std::expected<void, std::error_code>
{
    const auto* value = std::get_if<std::string_view>(&source);
    if (!value || !parse_datetime(*value, member))
        return json_type_error();
    return {};
}

auto decode_json_member(clock_time& member, const record_json_scalar& source)
    -> std::expected<void, std::error_code>
{
    const auto* value = std::get_if<std::string_view>(&source);
    if (!value || !parse_time(*value, member))
        return json_type_error();
    return {};
}

auto decode_json_member(uuid& member, const record_json_scalar& source)
    -> std::expected<void, std::error_code>
{
    const auto* value = std::get_if<std::string_view>(&source);
    if (!value)
        return json_type_error();
    auto parsed = uuid::from_string(*value);
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
