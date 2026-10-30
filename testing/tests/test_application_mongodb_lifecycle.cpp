import std;
import cnetmod.application;
import cnetmod.protocol.mongodb;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.observability;

namespace app = cnetmod::application;
namespace mg = cnetmod::mongodb;

namespace {
void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}
} // namespace

int main()
{
    const auto port = std::getenv("CNETMOD_MONGODB_TEST_PORT");
    if (!port)
        return 77;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    app::task_supervisor supervisor{*io};
    cnetmod::cancel_token token;
    app::mongodb_service_options options;
    const auto replica_set = std::getenv("CNETMOD_MONGODB_TEST_REPLICA_SET");
    options.uri = std::format("mongodb://127.0.0.1:{}/?replicaSet={}&compressors=snappy&minPoolSize=2&maxPoolSize=2",
        port, replica_set ? replica_set : "cnetmod-test");
    options.configure_connection = [](mg::connection_options& connection) -> mg::result<void>
    {
        connection.compression_minimum_bytes = 0;
        return {};
    };
    app::mongodb_service service{options, *io, "native", app::service_requirement::required, {}};
    int result = 1;
    auto work = [&]() -> cnetmod::task<void>
    {
        try
        {
            app::service_context context{*io, telemetry, supervisor, token, cnetmod::deadline::after(std::chrono::seconds{10})};
            require((co_await service.start(context)).has_value(), "URI service failed startup");
            require(service.topology_pool() != nullptr, "topology unavailable after startup");
            auto lease = co_await service.acquire();
            require(lease.has_value(), "application acquire failed");
            require((*lease)->capabilities().selected_compressor == mg::compressor_snappy, "Snappy not negotiated through application URI");
            require(service.topology_pool()->statistics().connection_count == 2, "URI minimum pool size was not warmed");
            require((co_await service.command({{"ping", 1}})).has_value(), "application command failed");
            require((co_await service.probe(context)).status == app::service_health::up, "application health failed");

            // Shutdown must retain the borrowed pool and reject new application
            // work until the outstanding lease is returned and cleanup retried.
            app::service_context short_stop{*io, telemetry, supervisor, token, cnetmod::deadline::after(std::chrono::milliseconds{20})};
            auto stopped = co_await service.stop(short_stop);
            require(!stopped && stopped.error() == std::errc::timed_out, "shutdown ignored borrowed lease");
            require(service.cleanup_required(), "pending shutdown lost its ownership state");
            require(!(co_await service.command("admin", {{"ping", 1}})), "command admitted during cleanup");
            require(!(co_await service.start(context)), "restart admitted before cleanup");
            *lease = mg::pooled_connection{};
            context.operation_deadline = cnetmod::deadline::after(std::chrono::seconds{10});
            require((co_await service.stop(context)).has_value(), "shutdown retry failed");
            auto closed = co_await service.topology_pool()->acquire();
            require(!closed && closed.error().code == mg::error_code::connection_closed, "closed topology resurrected a pool");
            require((co_await service.start(context)).has_value(), "URI service restart failed");
            require((co_await service.command("admin", {{"ping", 1}})).has_value(), "command after restart failed");
            require((co_await service.stop(context)).has_value(), "final shutdown failed");
            result = 0;
        }
        catch (const std::exception& error)
        {
            std::println("[FAIL] {}", error.what());
        }
        supervisor.request_stop();
        (void)co_await supervisor.join();
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    if (!result)
        std::println("[PASS] application URI, Snappy, health, lease shutdown and restart");
    return result;
}
