module cnetmod.application.mongodb_credentials;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

namespace cnetmod::application::mongodb_credentials {
auto make_default_aws_provider(io_context& io, thread_pool& pool, metadata_transport transport) -> std::shared_ptr<aws_credential_cache>
{
    if (!transport)
        transport = make_metadata_transport(io);
    // @flow Custom providers are selected by the caller. The default cache reads
    // environment credentials dynamically, then invokes this fallback chain.
    auto callback = [&io, &pool, transport]() mutable -> task<result<aws_credentials>>
    {
        // @step Prefer web identity whenever either required setting is present;
        // a partial configuration fails instead of falling through to another identity.
        const auto file = detail::environment_value("AWS_WEB_IDENTITY_TOKEN_FILE");
        const auto role = detail::environment_value("AWS_ROLE_ARN");
        if (!file.empty() || !role.empty())
            co_return co_await detail::fetch_web_identity(io, pool, transport, file, role);
        // @step A configured container endpoint owns this attempt. EC2 metadata
        // is used only when no container identity was configured.
        auto ecs = detail::ecs_endpoint();
        if (!ecs)
            co_return std::unexpected(ecs.error());
        if (*ecs)
            co_return co_await detail::fetch_ecs_credentials(io, pool, transport, std::move(**ecs));
        co_return co_await detail::fetch_instance_credentials(transport);
    };
    return std::make_shared<aws_credential_cache>(std::move(callback), true);
}
} // namespace cnetmod::application::mongodb_credentials

#endif
