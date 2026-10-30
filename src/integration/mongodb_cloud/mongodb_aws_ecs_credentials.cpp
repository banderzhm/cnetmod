module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

namespace cnetmod::mongodb::cloud::detail {
auto ecs_endpoint() -> result<std::optional<std::string>>
{
    const auto relative = environment_value("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    if (!relative.empty())
    {
        if (!relative.starts_with('/') || relative.starts_with("//") || relative.find_first_of("\r\n#") != std::string::npos)
            return std::unexpected(make_error(error_code::authentication_failed, "invalid ECS relative credential URI"));
        return std::optional<std::string>{"http://169.254.170.2" + relative};
    }
    const auto full = environment_value("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    if (full.empty())
        return std::optional<std::string>{};
    const auto https = full.starts_with("https://");
    if (!https && !full.starts_with("http://"))
        return std::unexpected(make_error(error_code::authentication_failed, "invalid ECS credential URI scheme"));
    const std::string_view url{full};
    const auto authority = url.substr(https ? 8 : 7, url.find('/', https ? 8 : 7) - (https ? 8 : 7));
    if (authority.empty() || authority.find('@') != std::string_view::npos || full.find_first_of("\r\n#") != std::string::npos)
        return std::unexpected(make_error(error_code::authentication_failed, "invalid ECS credential URI authority"));
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    constexpr std::array<std::string_view, 4> allowed{"localhost", "127.0.0.1", "169.254.170.2", "169.254.170.23"};
    if (!https && std::ranges::find(allowed, host) == allowed.end())
        return std::unexpected(make_error(error_code::authentication_failed, "untrusted HTTP ECS credential host"));
    return std::optional<std::string>{full};
}

auto fetch_ecs_credentials(io_context& io, thread_pool& pool, metadata_transport& transport, std::string endpoint)
    -> task<result<aws_credentials>>
{
    metadata_request request;
    request.url = std::move(endpoint);
    auto authorization = environment_value("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    if (const auto file = environment_value("AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE"); !file.empty())
    {
        auto value = co_await blocking_invoke(pool, io, [file]
            {
                return detail::read_token_file(file);
            });
        if (!value)
            co_return std::unexpected(value.error());
        authorization = std::move(*value);
    }
    if (!authorization.empty())
        request.headers.emplace("Authorization", std::move(authorization));
    auto reply = co_await transport(std::move(request));
    if (!reply)
        co_return std::unexpected(reply.error());
    if (reply->status != 200 || reply->body.size() > 1024U * 1024U)
        co_return std::unexpected(make_error(error_code::authentication_failed, "ECS credential exchange failed"));
    co_return detail::parse_aws_json_credentials(reply->body);
}
} // namespace cnetmod::mongodb::cloud::detail
