module cnetmod.application.mongodb_credentials;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

namespace cnetmod::application::mongodb_credentials {
using mongodb::authentication_mechanism;

auto configure_cloud_credentials(mongodb::connection_options& options, io_context& io,
    thread_pool& pool, metadata_transport transport) -> mongodb::result<void>
{
    if (options.authentication == authentication_mechanism::aws)
    {
        if (!options.username.empty() || !options.password.empty())
            return std::unexpected(mongodb::make_error(mongodb::error_code::authentication_failed,
                "AWS credentials must come from a provider or environment"));
        if (!options.aws_provider)
            options.aws_provider = make_default_aws_provider(io, pool, std::move(transport));
    }
    else if (options.authentication == authentication_mechanism::oidc)
    {
        if (!options.password.empty() || (options.oidc_provider && (options.oidc_application_environment != mongodb::oidc_environment::none || !options.oidc_token_resource.empty())))
            return std::unexpected(mongodb::make_error(mongodb::error_code::authentication_failed,
                "OIDC callback/environment conflict or forbidden password"));
        if (!options.oidc_provider)
        {
            auto provider = make_oidc_workload_provider(io, pool,
                oidc_workload_options{options.oidc_application_environment, options.oidc_token_resource, options.username}, std::move(transport));
            if (!provider)
                return std::unexpected(provider.error());
            options.oidc_provider = std::move(*provider);
            options.oidc_application_environment = mongodb::oidc_environment::none;
            options.oidc_token_resource.clear();
        }
    }
    return {};
}
} // namespace cnetmod::application::mongodb_credentials

#endif
