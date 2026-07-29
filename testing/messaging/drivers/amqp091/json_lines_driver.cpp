module;
#include <cnetmod/config.hpp>

module cnetmod.testing.messaging.amqp091_driver;

import std;
import nlohmann.json;
import cnetmod.io.io_context;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import :rabbitmq_operation_executor;

namespace cnetmod::testing::messaging::amqp091_driver {
namespace {

    auto error_response(std::string code, std::string message) -> nlohmann::json
    {
        return {{"contract_version", 1},
            {"status", "error"},
            {"error_code", std::move(code)},
            {"message", std::move(message)}};
    }

    auto process_request(nlohmann::json request) -> nlohmann::json
    {
        if (request.value("contract_version", 0) != 1)
            return error_response("unsupported_contract", "contract_version must be 1");
        if (request.value("protocol", "") != "amqp091")
            return error_response("wrong_protocol", "protocol must be amqp091");

        auto context = make_io_context();
        std::optional<nlohmann::json> response;
        spawn(*context, [&]() -> task<void>
            {
                try
                {
                    auto result = co_await execute_rabbitmq_operation(*context, request);
                    response = nlohmann::json{{"contract_version", 1},
                        {"status", "ok"},
                        {"result", std::move(result)}};
                }
                catch (const std::exception& exception)
                {
                    response = error_response("operation_failed", exception.what());
                }
                catch (...)
                {
                    response = error_response("operation_failed", "unknown C++ exception");
                }
                context->stop();
            }());
        context->run();
        if (!response)
            return error_response("event_loop_stopped", "operation produced no result");
        return std::move(*response);
    }

} // namespace

auto run_json_lines(std::istream& input, std::ostream& output,
    std::ostream& diagnostics) -> int
{
    std::string line;
    if (!std::getline(input, line) || line.empty())
    {
        std::println(diagnostics, "AMQP 0-9-1 driver received no JSON request");
        return 2;
    }
    try
    {
        std::println(output, "{}", process_request(nlohmann::json::parse(line)).dump());
        output.flush();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::println(output, "{}",
            error_response("invalid_json", exception.what()).dump());
        output.flush();
        return 0;
    }
}

} // namespace cnetmod::testing::messaging::amqp091_driver
