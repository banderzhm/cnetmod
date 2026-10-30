module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

namespace cnetmod::mongodb::cloud {
auto configure_cloud_credentials(connection_options& options, io_context& io, thread_pool& pool,
    metadata_transport transport) -> result<void>
{
    if (options.authentication == authentication_mechanism::aws)
    {
        if (!options.username.empty() || !options.password.empty())
            return std::unexpected(make_error(error_code::authentication_failed, "AWS credentials must come from a provider or environment"));
        if (!options.aws_provider)
            options.aws_provider = make_default_aws_provider(io, pool, std::move(transport));
    }
    else if (options.authentication == authentication_mechanism::oidc)
    {
        if (!options.password.empty() || (options.oidc_provider && (options.oidc_application_environment != oidc_environment::none || !options.oidc_token_resource.empty())))
            return std::unexpected(make_error(error_code::authentication_failed, "OIDC callback/environment conflict or forbidden password"));
        if (!options.oidc_provider)
        {
            auto provider = make_oidc_workload_provider(io, pool,
                oidc_workload_options{options.oidc_application_environment, options.oidc_token_resource, options.username}, std::move(transport));
            if (!provider)
                return std::unexpected(provider.error());
            options.oidc_provider = std::move(*provider);
            options.oidc_application_environment = oidc_environment::none;
            options.oidc_token_resource.clear();
        }
    }
    return {};
}
} // namespace cnetmod::mongodb::cloud
