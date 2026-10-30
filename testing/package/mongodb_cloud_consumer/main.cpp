import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;
import cnetmod.integration.mongodb_cloud;

int main()
{
    namespace mg = cnetmod::mongodb;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    auto config = mg::parse_connection_string("mongodb+srv://cluster.example.com/?authMechanism=MONGODB-OIDC&authSource=%24external");
    if (!config || !config->topology.per_server_pool.connection.tls)
        return 1;
    auto& options = config->topology.per_server_pool.connection;
    options.oidc_application_environment = mg::oidc_environment::gcp;
    options.oidc_token_resource = "installed-sdk-audience";
    auto configured = mg::cloud::configure_cloud_credentials(options, *io, pool,
        [](mg::cloud::metadata_request) -> cnetmod::task<mg::result<mg::cloud::metadata_response>>
        {
            co_return mg::cloud::metadata_response{200, "installed-sdk-token"};
        });
    if (!configured)
        return 2;
    int result = 1;
    auto work = [&]() -> cnetmod::task<void>
    {
        const auto token = co_await options.oidc_provider->get();
        if (token && token->access_token == "installed-sdk-token")
            result = 0;
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    return result;
}
