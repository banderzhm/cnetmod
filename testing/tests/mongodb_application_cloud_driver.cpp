import std;
import cnetmod.application;
import cnetmod.protocol.mongodb;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.observability;
import cnetmod.json;
import cnetmod.protocol.http;
#include "mongodb_cloud_test_certificate.hpp"

int main(int argc, char** argv)
{
    if (argc == 4 && std::string_view(argv[1]) == "--write-test-certificate")
        return write_mongodb_cloud_test_certificate(argv[2], argv[3]) ? 0 : 1;
    if (argc != 5)
        return 2;
    namespace app = cnetmod::application;
    namespace mg = cnetmod::mongodb;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    app::task_supervisor supervisor{*io};
    cnetmod::cancel_token cancellation;
    const std::string mode{argv[4]};
    const auto mechanism = mode == "aws" ? "MONGODB-AWS" : (mode == "x509" ? "MONGODB-X509" : "MONGODB-OIDC");
    app::mongodb_service_options options;
    options.uri = std::format("mongodb://127.0.0.1:{}/?tls=true&authMechanism={}&authSource=%24external&connectTimeoutMS=3000&socketTimeoutMS=3000", argv[1], mechanism);
    if (mode == "oidc")
        options.uri += "&authMechanismProperties=ENVIRONMENT:test";
    app::configured_service configuration{.name = "mongodb", .instance = "cloud", .enabled = true};
    configuration.properties = cnetmod::json::object({{"uri", options.uri}, {"tls_ca_file", argv[2]}, {"tls_sni", "localhost"}});
    if (mode == "x509")
    {
        configuration.properties["tls_cert_file"] = argv[2];
        configuration.properties["tls_key_file"] = argv[3];
    }
    app::service_registry registry;
    cnetmod::http::router routes;
    std::array<cnetmod::io_context*, 1> loops{io.get()};
    app::auto_configuration_context composition{*io, loops, telemetry, supervisor, registry, routes};
    if (!app::auto_configure_mongodb(configuration, composition))
        return 3;
    auto& service = *registry.find<app::mongodb_service>("cloud");
    int result = 1;
    auto work = [&]() -> cnetmod::task<void>
    {
        app::service_context context{*io, telemetry, supervisor, cancellation, cnetmod::deadline::after(std::chrono::seconds{10})};
        try
        {
            auto started = co_await service.start(context);
            if (!started)
                throw std::runtime_error("application cloud startup failed: " + started.error().message());
            if (!(co_await service.command("admin", {{"ping", 1}})))
                throw std::runtime_error("application cloud command failed");
            if ((co_await service.probe(context)).status != app::service_health::up)
                throw std::runtime_error("application cloud health failed");
            result = 0;
        }
        catch (const std::exception& error)
        {
            std::println("[FAIL] {}", error.what());
        }
        if (!(co_await service.stop(context)))
            result = 1;
        supervisor.request_stop();
        (void)co_await supervisor.join();
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    return result;
}
