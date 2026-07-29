#include <cnetmod/config.hpp>

#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.testing.messaging.kafka_interoperability_driver;

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--json-lines")
    {
        std::fputs("usage: kafka_interop_driver --json-lines\n", stderr);
        return 2;
    }

    std::string request;
    if (!std::getline(std::cin, request) || request.empty())
    {
        std::fputs("kafka_interop_driver: expected one JSON request on stdin\n",
            stderr);
        return 2;
    }
    std::string extra;
    while (std::getline(std::cin, extra))
    {
        if (!extra.empty())
        {
            std::fputs("kafka_interop_driver: expected exactly one JSON line\n",
                stderr);
            return 2;
        }
    }

    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::string response;
    cnetmod::spawn(*context, [&]() -> cnetmod::task<void>
        {
            response = co_await cnetmod::testing::messaging::execute_kafka_interoperability_request(
                *context, std::move(request));
            context->stop();
        }());
    context->run();
    std::cout << response << '\n';
    return 0;
}
