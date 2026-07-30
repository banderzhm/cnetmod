module;

#include <cnetmod/config.hpp>
#include <nlohmann/json.hpp>

module cnetmod.testing.database.postgresql_interoperability_driver;

import std;
import cnetmod.protocol.postgresql;

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

    auto tls_mode_from_name(std::string_view name) -> postgresql::tls_mode
    {
        if (name == "disable")
            return postgresql::tls_mode::disable;
        if (name == "require")
            return postgresql::tls_mode::require;
        if (name == "verify-ca")
            return postgresql::tls_mode::verify_ca;
        if (name == "verify-full")
            return postgresql::tls_mode::verify_full;
        return postgresql::tls_mode::prefer;
    }

    auto connection_options_from_json(const json& parameters)
        -> postgresql::connection_options
    {
        postgresql::connection_options options;
        options.host = parameters.value("host", std::string("localhost"));
        options.port = static_cast<std::uint16_t>(parameters.value("port", 5432));
        options.username = parameters.value("username", std::string("postgres"));
        options.password = parameters.value("password", std::string{});
        options.database = parameters.value("database", std::string("postgres"));
        options.application_name = "cnetmod-postgresql-interoperability";
        options.tls = tls_mode_from_name(
            parameters.value("tls_mode", std::string("prefer")));
        options.tls_ca_file = parameters.value("tls_ca_file", std::string{});
        options.connect_timeout = std::chrono::milliseconds(
            parameters.value("connect_timeout_milliseconds", 10000));
        options.maximum_message_size = 16U * 1024U * 1024U;
        options.maximum_row_count = 1000;
        return options;
    }

    auto first_cell(const postgresql::result_set& result) -> std::string
    {
        if (result.rows.empty() || result.rows.front().empty())
            return {};
        return result.rows.front().front().to_string();
    }

} // namespace

auto execute_postgresql_interoperability_request(io_context& context,
    std::string request_json) -> task<std::string>
{
    try
    {
        const auto request = json::parse(request_json);
        if (request.value("contract_version", 0) != 1 ||
            request.value("protocol", std::string{}) != "postgresql")
            co_return failure("invalid_request", "unsupported request contract");

        const auto& parameters = request.at("parameters");
        const auto operation = request.at("operation").get<std::string>();
        postgresql::client connection(context);
        auto connected = co_await connection.connect(
            connection_options_from_json(parameters));

        if (operation == "connect_failure")
        {
            if (connected.is_err())
                co_return failure("connection_failed", connected.error_msg);
            co_await connection.terminate();
            co_return failure("unexpected_success", "connection unexpectedly succeeded");
        }
        if (operation != "round_trip")
            co_return failure("unsupported_operation", operation);
        if (connected.is_err())
            co_return failure("connection_failed", connected.error_msg);

        auto prepared = co_await connection.prepare(
            "SELECT $1::text AS marker");
        if (!prepared)
        {
            co_await connection.terminate();
            co_return failure("prepare_failed", prepared.error());
        }
        const auto marker = parameters.at("marker").get<std::string>();
        std::array values{postgresql::param_value::from_string(marker)};
        auto marker_result = co_await connection.execute(*prepared, values);
        (void)co_await connection.close_statement(*prepared);
        if (marker_result.is_err())
        {
            co_await connection.terminate();
            co_return failure("query_failed", marker_result.error_msg);
        }

        auto version_result = co_await connection.query(
            "SELECT current_setting('server_version_num')");
        if (version_result.is_err())
        {
            co_await connection.terminate();
            co_return failure("query_failed", version_result.error_msg);
        }

        auto result = json{{"marker", first_cell(marker_result)},
            {"server_version_number", first_cell(version_result)},
            {"ready_for_query", connection.is_open()},
            {"secure_channel", connection.secure_channel()},
            {"backend_process_id", connection.backend_process_id()}};
        co_await connection.terminate();
        co_return success(std::move(result));
    }
    catch (const std::exception& error)
    {
        co_return failure("invalid_request", error.what());
    }
}

} // namespace cnetmod::testing::database
