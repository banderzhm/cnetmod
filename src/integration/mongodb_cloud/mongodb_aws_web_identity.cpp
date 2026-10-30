module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

namespace cnetmod::mongodb::cloud::detail {
auto environment_value(std::string_view key) -> std::string
{
    const auto value = std::getenv(std::string(key).c_str());
    return value ? value : "";
}

auto safe_role_name(std::string_view name) -> bool
{
    return !name.empty() && name.size() <= 256 && std::ranges::all_of(name, [](unsigned char c)
                                                      {
                                                          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+' || c == '=' || c == ',' || c == '.' || c == '@';
                                                      });
}

auto fetch_web_identity(io_context& io, thread_pool& pool, metadata_transport& transport, std::string token_file, std::string role)
    -> task<result<aws_credentials>>
{
    if (token_file.empty() || role.empty())
        co_return std::unexpected(make_error(error_code::authentication_failed, "incomplete AWS web identity configuration"));
    auto token = co_await blocking_invoke(pool, io, [token_file]
        {
            return detail::read_token_file(token_file);
        });
    if (!token)
        co_return std::unexpected(token.error());
    auto session = environment_value("AWS_ROLE_SESSION_NAME");
    if (session.empty())
        session = "cnetmod-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (!safe_role_name(session))
        co_return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS role session name"));
    metadata_request request;
    request.method = metadata_method::post;
    auto region = environment_value("AWS_REGION");
    if (region.empty())
        region = environment_value("AWS_DEFAULT_REGION");
    if (region.empty())
        request.url = "https://sts.amazonaws.com/";
    else
    {
        if (region.size() > 64 || !std::ranges::all_of(region, [](unsigned char c)
                                      {
                                          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
                                      }))
            co_return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS STS region"));
        request.url = "https://sts." + region + (region.starts_with("cn-") ? ".amazonaws.com.cn/" : ".amazonaws.com/");
    }
    request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    request.body = "Action=AssumeRoleWithWebIdentity&Version=2011-06-15&RoleArn=" + detail::percent_encode(role) +
        "&RoleSessionName=" + detail::percent_encode(session) + "&WebIdentityToken=" + detail::percent_encode(*token);
    auto reply = co_await transport(std::move(request));
    if (!reply)
        co_return std::unexpected(reply.error());
    if (reply->status != 200 || reply->body.size() > 1024U * 1024U)
        co_return std::unexpected(make_error(error_code::authentication_failed, "STS web identity exchange failed"));
    co_return detail::parse_aws_sts_credentials(reply->body);
}
} // namespace cnetmod::mongodb::cloud::detail
