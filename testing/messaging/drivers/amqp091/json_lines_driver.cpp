module;
#include <cnetmod/config.hpp>

module cnetmod.testing.messaging.amqp091_driver;

import std;
import cnetmod.json;
import cnetmod.io.io_context;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import :rabbitmq_operation_executor;

namespace cnetmod::testing::messaging::amqp091_driver {
namespace {

    auto error_response(std::string code, std::string message) -> cnetmod::json::document
    {
        return {{"contract_version", 1},
            {"status", "error"},
            {"error_code", std::move(code)},
            {"message", std::move(message)}};
    }

    auto process_request(cnetmod::json::document request) -> cnetmod::json::document
    {
        if (cnetmod::json::value_or(request, "contract_version", 0) != 1)
            return error_response("unsupported_contract", "contract_version must be 1");
        if (cnetmod::json::value_or(request, "protocol", "") != "amqp091")
            return error_response("wrong_protocol", "protocol must be amqp091");

        auto context = make_io_context();
        std::optional<cnetmod::json::document> response;
        spawn(*context, [&]() -> task<void>
            {
                try
                {
                    auto result = co_await execute_rabbitmq_operation(*context, request);
                    response = cnetmod::json::document{{"contract_version", 1},
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
        const auto parsed = cnetmod::json::parse_document(line);
        if (!parsed)
            throw std::runtime_error("invalid JSON request");
        const auto encoded = cnetmod::json::write_document(process_request(*parsed));
        if (!encoded)
            throw std::runtime_error("failed to encode JSON response");
        std::println(output, "{}", *encoded);
        output.flush();
        return 0;
    }
    catch (const std::exception& exception)
    {
        const auto encoded = cnetmod::json::write_document(
            error_response("invalid_json", exception.what()));
        std::println(output, "{}", encoded.value_or("{}"));
        output.flush();
        return 0;
    }
}

} // namespace cnetmod::testing::messaging::amqp091_driver
