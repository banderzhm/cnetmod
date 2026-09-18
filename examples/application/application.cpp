#include <cnetmod/config.hpp>

import std;
import cnetmod.application;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace application = cnetmod::application;
namespace http = cnetmod::http;

auto configure_routes(http::router& routes) -> void
{
    routes.get("/orders", [](http::request_context& request) -> cnetmod::task<void>
        {
            request.json(http::status::ok, R"({"orders":[]})");
            co_return;
        });
}

auto main() -> int
{
    auto host = application::application_builder{"order-service"}
                    .configuration_file("application.yaml")
                    .enable_auto_configuration()
                    .routes(configure_routes)
                    .build();
    if (!host)
        return 1;
    return host->run() ? 0 : 1;
}
