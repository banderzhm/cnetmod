module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse.orm;
#ifdef CNETMOD_HAS_ORM
import std;
import cnetmod.database.sql_parameters;
import cnetmod.database.sql_query_data;
import cnetmod.protocol.clickhouse;
    #include "statement_binding.hpp"

namespace cnetmod::clickhouse::orm_detail {
namespace {
    auto bare_type(std::string type) -> std::string
    {
        for (;;)
        {
            if (type.starts_with("Nullable("))
            {
                type = type.substr(9, type.size() - 10);
                continue;
            }
            if (type.starts_with("LowCardinality("))
            {
                type = type.substr(15, type.size() - 16);
                continue;
            }
            return type;
        }
    }

    auto wide_decimal(std::string bytes, bool signed_value) -> std::string
    {
        const bool negative = signed_value && !bytes.empty() && (static_cast<unsigned char>(bytes.back()) & 128);
        std::vector<unsigned> digits(bytes.size());
        unsigned carry = negative ? 1 : 0;
        for (std::size_t i{}; i < bytes.size(); ++i)
        {
            const auto byte = static_cast<unsigned char>(bytes[i]);
            const auto value = (negative ? 255 - byte : byte) + carry;
            digits[i] = value & 255;
            carry = value >> 8;
        }
        std::string result;
        do
        {
            unsigned remainder{};
            for (std::size_t i = digits.size(); i > 0; --i)
            {
                auto value = remainder * 256 + digits[i - 1];
                digits[i - 1] = value / 10;
                remainder = value % 10;
            }
            result += static_cast<char>('0' + remainder);
        } while (std::ranges::any_of(digits, [](auto value)
            {
                return value != 0;
            }));
        if (negative)
            result += '-';
        std::reverse(result.begin(), result.end());
        return result;
    }

    auto numeric_text(const value& input) -> std::string
    {
        if (const auto* n = std::get_if<std::int64_t>(&input.data))
            return std::to_string(*n);
        if (const auto* n = std::get_if<std::uint64_t>(&input.data))
            return std::to_string(*n);
        return wide_decimal(std::get<std::string>(input.data), true);
    }

    auto decimal_text(const value& input, const std::string& type) -> std::string
    {
        auto opening = type.find('('), comma = type.find(',');
        auto scale_text = type.substr(comma == type.npos ? opening + 1 : comma + 1);
        unsigned scale{};
        auto begin = scale_text.find_first_not_of(' ');
        if (begin != scale_text.npos)
            std::from_chars(scale_text.data() + begin, scale_text.data() + scale_text.size(), scale);
        if (scale > 76)
            throw std::invalid_argument("invalid Decimal scale");
        auto result = numeric_text(input);
        bool negative = result.front() == '-';
        if (negative)
            result.erase(0, 1);
        if (scale)
        {
            if (result.size() <= scale)
                result.insert(0, scale + 1 - result.size(), '0');
            result.insert(result.size() - scale, 1, '.');
        }
        if (negative)
            result.insert(0, 1, '-');
        return result;
    }

    auto calendar(std::int64_t ticks, std::int64_t factor) -> database::calendar_datetime
    {
        auto seconds = ticks / factor;
        auto fraction = ticks % factor;
        if (fraction < 0)
        {
            --seconds;
            fraction += factor;
        }
        if (seconds < -62135596800LL || seconds > 253402300799LL)
            throw std::invalid_argument("ClickHouse timestamp exceeds common calendar range");
        const auto instant = std::chrono::sys_time<std::chrono::microseconds>{std::chrono::seconds{seconds} + std::chrono::microseconds{fraction * (1'000'000 / factor)}};
        const auto day = std::chrono::floor<std::chrono::days>(instant);
        const std::chrono::year_month_day date{day};
        const std::chrono::hh_mm_ss time{instant - day};
        return {static_cast<std::uint16_t>(int(date.year())), static_cast<std::uint8_t>(unsigned(date.month())), static_cast<std::uint8_t>(unsigned(date.day())), static_cast<std::uint8_t>(time.hours().count()), static_cast<std::uint8_t>(time.minutes().count()), static_cast<std::uint8_t>(time.seconds().count()), static_cast<std::uint32_t>(time.subseconds().count())};
    }

    auto field(const value& input, const std::string& spelling) -> database::field_value
    {
        const auto type = bare_type(spelling);
        if (std::holds_alternative<std::monostate>(input.data))
            return {};
        if (type.starts_with("Decimal"))
            return database::field_value::from_string(decimal_text(input, type));
        if (type == "Date" || type == "Date32")
        {
            const auto days = std::holds_alternative<std::int64_t>(input.data) ? std::get<std::int64_t>(input.data) : static_cast<std::int64_t>(std::get<std::uint64_t>(input.data));
            const std::chrono::year_month_day date{std::chrono::sys_days{std::chrono::days{days}}};
            return database::field_value::from_date({static_cast<std::uint16_t>(int(date.year())), static_cast<std::uint8_t>(unsigned(date.month())), static_cast<std::uint8_t>(unsigned(date.day()))});
        }
        if (type.starts_with("DateTime"))
        {
            std::int64_t factor = 1;
            if (type.starts_with("DateTime64("))
            {
                unsigned scale{};
                std::from_chars(type.data() + 11, type.data() + type.size(), scale);
                for (unsigned i{}; i < scale; ++i)
                    factor *= 10;
            }
            const auto ticks = std::holds_alternative<std::int64_t>(input.data) ? std::get<std::int64_t>(input.data) : static_cast<std::int64_t>(std::get<std::uint64_t>(input.data));
            // The common SQL value supports microseconds. Preserve finer precision
            // as text rather than silently truncate or overflow the common value.
            if (factor > 1'000'000)
                return database::field_value::from_string(numeric_text(input));
            return database::field_value::from_datetime(calendar(ticks, factor));
        }
        if (const auto* number = std::get_if<std::int64_t>(&input.data))
            return database::field_value::from_int64(*number);
        if (const auto* number = std::get_if<std::uint64_t>(&input.data))
            return database::field_value::from_uint64(*number);
        if (const auto* number = std::get_if<double>(&input.data))
            return database::field_value::from_double(*number);
        if (const auto* text = std::get_if<std::string>(&input.data))
        {
            if (type.starts_with("UInt") || type.starts_with("Int"))
                return database::field_value::from_string(wide_decimal(*text, type.starts_with("Int")));
            if (type == "UUID")
            {
                if (text->size() != 16)
                    throw std::invalid_argument("invalid UUID size");
                constexpr char hex[] = "0123456789abcdef";
                std::string uuid;
                for (unsigned i{}; i < 16; ++i)
                {
                    if (i == 4 || i == 6 || i == 8 || i == 10)
                        uuid += '-';
                    auto byte = static_cast<unsigned char>((*text)[i < 8 ? 7 - i : 23 - i]);
                    uuid += hex[byte >> 4];
                    uuid += hex[byte & 15];
                }
                return database::field_value::from_string(std::move(uuid));
            }
            return database::field_value::from_string(*text);
        }
        throw std::invalid_argument("nested ClickHouse columns require an explicit ORM value type handler");
    }
} // namespace

auto adapt_result(std::expected<query_result, error> input, const orm_value_decoder& decoder) -> database::query_result
{
    database::query_result result;
    if (!input)
    {
        result.error_msg = input.error().message;
        result.error_code = static_cast<std::uint32_t>(input.error().server_code);
        return result;
    }
    result.affected_rows = input->statistics.written_rows;
    try
    {
        for (const auto& block : input->blocks)
        {
            if (result.columns.empty())
                for (const auto& column : block.columns)
                    result.columns.push_back(database::column_metadata{.name = column.name});
            if (block.columns.size() != result.columns.size())
                throw std::invalid_argument("ClickHouse result schema changed");
            for (std::size_t row_index{}; row_index < block.rows(); ++row_index)
            {
                database::row row;
                row.reserve(block.columns.size());
                for (const auto& column : block.columns)
                {
                    auto custom = decoder ? decoder(column.values[row_index], column.type) : std::optional<database::field_value>{};
                    row.push_back(custom ? std::move(*custom) : field(column.values[row_index], column.type));
                }
                result.rows.push_back(std::move(row));
            }
        }
    }
    catch (const std::exception& failure)
    {
        result.rows.clear();
        result.error_msg = failure.what();
    }
    return result;
}
} // namespace cnetmod::clickhouse::orm_detail
#endif
