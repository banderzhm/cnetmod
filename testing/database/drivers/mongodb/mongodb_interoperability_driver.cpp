module;

#include <cnetmod/config.hpp>
#include <nlohmann/json.hpp>

module cnetmod.testing.database.mongodb_interoperability_driver;

import std;
import cnetmod.protocol.mongodb;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

namespace cnetmod::testing::database {
namespace {

    using json = nlohmann::json;

    auto success(json result) -> std::string
    {
        return json{{"contract_version", 1}, {"status", "ok"},
            {"result", std::move(result)}}
            .dump();
    }

    auto failure(std::string_view code, std::string message) -> std::string
    {
        return json{{"contract_version", 1}, {"status", "error"},
            {"error_code", code}, {"message", std::move(message)}}
            .dump();
    }

    auto error_name(mongodb::error_code code) -> std::string_view
    {
        using enum mongodb::error_code;
        switch (code)
        {
        case invalid_bson:
            return "invalid_bson";
        case message_too_large:
            return "message_too_large";
        case connection_failed:
            return "connection_failed";
        case tls_failed:
            return "tls_failed";
        case authentication_failed:
            return "authentication_failed";
        case command_failed:
            return "command_failed";
        case connection_closed:
            return "connection_closed";
        case operation_timed_out:
            return "operation_timed_out";
        case operation_cancelled:
            return "operation_cancelled";
        case server_selection_failed:
            return "server_selection_failed";
        case pool_exhausted:
            return "pool_exhausted";
        case transaction_failed:
            return "transaction_failed";
        case change_stream_closed:
            return "change_stream_closed";
        case compression_failed:
            return "compression_failed";
        default:
            return "protocol_error";
        }
    }

    auto options_from_json(const json& parameters) -> mongodb::connection_options
    {
        mongodb::connection_options options;
        options.host = parameters.value("host", std::string("127.0.0.1"));
        options.port = static_cast<std::uint16_t>(parameters.value("port", 27017));
        options.database = parameters.value("database", std::string("admin"));
        options.username = parameters.value("username", std::string{});
        options.password = parameters.value("password", std::string{});
        options.authentication_database = parameters.value(
            "authentication_database", std::string("admin"));
        options.tls = parameters.value("tls", false);
        options.tls_verify = parameters.value("tls_verify", true);
        options.tls_ca_file = parameters.value("tls_ca_file", std::string{});
        options.connect_timeout = std::chrono::milliseconds(
            parameters.value("connect_timeout_milliseconds", 10000));
        options.command_timeout = std::chrono::milliseconds(
            parameters.value("command_timeout_milliseconds", 30000));
        options.enable_zlib_compression = parameters.value("enable_zlib_compression", true);
        options.compression_minimum_bytes = parameters.value(
            "compression_minimum_bytes", std::size_t{1024});
        options.max_message_bytes = 16U * 1024U * 1024U;
        return options;
    }

    auto topology_options_from_json(const json& parameters)
        -> mongodb::topology_connection_pool_options
    {
        mongodb::topology_connection_pool_options topology;
        topology.seeds.clear();
        if (parameters.contains("seeds"))
            for (const auto& seed : parameters.at("seeds"))
            {
                auto parsed = mongodb::parse_server_address(seed.get<std::string>());
                if (!parsed)
                    throw std::invalid_argument(parsed.error().message);
                topology.seeds.push_back(*parsed);
            }
        if (topology.seeds.empty())
            topology.seeds.push_back({parameters.value("host", std::string("127.0.0.1")),
                static_cast<std::uint16_t>(parameters.value("port", 27017))});
        topology.per_server_pool.connection = options_from_json(parameters);
        topology.per_server_pool.minimum_size = 1;
        topology.per_server_pool.maximum_size = 8;
        if (parameters.contains("replica_set_name"))
            topology.replica_set_name = parameters.at("replica_set_name").get<std::string>();
        return topology;
    }

    auto address_text(const mongodb::server_address& address) -> std::string
    {
        if (address.host.contains(':'))
            return std::format("[{}]:{}", address.host, address.port);
        return std::format("{}:{}", address.host, address.port);
    }

    auto returned_marker(const mongodb::bson_document& reply) -> std::string
    {
        const auto* cursor_value = reply.find("cursor");
        if (!cursor_value)
            return {};
        const auto* cursor = cursor_value->as_document();
        if (!cursor)
            return {};
        const auto* batch_value = cursor->find("firstBatch");
        if (!batch_value)
            return {};
        const auto* batch = batch_value->as_array();
        if (!batch || batch->empty())
            return {};
        const auto* document = batch->front().as_document();
        if (!document)
            return {};
        const auto* marker = document->find("marker");
        if (!marker)
            return {};
        if (const auto* text = marker->get_if<std::string>())
            return *text;
        return {};
    }

    auto first_document(const mongodb::bson_document& reply)
        -> const mongodb::bson_document*
    {
        const auto* cursor_value = reply.find("cursor");
        const auto* cursor = cursor_value ? cursor_value->as_document() : nullptr;
        const auto* batch_value = cursor ? cursor->find("firstBatch") : nullptr;
        const auto* batch = batch_value ? batch_value->as_array() : nullptr;
        return batch && !batch->empty() ? batch->front().as_document() : nullptr;
    }

    auto pool_options(mongodb::connection_options connection,
        const json& parameters) -> mongodb::connection_pool_options
    {
        mongodb::connection_pool_options result;
        result.connection = std::move(connection);
        result.minimum_size = parameters.value("pool_minimum_size", std::size_t{0});
        result.maximum_size = parameters.value("pool_maximum_size", std::size_t{4});
        result.maximum_connecting = parameters.value("pool_maximum_connecting", std::size_t{2});
        result.wait_queue_timeout = std::chrono::milliseconds(
            parameters.value("pool_wait_timeout_milliseconds", 250));
        return result;
    }

    auto event_marker(const mongodb::bson_document& event) -> std::string
    {
        const auto* full_value = event.find("fullDocument");
        const auto* full = full_value ? full_value->as_document() : nullptr;
        const auto* marker = full ? full->find("marker") : nullptr;
        if (const auto* text = marker ? marker->get_if<std::string>() : nullptr)
            return *text;
        return {};
    }

} // namespace

auto execute_mongodb_interoperability_request(io_context& context,
    std::string request_json) -> task<std::string>
{
    try
    {
        const auto request = json::parse(request_json);
        if (request.value("contract_version", 0) != 1 ||
            request.value("protocol", std::string{}) != "mongodb")
            co_return failure("invalid_request", "unsupported request contract");

        const auto& parameters = request.at("parameters");
        const auto operation = request.at("operation").get<std::string>();
        auto options = options_from_json(parameters);

        if (operation == "topology_status" || operation == "failover_watch")
        {
            mongodb::topology_connection_pool pool(context,
                topology_options_from_json(parameters));
            auto refreshed = co_await pool.refresh();
            if (!refreshed)
                co_return failure(error_name(refreshed.error().code),
                    refreshed.error().message);
            if (operation == "topology_status")
            {
                json servers = json::array();
                for (const auto& server : pool.topology().snapshot())
                    servers.push_back({{"address", address_text(server.address)},
                        {"kind", static_cast<int>(server.kind)},
                        {"writable", server.writable()}, {"readable", server.readable()}});
                co_return success({{"servers", std::move(servers)},
                    {"topology_kind", static_cast<int>(pool.topology().kind())}});
            }
            const auto duration = std::chrono::milliseconds(
                parameters.value("duration_milliseconds", 30000));
            const auto interval = std::chrono::milliseconds(
                parameters.value("interval_milliseconds", 200));
            const auto deadline = std::chrono::steady_clock::now() + duration;
            std::set<std::string> primaries;
            std::size_t successful_writes{};
            std::size_t transient_failures{};
            std::int64_t sequence{};
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto selected = pool.topology().select_server();
                if (selected)
                    primaries.emplace(address_text(selected->address));
                auto marker = std::format("failover-{}-{}", sequence++,
                    std::chrono::steady_clock::now().time_since_epoch().count());
                mongodb::bson_array documents{mongodb::bson_value{
                    mongodb::bson_document{{"_id", marker}, {"sequence", sequence}}}};
                mongodb::bson_document insert{{"insert", "cnetmod_failover_probe"},
                    {"documents", std::move(documents)}, {"ordered", true},
                    {"writeConcern", mongodb::bson_document{{"w", "majority"}}}};
                auto written = co_await mongodb::execute_retryable_command(pool,
                    parameters.value("database", std::string("admin")), std::move(insert),
                    mongodb::operation_kind::write);
                if (written)
                    ++successful_writes;
                else
                    ++transient_failures;
                co_await async_sleep(context, interval);
            }
            json observed = json::array();
            for (const auto& primary : primaries)
                observed.push_back(primary);
            pool.close();
            co_return success({{"successful_writes", successful_writes},
                {"transient_failures", transient_failures},
                {"observed_primaries", std::move(observed)}});
        }

        if (operation == "timeout_probe")
        {
            mongodb::connection timed(context);
            auto attempt = co_await timed.connect(options);
            if (!attempt)
                co_return failure(error_name(attempt.error().code), attempt.error().message);
            auto command = co_await timed.ping();
            if (!command && command.error().code == mongodb::error_code::operation_timed_out)
                co_return success({{"timed_out", true}, {"connection_closed", !timed.is_open()}});
            if (!command)
                co_return failure(error_name(command.error().code), command.error().message);
            co_return failure("unexpected_success", "MongoDB timeout probe command unexpectedly completed");
        }

        if (operation == "cancel_probe")
        {
            mongodb::connection cancelled(context);
            auto attempt = co_await cancelled.connect(options);
            if (!attempt)
                co_return failure(error_name(attempt.error().code), attempt.error().message);
            const auto delay = std::chrono::milliseconds(
                parameters.value("cancel_after_milliseconds", 100));
            std::jthread canceller([&cancelled, delay](std::stop_token stop)
                {
                    std::this_thread::sleep_for(delay);
                    if (!stop.stop_requested())
                        cancelled.cancel_active_command();
                });
            auto command = co_await cancelled.ping();
            canceller.request_stop();
            if (!command && command.error().code == mongodb::error_code::operation_cancelled)
                co_return success({{"cancelled", true}, {"connection_closed", !cancelled.is_open()}});
            if (!command)
                co_return failure(error_name(command.error().code), command.error().message);
            co_return failure("unexpected_success", "MongoDB cancelled command unexpectedly completed");
        }

        if (operation == "pool_wait_timeout")
        {
            auto configured = pool_options(options, parameters);
            configured.maximum_size = 1;
            configured.maximum_connecting = 1;
            mongodb::connection_pool pool(context, std::move(configured));
            auto first = co_await pool.acquire();
            if (!first)
                co_return failure(error_name(first.error().code), first.error().message);
            const auto started = std::chrono::steady_clock::now();
            auto second = co_await pool.acquire();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            if (second)
                co_return failure("unexpected_success", "exhausted pool granted a second lease");
            if (second.error().code != mongodb::error_code::pool_exhausted)
                co_return failure(error_name(second.error().code), second.error().message);
            co_return success({{"timed_out", true}, {"elapsed_milliseconds", elapsed.count()},
                {"checked_out", pool.checked_out_count()}});
        }

        if (operation == "pool_wait_cancel")
        {
            auto configured = pool_options(options, parameters);
            configured.maximum_size = 1;
            configured.maximum_connecting = 1;
            configured.wait_queue_timeout = std::chrono::seconds{10};
            mongodb::connection_pool pool(context, std::move(configured));
            auto first = co_await pool.acquire();
            if (!first)
                co_return failure(error_name(first.error().code), first.error().message);
            const auto delay = std::chrono::milliseconds(
                parameters.value("cancel_after_milliseconds", 100));
            std::jthread closer([&pool, delay]
                {
                    std::this_thread::sleep_for(delay);
                    pool.close();
                });
            const auto started = std::chrono::steady_clock::now();
            auto waiting = co_await pool.acquire();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            if (waiting)
                co_return failure("unexpected_success", "closed pool granted a waiting lease");
            if (waiting.error().code != mongodb::error_code::connection_closed)
                co_return failure(error_name(waiting.error().code), waiting.error().message);
            co_return success({{"cancelled", true}, {"elapsed_milliseconds", elapsed.count()}});
        }

        if (operation == "pool_wait_targeted_cancel")
        {
            auto configured = pool_options(options, parameters);
            configured.maximum_size = 1;
            configured.maximum_connecting = 1;
            configured.wait_queue_timeout = std::chrono::seconds{10};
            mongodb::connection_pool pool(context, std::move(configured));
            auto first = co_await pool.acquire();
            if (!first)
                co_return failure(error_name(first.error().code), first.error().message);
            std::stop_source cancellation;
            const auto delay = std::chrono::milliseconds(
                parameters.value("cancel_after_milliseconds", 100));
            std::jthread canceller([&cancellation, delay]
                {
                    std::this_thread::sleep_for(delay);
                    cancellation.request_stop();
                });
            const auto started = std::chrono::steady_clock::now();
            auto waiting = co_await pool.acquire(cancellation.get_token());
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            if (waiting)
                co_return failure("unexpected_success", "cancelled waiter received a lease");
            if (waiting.error().code != mongodb::error_code::operation_cancelled)
                co_return failure(error_name(waiting.error().code), waiting.error().message);
            first = {};
            auto healthy = co_await pool.acquire();
            if (!healthy)
                co_return failure(error_name(healthy.error().code), healthy.error().message);
            co_return success({{"cancelled", true}, {"pool_still_open", true},
                {"elapsed_milliseconds", elapsed.count()}});
        }

        if (operation == "bson_types")
        {
            mongodb::connection bson_connection(context);
            auto connected = co_await bson_connection.connect(options);
            if (!connected)
                co_return failure(error_name(connected.error().code), connected.error().message);
            const auto marker = parameters.at("marker").get<std::string>();
            mongodb::bson_object_id object_id;
            for (std::size_t index = 0; index < object_id.bytes.size(); ++index)
                object_id.bytes[index] = static_cast<std::byte>(index + 1);
            mongodb::bson_binary binary{.subtype = 0x80,
                .bytes = {std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}}};
            mongodb::bson_binary legacy_binary{.subtype = 0x02,
                .bytes = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
            mongodb::bson_decimal128 decimal;
            decimal.bytes[14] = std::byte{0x40};
            decimal.bytes[15] = std::byte{0x30};
            auto code_scope = std::make_shared<mongodb::bson_document>(
                mongodb::bson_document{{"answer", std::int32_t{42}}});
            mongodb::bson_document value{{"_id", marker}, {"null", nullptr},
                {"undefined", mongodb::bson_undefined{}},
                {"double", 1.25}, {"text", "MongoDB-文档"},
                {"document", mongodb::bson_document{{"nested", true}}},
                {"array", mongodb::bson_array{mongodb::bson_value{std::int32_t{1}}, mongodb::bson_value{"two"}}}, {"binary", binary},
                {"legacy_binary", legacy_binary},
                {"object_id", object_id}, {"boolean", true},
                {"datetime", mongodb::bson_datetime{1700000000123}},
                {"timestamp", mongodb::bson_timestamp{7, 1700000000}},
                {"regex", mongodb::bson_regex{"^cnetmod", "im"}},
                {"javascript", mongodb::bson_javascript_code{"return 42;"}},
                {"javascript_scope", mongodb::bson_javascript_code_with_scope{"return answer;", std::move(code_scope)}},
                {"symbol", mongodb::bson_symbol{"legacy-symbol"}},
                {"db_pointer", mongodb::bson_db_pointer{"interop.target", object_id}},
                {"decimal128", decimal}, {"min_key", mongodb::bson_min_key{}},
                {"max_key", mongodb::bson_max_key{}}, {"int32", std::int32_t{-123}},
                {"int64", std::int64_t{9223372036854770000LL}}};
            mongodb::bson_document insert{{"insert", "cnetmod_bson_interop"},
                {"documents", mongodb::bson_array{mongodb::bson_value{std::move(value)}}}};
            auto inserted = co_await bson_connection.command(options.database, std::move(insert));
            if (!inserted)
                co_return failure(error_name(inserted.error().code), inserted.error().message);
            mongodb::bson_document find{{"find", "cnetmod_bson_interop"},
                {"filter", mongodb::bson_document{{"_id", marker}}}, {"limit", std::int32_t{1}}};
            auto found = co_await bson_connection.command(options.database, std::move(find));
            if (!found)
                co_return failure(error_name(found.error().code), found.error().message);
            const auto* document = first_document(*found);
            auto has = [document]<class T>(std::string_view key)
            {
                const auto* value = document ? document->find(key) : nullptr;
                return value && value->get_if<T>();
            };
            const bool complete = document && has.template operator()<mongodb::bson_null>("null") &&
                has.template operator()<mongodb::bson_undefined>("undefined") &&
                has.template operator()<double>("double") && has.template operator()<std::string>("text") &&
                document->find("document") && document->find("document")->as_document() &&
                document->find("array") && document->find("array")->as_array() &&
                has.template operator()<mongodb::bson_binary>("binary") &&
                has.template operator()<mongodb::bson_binary>("legacy_binary") &&
                has.template operator()<mongodb::bson_object_id>("object_id") &&
                has.template operator()<bool>("boolean") &&
                has.template operator()<mongodb::bson_datetime>("datetime") &&
                has.template operator()<mongodb::bson_timestamp>("timestamp") &&
                has.template operator()<mongodb::bson_regex>("regex") &&
                has.template operator()<mongodb::bson_javascript_code>("javascript") &&
                has.template operator()<mongodb::bson_javascript_code_with_scope>("javascript_scope") &&
                has.template operator()<mongodb::bson_symbol>("symbol") &&
                has.template operator()<mongodb::bson_db_pointer>("db_pointer") &&
                has.template operator()<mongodb::bson_decimal128>("decimal128") &&
                has.template operator()<mongodb::bson_min_key>("min_key") &&
                has.template operator()<mongodb::bson_max_key>("max_key") &&
                has.template operator()<std::int32_t>("int32") &&
                has.template operator()<std::int64_t>("int64");
            auto cleanup = co_await bson_connection.command(options.database,
                mongodb::bson_document{{"delete", "cnetmod_bson_interop"},
                    {"deletes", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"q", mongodb::bson_document{{"_id", marker}}}, {"limit", std::int32_t{1}}}}}}});
            (void)cleanup;
            co_return success({{"all_types_round_tripped", complete}, {"field_count", document ? document->size() : std::size_t{0}}});
        }

        if (operation == "retryable_read_write")
        {
            mongodb::connection_pool pool(context, pool_options(options, parameters));
            const auto marker = parameters.at("marker").get<std::string>();
            mongodb::bson_document insert{{"insert", "cnetmod_retryable_interop"},
                {"documents", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"_id", marker}, {"marker", marker}}}}},
                {"writeConcern", mongodb::bson_document{{"w", "majority"}}}};
            auto written = co_await mongodb::execute_retryable_command(pool, options.database,
                std::move(insert), mongodb::operation_kind::write);
            if (!written)
                co_return failure(error_name(written.error().code), written.error().message);
            mongodb::bson_document find{{"find", "cnetmod_retryable_interop"},
                {"filter", mongodb::bson_document{{"_id", marker}}}, {"limit", std::int32_t{1}}};
            auto read = co_await mongodb::execute_retryable_command(pool, options.database,
                std::move(find), mongodb::operation_kind::read);
            if (!read)
                co_return failure(error_name(read.error().code), read.error().message);
            co_return success({{"write_ok", true}, {"read_marker", returned_marker(*read)}});
        }

        if (operation == "transaction_commit_abort")
        {
            mongodb::connection_pool pool(context, pool_options(options, parameters));
            const auto committed = parameters.at("committed_marker").get<std::string>();
            const auto aborted = parameters.at("aborted_marker").get<std::string>();
            mongodb::client_session commit_session;
            auto started = commit_session.start_transaction();
            if (!started)
                co_return failure(error_name(started.error().code), started.error().message);
            auto write = co_await commit_session.command(pool, options.database,
                mongodb::bson_document{{"insert", "cnetmod_transaction_interop"},
                    {"documents", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"_id", committed}, {"marker", committed}}}}}});
            if (!write)
                co_return failure(error_name(write.error().code), write.error().message);
            const bool inject_commit_disconnect = parameters.value(
                "inject_commit_disconnect", false);
            auto committed_result = co_await commit_session.commit_transaction(pool);
            if (!committed_result)
                co_return failure(error_name(committed_result.error().code), committed_result.error().message);
            mongodb::client_session abort_session;
            started = abort_session.start_transaction();
            if (!started)
                co_return failure(error_name(started.error().code), started.error().message);
            write = co_await abort_session.command(pool, options.database,
                mongodb::bson_document{{"insert", "cnetmod_transaction_interop"},
                    {"documents", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"_id", aborted}, {"marker", aborted}}}}}});
            if (!write)
                co_return failure(error_name(write.error().code), write.error().message);
            auto aborted_result = co_await abort_session.abort_transaction(pool);
            if (!aborted_result)
                co_return failure(error_name(aborted_result.error().code), aborted_result.error().message);
            co_return success({{"committed", committed}, {"aborted", aborted},
                {"commit_retried", inject_commit_disconnect}});
        }

        if (operation == "change_stream_resume")
        {
            auto configured = pool_options(options, parameters);
            configured.maximum_size = std::max<std::size_t>(2, configured.maximum_size);
            mongodb::connection_pool pool(context, std::move(configured));
            const auto first_marker = parameters.at("first_marker").get<std::string>();
            const auto second_marker = parameters.at("second_marker").get<std::string>();
            mongodb::change_stream stream(pool, options.database, "cnetmod_change_stream_interop");
            auto opened = co_await stream.open();
            if (!opened)
                co_return failure(error_name(opened.error().code), opened.error().message);
            auto writer = co_await pool.acquire();
            if (!writer)
                co_return failure(error_name(writer.error().code), writer.error().message);
            auto written = co_await (*writer)->command(options.database,
                mongodb::bson_document{{"insert", "cnetmod_change_stream_interop"},
                    {"documents", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"marker", first_marker}}}}}});
            if (!written)
                co_return failure(error_name(written.error().code), written.error().message);
            writer = {};
            auto first = co_await stream.next();
            if (!first || !*first || !stream.resume_token())
                co_return failure("change_stream_closed", first ? "change stream produced no event/token" : first.error().message);
            auto resume_token = *stream.resume_token();
            const auto observed_first = event_marker(**first);
            if (parameters.value("inject_get_more_disconnect", false))
            {
                writer = co_await pool.acquire();
                if (!writer)
                    co_return failure(error_name(writer.error().code), writer.error().message);
                written = co_await (*writer)->command(options.database,
                    mongodb::bson_document{{"insert", "cnetmod_change_stream_interop"},
                        {"documents", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"marker", second_marker}}}}}});
                if (!written)
                    co_return failure(error_name(written.error().code), written.error().message);
                writer = {};
                auto automatically_resumed = co_await stream.next();
                if (!automatically_resumed || !*automatically_resumed)
                    co_return failure("change_stream_closed", automatically_resumed ? "automatically resumed stream produced no event" : automatically_resumed.error().message);
                const auto observed_second = event_marker(**automatically_resumed);
                co_await stream.close();
                co_return success({{"first_marker", observed_first},
                    {"resumed_marker", observed_second},
                    {"resume_token_present", true}, {"automatic_resume", true}});
            }
            co_await stream.close();
            writer = co_await pool.acquire();
            if (!writer)
                co_return failure(error_name(writer.error().code), writer.error().message);
            written = co_await (*writer)->command(options.database,
                mongodb::bson_document{{"insert", "cnetmod_change_stream_interop"},
                    {"documents", mongodb::bson_array{mongodb::bson_value{mongodb::bson_document{{"marker", second_marker}}}}}});
            if (!written)
                co_return failure(error_name(written.error().code), written.error().message);
            writer = {};
            mongodb::change_stream_options resume_options;
            resume_options.resume_after = std::move(resume_token);
            mongodb::change_stream resumed(pool, options.database,
                "cnetmod_change_stream_interop", std::move(resume_options));
            opened = co_await resumed.open();
            if (!opened)
                co_return failure(error_name(opened.error().code), opened.error().message);
            auto second = co_await resumed.next();
            if (!second || !*second)
                co_return failure("change_stream_closed", second ? "resumed stream produced no event" : second.error().message);
            const auto observed_second = event_marker(**second);
            co_await resumed.close();
            co_return success({{"first_marker", observed_first},
                {"resumed_marker", observed_second}, {"resume_token_present", true}});
        }

        mongodb::connection connection(context);
        auto connected = co_await connection.connect(options);

        if (operation == "connect_failure")
        {
            if (!connected)
                co_return failure(error_name(connected.error().code),
                    connected.error().message);
            connection.close();
            co_return failure("unexpected_success", "connection unexpectedly succeeded");
        }
        if (operation != "round_trip")
            co_return failure("unsupported_operation", operation);
        if (!connected)
            co_return failure(error_name(connected.error().code),
                connected.error().message);

        auto pinged = co_await connection.ping();
        if (!pinged)
        {
            connection.close();
            co_return failure(error_name(pinged.error().code), pinged.error().message);
        }

        const auto marker = parameters.at("marker").get<std::string>();
        mongodb::bson_array pipeline;
        pipeline.emplace_back(mongodb::bson_document{{"$documents",
            mongodb::bson_array{mongodb::bson_document{{"marker", marker}}}}});
        mongodb::bson_document command{{"aggregate", std::int32_t{1}},
            {"pipeline", std::move(pipeline)},
            {"cursor", mongodb::bson_document{}}};
        auto reply = co_await connection.command(options.database, std::move(command));
        if (!reply)
        {
            connection.close();
            co_return failure(error_name(reply.error().code), reply.error().message);
        }

        const auto& capabilities = connection.capabilities();
        auto result = json{{"marker", returned_marker(*reply)},
            {"ping_ok", true}, {"request_id_correlated", true},
            {"secure_channel", connection.secure_channel()},
            {"maximum_wire_version", capabilities.maximum_wire_version},
            {"writable_primary", capabilities.writable_primary},
            {"compressor", capabilities.selected_compressor ? "zlib" : "none"}};
        connection.close();
        co_return success(std::move(result));
    }
    catch (const std::exception& error)
    {
        co_return failure("invalid_request", error.what());
    }
}

} // namespace cnetmod::testing::database
