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

auto client::implementation::send_query(std::string_view sql, const query_options& request, cancel_token& token) -> task<void>
{
    if (!connected)
        detail::fail("ClickHouse client is not connected", error_kind::invalid_argument);
    if (sql.empty())
        detail::fail("ClickHouse query is empty", error_kind::invalid_argument);
    detail::writer output{{}, options.bounds};
    output.varint(1);
    output.string(request.query_id);
    output.revision = revision;
    output.fixed(1, 1); // Initial query, TCP ClientInfo at negotiated revision.
    output.string("");
    output.string("");
    output.string("[::ffff:127.0.0.1]:0");
    if (revision >= 54449)
        output.fixed(0, 8);
    output.fixed(1, 1);
    output.string("");
    output.string("");
    output.string("cnetmod");
    output.varint(2);
    output.varint(2);
    output.varint(revision);
    output.string(""); // Quota key.
    if (revision >= 54448)
        output.varint(0); // Distributed depth.
    output.varint(0);     // Client version patch.
    if (revision >= 54442)
        output.fixed(0, 1); // No trace context.
    if (revision >= 54453)
    {
        output.varint(0);
        output.varint(0);
        output.varint(0);
    }
    if (revision < 54429 && (!request.settings.empty() || !request.parameters.empty()))
        detail::fail("query settings/parameters require modern Native revision", error_kind::invalid_argument);
    for (const auto& setting : request.settings)
    {
        if (setting.name.empty())
            detail::fail("empty ClickHouse setting name", error_kind::invalid_argument);
        output.string(setting.name);
        output.varint(setting.important ? 1 : 0);
        output.string(setting.value);
    }
    output.string("");
    if (revision >= 54441)
        output.string(""); // Interserver secret, unused by clients.
    output.varint(2);
    compression_active = options.compression != compression_mode::none;
    output.varint(compression_active ? 1 : 0);
    output.string(sql);
    if (revision >= 54459)
    {
        for (const auto& parameter : request.parameters)
        {
            if (parameter.name.empty())
                detail::fail("empty ClickHouse parameter name", error_kind::invalid_argument);
            std::string quoted{"'"};
            for (const char character : parameter.value)
            {
                if (character == '\'' || character == '\\')
                    quoted += '\\';
                quoted += character;
            }
            quoted += '\'';
            output.string(parameter.name);
            output.varint(2);
            output.string(quoted);
        }
        output.string("");
    }
    for (const auto& table : request.external_tables)
    {
        if (table.name.empty())
            detail::fail("empty external table name", error_kind::invalid_argument);
        detail::write_data_packet(output, table.data, options.compression, table.name);
    }
    detail::write_data_packet(output, {}, options.compression); // External tables end.
    co_await write(output.output, token);
}

auto client::implementation::collect(cancel_token& token, const block_sink& sink) -> task<query_result>
{
    query_result result;
    std::size_t retained_rows{}, retained_values{}, retained_bytes{};
    for (;;)
    {
        auto response = co_await receive(token);
        const bool retain = (response.kind == server_packet::data && !sink) ||
            response.kind == server_packet::totals || response.kind == server_packet::extremes ||
            response.kind == server_packet::log || response.kind == server_packet::profile_events;
        if (retain)
        {
            if (response.data.rows() > options.bounds.max_rows - retained_rows ||
                response.decoded_bytes > options.bounds.max_packet_bytes - retained_bytes ||
                response.value_count > options.bounds.max_values - retained_values)
                detail::fail("buffered ClickHouse result exceeds aggregate limits");
            retained_rows += response.data.rows();
            retained_bytes += response.decoded_bytes;
            retained_values += response.value_count;
        }
        switch (response.kind)
        {
        case server_packet::end:
            co_return result;
        case server_packet::data:
            if (response.data.columns.empty())
                break;
            if (sink)
                co_await sink(response.data);
            else
            {
                result.blocks.push_back(std::move(response.data));
            }
            break;
        case server_packet::totals:
            result.totals = std::move(response.data);
            break;
        case server_packet::extremes:
            result.extremes = std::move(response.data);
            break;
        case server_packet::log:
            if (result.logs.size() >= options.bounds.max_columns)
                detail::fail("too many Native log blocks");
            result.logs.push_back(std::move(response.data));
            break;
        case server_packet::profile_events:
            if (result.profile_events.size() >= options.bounds.max_columns)
                detail::fail("too many Native profile event blocks");
            result.profile_events.push_back(std::move(response.data));
            break;
        case server_packet::table_columns:
        case server_packet::part_uuids:
            break;
        case server_packet::profile:
            result.profile = response.profile;
            break;
        case server_packet::progress:
            if (response.statistics.rows > std::numeric_limits<std::uint64_t>::max() - result.statistics.rows ||
                response.statistics.bytes > std::numeric_limits<std::uint64_t>::max() - result.statistics.bytes ||
                response.statistics.written_rows > std::numeric_limits<std::uint64_t>::max() - result.statistics.written_rows ||
                response.statistics.written_bytes > std::numeric_limits<std::uint64_t>::max() - result.statistics.written_bytes)
                detail::fail("ClickHouse progress counter overflow");
            result.statistics.written_rows += response.statistics.written_rows;
            result.statistics.written_bytes += response.statistics.written_bytes;
            result.statistics.rows += response.statistics.rows;
            result.statistics.bytes += response.statistics.bytes;
            result.statistics.total_rows = std::max(result.statistics.total_rows, response.statistics.total_rows);
            break;
        default:
            detail::fail("unexpected packet during ClickHouse query");
        }
    }
}

auto client::query(std::string sql, std::string id, cancel_token* cancellation) -> task<std::expected<query_result, error>>
{
    return query_blocks(std::move(sql), {}, std::move(id), cancellation);
}

auto client::query_blocks(std::string sql, block_sink sink, std::string id, cancel_token* cancellation) -> task<std::expected<query_result, error>>
{
    cancel_token local;
    auto& token = cancellation ? *cancellation : local;
    try
    {
        impl_->check_cancel(token);
        implementation::operation operation{*impl_};
        co_await impl_->send_query(sql, query_options{.query_id = id}, token);
        impl_->querying = true;
        auto result = co_await impl_->collect(token, sink);
        operation.complete = true;
        co_return result;
    }
    catch (detail::failure& failure)
    {
        co_return std::unexpected(std::move(failure.value));
    }
}

auto client::query(std::string sql, query_options request, cancel_token* cancellation)
    -> task<std::expected<query_result, error>>
{
    cancel_token local;
    auto& token = cancellation ? *cancellation : local;
    try
    {
        impl_->check_cancel(token);
        implementation::operation operation{*impl_};
        co_await impl_->send_query(sql, request, token);
        impl_->querying = true;
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
