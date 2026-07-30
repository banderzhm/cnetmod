#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.testing.database.mongodb_interoperability_driver;

auto main(int argument_count, char** arguments) -> int
{
    if (argument_count != 2 || std::string_view(arguments[1]) != "--json-lines")
        return 2;

    std::string request;
    std::array<char, 4096> buffer{};
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), stdin))
        request.append(buffer.data(), count);
    while (!request.empty() && (request.back() == '\n' || request.back() == '\r'))
        request.pop_back();
    if (request.empty() || request.find('\n') != std::string::npos)
        return 2;

    cnetmod::net_init network;
    auto context = cnetmod::make_io_context();
    std::string response;
    cnetmod::spawn(*context, [&]() -> cnetmod::task<void>
        {
            response = co_await cnetmod::testing::database::
                execute_mongodb_interoperability_request(*context, std::move(request));
            context->stop();
        }());
    context->run();
    response.push_back('\n');
    return std::fwrite(response.data(), 1, response.size(), stdout) == response.size()
        ? 0
        : 3;
}
