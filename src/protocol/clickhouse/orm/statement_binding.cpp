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
    auto parameter_type(const database::query_parameter& parameter) -> std::pair<std::string, std::string>
    {
        using kind = database::query_parameter::kind_t;
        switch (parameter.kind)
        {
        case kind::null_kind:
            return {"Nullable(String)", "\\N"};
        case kind::int64_kind:
            return {"Int64", std::to_string(parameter.int_val)};
        case kind::uint64_kind:
            return {"UInt64", std::to_string(parameter.uint_val)};
        case kind::double_kind:
        {
            if (!std::isfinite(parameter.double_val))
                throw std::invalid_argument("non-finite SQL parameter");
            std::array<char, 64> buffer{};
            auto [end, code] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), parameter.double_val);
            if (code != std::errc{})
                throw std::invalid_argument("invalid floating SQL parameter");
            return {"Float64", std::string(buffer.data(), end)};
        }
        case kind::string_kind:
        case kind::blob_kind:
            return {"String", parameter.str_val};
        case kind::date_kind:
            return {"Date32", parameter.date_val.to_string()};
        case kind::datetime_kind:
            return {"DateTime64(6, 'UTC')", parameter.datetime_val.to_string()};
        case kind::time_kind:
            return {"String", parameter.time_val.to_string()};
        }
        throw std::invalid_argument("unknown SQL parameter kind");
    }

    // Tokenize SQL syntax rather than replacing text inside literals/comments.
    struct sql_scan
    {
        std::string_view sql;
        std::size_t position{};

        auto quoted_end(std::size_t start, char quote) const -> std::size_t
        {
            auto i = start + 1;
            for (; i < sql.size(); ++i)
            {
                if (sql[i] == '\\')
                {
                    ++i;
                    continue;
                }
                if (sql[i] == quote)
                {
                    if (i + 1 < sql.size() && sql[i + 1] == quote)
                    {
                        ++i;
                        continue;
                    }
                    return i + 1;
                }
            }
            throw std::invalid_argument("unterminated SQL literal");
        }
    };

    auto upper(std::string_view text) -> std::string
    {
        std::string result(text);
        for (auto& c : result)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - ('a' - 'A'));
        return result;
    }

    auto rewrite_statement(std::string sql) -> std::string
    {
        auto start = sql.find_first_not_of(" \t\r\n");
        if (start == sql.npos)
            return sql;
        auto end = sql.find_first_of(" \t\r\n", start);
        auto verb = upper(std::string_view(sql).substr(start, end - start));
        if (verb == "START" || verb == "BEGIN" || verb == "COMMIT" || verb == "ROLLBACK" || verb == "SAVEPOINT")
            throw std::invalid_argument("ClickHouse ORM transactions are not supported");
        if (verb == "UPDATE")
        {
            // ORM-generated UPDATE has a quoted table followed by SET. Keep values
            // server-bound and make mutations synchronous before reporting success.
            sql_scan scan{sql};
            auto at = sql.find_first_not_of(" \t\r\n", end);
            if (at == sql.npos)
                throw std::invalid_argument("missing ORM UPDATE table");
            auto table_end = (sql[at] == '`' || sql[at] == '\"') ? scan.quoted_end(at, sql[at]) : sql.find_first_of(" \t\r\n", at);
            auto set = sql.find_first_not_of(" \t\r\n", table_end);
            if (set == sql.npos || upper(std::string_view(sql).substr(set, 3)) != "SET")
                throw std::invalid_argument("invalid ORM UPDATE");
            return "ALTER TABLE " + sql.substr(at, table_end - at) + " UPDATE " + sql.substr(set + 3) + " SETTINGS mutations_sync=1";
        }
        if (verb == "DELETE")
        {
            auto from = sql.find_first_not_of(" \t\r\n", end);
            if (from == sql.npos || upper(std::string_view(sql).substr(from, 4)) != "FROM")
                throw std::invalid_argument("invalid ORM DELETE");
            auto at = sql.find_first_not_of(" \t\r\n", from + 4);
            if (at == sql.npos)
                throw std::invalid_argument("missing ORM DELETE table");
            sql_scan scan{sql};
            auto table_end = (sql[at] == '`' || sql[at] == '\"') ? scan.quoted_end(at, sql[at]) : sql.find_first_of(" \t\r\n", at);
            const auto suffix = table_end == sql.npos ? std::string{} : sql.substr(table_end);
            return "ALTER TABLE " + sql.substr(at, table_end - at) + " DELETE" + (suffix.empty() ? " WHERE 1" : suffix) + " SETTINGS mutations_sync=1";
        }
        if (verb != "INSERT")
            return sql;
        sql_scan scan{sql};
        std::size_t values = sql.npos;
        for (std::size_t i = end; i < sql.size();)
        {
            if (sql[i] == '\'' || sql[i] == '`' || sql[i] == '\"')
            {
                i = scan.quoted_end(i, sql[i]);
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(sql[i])))
            {
                auto word = i;
                while (i < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_'))
                    ++i;
                if (upper(std::string_view(sql).substr(word, i - word)) == "VALUES")
                {
                    values = word;
                    end = i;
                    break;
                }
            }
            else
                ++i;
        }
        if (values == sql.npos)
            return sql;
        std::string result = sql.substr(0, values);
        auto at = end;
        bool first = true;
        while (at < sql.size())
        {
            at = sql.find_first_not_of(" \t\r\n", at);
            if (at == sql.npos)
                break;
            if (sql[at] != '(')
                throw std::invalid_argument("invalid ORM INSERT VALUES");
            const auto body = ++at;
            unsigned depth = 1;
            while (at < sql.size() && depth)
            {
                if (sql[at] == '\'' || sql[at] == '`' || sql[at] == '\"')
                {
                    at = scan.quoted_end(at, sql[at]);
                    continue;
                }
                if (sql[at] == '(')
                    ++depth;
                else if (sql[at] == ')')
                    --depth;
                if (depth)
                    ++at;
            }
            if (depth)
                throw std::invalid_argument("unbalanced ORM VALUES");
            result += (first ? "SELECT " : " UNION ALL SELECT ") + sql.substr(body, at - body);
            first = false;
            at = sql.find_first_not_of(" \t\r\n", at + 1);
            if (at == sql.npos)
                break;
            if (sql[at] != ',')
                throw std::invalid_argument("unsupported INSERT suffix for ClickHouse");
            ++at;
        }
        return result;
    }
} // namespace

auto bind_statement(database::parameterized_query input) -> std::expected<bound_statement, std::string>
{
    try
    {
        bound_statement result;
        sql_scan scan{input.query};
        std::size_t index{};
        for (std::size_t i{}; i < input.query.size();)
        {
            const auto c = input.query[i];
            if (c == '\'' || c == '`' || c == '\"')
            {
                auto end = scan.quoted_end(i, c);
                result.sql.append(input.query, i, end - i);
                i = end;
                continue;
            }
            if (input.query.compare(i, 2, "--") == 0 || input.query.compare(i, 1, "#") == 0)
            {
                auto end = input.query.find('\n', i);
                if (end == input.query.npos)
                    end = input.query.size();
                result.sql.append(input.query, i, end - i);
                i = end;
                continue;
            }
            if (input.query.compare(i, 2, "/*") == 0)
            {
                auto end = input.query.find("*/", i + 2);
                if (end == input.query.npos)
                    throw std::invalid_argument("unterminated SQL comment");
                end += 2;
                result.sql.append(input.query, i, end - i);
                i = end;
                continue;
            }
            if (input.query.compare(i, 2, "{}") == 0)
            {
                if (index >= input.args.size())
                    throw std::invalid_argument("missing SQL parameter");
                auto [type, value] = parameter_type(input.args[index]);
                auto name = "orm_p" + std::to_string(++index);
                result.sql += "{" + name + ":" + type + "}";
                result.request.parameters.push_back({std::move(name), std::move(value)});
                i += 2;
            }
            else
            {
                result.sql += c;
                ++i;
            }
        }
        if (index != input.args.size())
            throw std::invalid_argument("unused SQL parameter");
        result.sql = rewrite_statement(std::move(result.sql));
        return result;
    }
    catch (const std::exception& failure)
    {
        return std::unexpected(failure.what());
    }
}
} // namespace cnetmod::clickhouse::orm_detail
#endif
