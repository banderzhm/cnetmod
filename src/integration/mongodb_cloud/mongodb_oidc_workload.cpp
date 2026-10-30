module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.json;
import cnetmod.protocol.mongodb;

namespace cnetmod::mongodb::cloud {
auto make_oidc_workload_provider(io_context& io, thread_pool& pool, oidc_workload_options options,
    metadata_transport transport) -> result<std::shared_ptr<oidc_token_cache>>
{
    const bool metadata = options.environment == oidc_environment::azure || options.environment == oidc_environment::gcp;
    if (options.environment == oidc_environment::none || options.timeout.count() <= 0 || metadata == options.token_resource.empty())
        return std::unexpected(make_error(error_code::authentication_failed, "invalid OIDC environment, TOKEN_RESOURCE, or timeout"));
    if (!transport)
        transport = make_metadata_transport(io);
    auto callback = [&io, &pool, options, transport](oidc_callback_parameters parameters) -> task<result<oidc_access_token>>
    {
        if (options.environment == oidc_environment::test || options.environment == oidc_environment::kubernetes)
        {
            std::filesystem::path file;
            if (options.environment == oidc_environment::test)
            {
                const auto value = std::getenv("OIDC_TOKEN_FILE");
                if (!value || !*value)
                    co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC_TOKEN_FILE is not configured"));
                file = value;
            }
            else if (const auto value = std::getenv("AZURE_FEDERATED_TOKEN_FILE"); value && *value)
                file = value;
            else if (const auto value = std::getenv("AWS_WEB_IDENTITY_TOKEN_FILE"); value && *value)
                file = value;
            else
                file = "/var/run/secrets/kubernetes.io/serviceaccount/token";
            auto token = co_await blocking_invoke(pool, io, [file]
                {
                    return detail::read_token_file(file);
                });
            if (!token)
                co_return std::unexpected(token.error());
            co_return oidc_access_token{std::move(*token), {}};
        }
        metadata_request request;
        request.timeout = parameters.timeout;
        if (options.environment == oidc_environment::azure)
        {
            request.url = "http://169.254.169.254/metadata/identity/oauth2/token?api-version=2018-02-01&resource=" + detail::percent_encode(options.token_resource);
            if (!parameters.username.empty())
                request.url += "&client_id=" + detail::percent_encode(parameters.username);
            request.headers = {{"Accept", "application/json"}, {"Metadata", "true"}};
        }
        else
        {
            request.url = "http://metadata/computeMetadata/v1/instance/service-accounts/default/identity?audience=" + detail::percent_encode(options.token_resource);
            request.headers = {{"Metadata-Flavor", "Google"}};
        }
        auto reply = co_await transport(std::move(request));
        if (!reply)
            co_return std::unexpected(reply.error());
        if (reply->status != 200 || reply->body.empty() || reply->body.size() > 1024U * 1024U)
            co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC metadata endpoint did not return a valid success response"));
        if (options.environment == oidc_environment::gcp)
            co_return oidc_access_token{std::move(reply->body), {}};
        const auto document = cnetmod::json::parse_document(reply->body);
        if (!document)
            co_return std::unexpected(make_error(error_code::authentication_failed, "invalid Azure identity JSON"));
        try
        {
            oidc_access_token token{document->at("access_token").get<std::string>(), {}};
            if (document->contains("expires_in"))
            {
                const auto& expiration = document->at("expires_in");
                std::string text = expiration.is_string() ? expiration.get<std::string>() : expiration.dump();
                std::int64_t seconds{};
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
                if (error != std::errc{} || end != text.data() + text.size() || seconds < 0)
                    co_return std::unexpected(make_error(error_code::authentication_failed, "invalid Azure token expiry"));
                token.expires_in = std::chrono::seconds{seconds};
            }
            co_return token;
        }
        catch (...)
        {
            co_return std::unexpected(make_error(error_code::authentication_failed, "incomplete Azure identity JSON"));
        }
    };
    return std::make_shared<oidc_token_cache>(io, std::move(callback), oidc_callback_parameters{options.username, options.timeout, 1});
}
} // namespace cnetmod::mongodb::cloud
