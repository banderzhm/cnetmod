module cnetmod.application.mongodb_credentials;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

namespace cnetmod::application::mongodb_credentials::detail {
auto fetch_instance_credentials(metadata_transport& transport) -> task<result<aws_credentials>>
{
    constexpr std::string_view disabled{"true"};
    if (environment_value("AWS_EC2_METADATA_DISABLED") == disabled)
        co_return std::unexpected(make_error(error_code::authentication_failed, "AWS instance metadata is disabled"));
    metadata_request token_request;
    token_request.method = metadata_method::put;
    token_request.url = "http://169.254.169.254/latest/api/token";
    token_request.headers = {{"X-aws-ec2-metadata-token-ttl-seconds", "21600"}};
    auto token = co_await transport(std::move(token_request));
    if (!token)
        co_return std::unexpected(token.error());
    if (token->status != 200 || token->body.empty() || token->body.size() > 16384 || token->body.find_first_of("\r\n") != std::string::npos)
        co_return std::unexpected(make_error(error_code::authentication_failed, "IMDSv2 token exchange failed"));
    metadata_request role_request;
    role_request.url = "http://169.254.169.254/latest/meta-data/iam/security-credentials/";
    role_request.headers = {{"X-aws-ec2-metadata-token", token->body}};
    auto role_reply = co_await transport(role_request);
    if (!role_reply)
        co_return std::unexpected(role_reply.error());
    while (role_reply->body.ends_with('\n') || role_reply->body.ends_with('\r'))
        role_reply->body.pop_back();
    if (role_reply->status != 200 || !safe_role_name(role_reply->body))
        co_return std::unexpected(make_error(error_code::authentication_failed, "invalid IMDSv2 role response"));
    role_request.url += detail::percent_encode(role_reply->body);
    auto reply = co_await transport(std::move(role_request));
    if (!reply)
        co_return std::unexpected(reply.error());
    if (reply->status != 200 || reply->body.size() > 1024U * 1024U)
        co_return std::unexpected(make_error(error_code::authentication_failed, "IMDSv2 credential exchange failed"));
    co_return detail::parse_aws_json_credentials(reply->body);
}
} // namespace cnetmod::application::mongodb_credentials::detail

#endif
