module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
#include "../block/native_block.hpp"
#include "../client/native_session.hpp"
#include "../compression/native_compression.hpp"
#include "../wire/native_buffer.hpp"
#include "../wire/server_packet.hpp"

namespace cnetmod::clickhouse {
using detail::packet;
using detail::parse_packet;
using detail::server_packet;
using detail::transport_error;

namespace {
    auto quote_identifier(std::string_view text) -> std::string
    {
        if (text.empty() || text.find('\0') != text.npos)
            detail::fail("invalid ClickHouse identifier", error_kind::invalid_argument);
        std::string result{"`"};
        for (const char character : text)
        {
            if (character == '`' || character == '\\')
                result += '\\';
            result += character;
        }
        return result + '`';
    }

    auto quote_table(std::string_view text) -> std::string
    {
        const auto dot = text.find('.');
        if (dot == text.npos)
            return quote_identifier(text);
        if (text.find('.', dot + 1) != text.npos)
            detail::fail("table must be table or database.table", error_kind::invalid_argument);
        return quote_identifier(text.substr(0, dot)) + '.' + quote_identifier(text.substr(dot + 1));
    }
} // namespace

auto client::insert(std::string table, block data, std::string id, cancel_token* cancellation) -> task<std::expected<query_result, error>>
{
    cancel_token local;
    auto& token = cancellation ? *cancellation : local;
    try
    {
        impl_->check_cancel(token);
        implementation::operation operation{*impl_};
        if (data.columns.empty())
            detail::fail("ClickHouse insert requires columns", error_kind::invalid_argument);
        detail::writer payload{{}, impl_->options.bounds};
        payload.revision = impl_->revision;
        detail::write_data_packet(payload, data, impl_->options.compression);
        std::string sql = "INSERT INTO " + quote_table(table) + " (";
        for (std::size_t i{}; i < data.columns.size(); ++i)
        {
            if (i)
                sql += ',';
            sql += quote_identifier(data.columns[i].name);
        }
        sql += ") VALUES";
        co_await impl_->send_query(sql, query_options{.query_id = id}, token);
        // INSERT sends its sample schema before accepting any client data.
        for (;;)
        {
            auto response = co_await impl_->receive(token);
            if (response.kind == server_packet::progress || response.kind == server_packet::table_columns || response.kind == server_packet::profile_events || response.kind == server_packet::log)
                continue;
            if (response.kind != server_packet::data)
                detail::fail("expected ClickHouse INSERT sample block");
            if (response.data.columns.size() != data.columns.size())
                detail::fail("INSERT schema column count differs", error_kind::invalid_argument);
            for (std::size_t i{}; i < data.columns.size(); ++i)
                if (response.data.columns[i].name != data.columns[i].name || response.data.columns[i].type != data.columns[i].type)
                    detail::fail("INSERT schema name/type differs", error_kind::invalid_argument);
            break;
        }
        co_await impl_->write(payload.output, token);
        detail::writer end{{}, impl_->options.bounds};
        end.revision = impl_->revision;
        detail::write_data_packet(end, {}, impl_->options.compression);
        co_await impl_->write(end.output, token);
        auto result = co_await impl_->collect(token);
        operation.complete = true;
        co_return result;
    }
    catch (detail::failure& failure)
    {
        co_return std::unexpected(std::move(failure.value));
    }
}
} // namespace cnetmod::clickhouse
