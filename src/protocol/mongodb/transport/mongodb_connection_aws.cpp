module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import :error;
import :authentication;
import :aws_credentials;
import :aws_sasl;
import :sasl_reply;
import :connection;

namespace cnetmod::mongodb {
auto connection::authenticate_aws() -> task<result<void>>
{
    result<aws_credentials> credentials = std::unexpected(make_error(error_code::authentication_failed, "AWS credentials not configured"));
    if (options_.aws_provider)
        credentials = co_await options_.aws_provider->get();
    else if (!options_.username.empty() || !options_.password.empty())
        co_return std::unexpected(make_error(error_code::authentication_failed, "AWS credentials are not allowed in client username/password options"));
    else
    {
        auto environment = aws_environment_credentials();
        if (!environment)
            co_return std::unexpected(environment.error());
        if (*environment)
            credentials = std::move(**environment);
    }
    if (!credentials)
        co_return std::unexpected(credentials.error());
    auto nonce = make_aws_client_nonce();
    if (!nonce)
        co_return std::unexpected(nonce.error());
    auto initial = aws_first_message(*nonce);
    if (!initial)
        co_return std::unexpected(initial.error());
    auto first = co_await sasl_start(aws_mechanism_name, std::move(*initial));
    if (!first)
        co_return std::unexpected(first.error());
    if (first->done)
        co_return std::unexpected(make_error(error_code::authentication_failed, "premature AWS SASL completion"));
    const auto timestamp = std::format("{:%Y%m%dT%H%M%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    auto signed_payload = aws_challenge_response(first->payload, *nonce, *credentials, timestamp);
    if (!signed_payload)
        co_return std::unexpected(signed_payload.error());
    auto final = co_await sasl_continue(first->conversation_id, std::move(*signed_payload));
    if (!final)
        co_return std::unexpected(final.error());
    if (!final->done)
        co_return std::unexpected(make_error(error_code::authentication_failed, "AWS SASL did not complete"));
    co_return result<void>{};
}
} // namespace cnetmod::mongodb
