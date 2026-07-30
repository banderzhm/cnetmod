export module cnetmod.orm.sql_statement_formatting;

import std;
import cnetmod.orm.sql_parameters;

export namespace cnetmod::orm {

enum class format_errc : std::uint8_t
{
    ok,
    invalid_format_string,
    arg_not_found,
    invalid_encoding,
    manual_auto_mix
};

inline auto quote_sql_string(std::string_view input, bool backslash_escapes) -> std::string
{
    std::string out{"'"};
    out.reserve(input.size() + 2);
    for (char c : input)
    {
        if (c == '\'')
            out += "''";
        else if (c == '\\' && backslash_escapes)
            out += "\\\\";
        else if (c == '\0')
            throw std::invalid_argument("SQL string contains NUL");
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

inline auto format_parameter(const query_parameter& value, const sql_format_options& options) -> std::string
{
    using kind = query_parameter::kind_t;
    switch (value.kind)
    {
    case kind::null_kind:
        return "NULL";
    case kind::int64_kind:
        return std::to_string(value.int_val);
    case kind::uint64_kind:
        return std::to_string(value.uint_val);
    case kind::double_kind:
        return std::format("{}", value.double_val);
    case kind::string_kind:
        return quote_sql_string(value.str_val, options.backslash_escapes);
    case kind::blob_kind:
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out = "X'";
        out.reserve(value.str_val.size() * 2 + 3);
        for (unsigned char c : value.str_val)
        {
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
        out.push_back('\'');
        return out;
    }
    case kind::date_kind:
        return quote_sql_string(value.date_val.to_string(), false);
    case kind::datetime_kind:
        return quote_sql_string(value.datetime_val.to_string(), false);
    case kind::time_kind:
        return quote_sql_string(value.time_val.to_string(), false);
    }
    return "NULL";
}

inline auto format_sql(const sql_format_options& options, std::string_view format,
    std::span<const query_parameter> arguments) -> std::expected<std::string, format_errc>
{
    std::string output;
    output.reserve(format.size() + arguments.size() * 8);
    std::size_t argument{};
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{')
        {
            output.push_back('{');
            ++i;
            continue;
        }
        if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}')
        {
            output.push_back('}');
            ++i;
            continue;
        }
        if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '}')
        {
            if (argument >= arguments.size())
                return std::unexpected(format_errc::arg_not_found);
            try
            {
                output += format_parameter(arguments[argument++], options);
            }
            catch (...)
            {
                return std::unexpected(format_errc::invalid_encoding);
            }
            ++i;
            continue;
        }
        if (format[i] == '{' || format[i] == '}')
            return std::unexpected(format_errc::invalid_format_string);
        output.push_back(format[i]);
    }
    if (argument != arguments.size())
        return std::unexpected(format_errc::arg_not_found);
    return output;
}

} // namespace cnetmod::orm
