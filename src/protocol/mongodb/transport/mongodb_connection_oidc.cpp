module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import :error;
import :bson_document;
import :authentication;
import :oidc_credentials;
import :sasl_reply;
import :connection;

namespace cnetmod::mongodb {
auto connection::authenticate_oidc() -> task<result<void>>
{
    if (!options_.password.empty() || !options_.oidc_provider)
        co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC requires a token provider and no password"));
    if (!reauthenticating_)
        if (const auto speculative = hello_response_.find("speculativeAuthenticate"); speculative && speculative->as_document())
        {
            auto reply = parse_sasl_reply(*speculative->as_document());
            if (reply && reply->done && !oidc_access_token_.empty())
                co_return result<void>{};
        }
    const auto remaining_budget = [&]
    {
        if (reauthenticating_)
            return command_deadline_ ? std::chrono::duration_cast<std::chrono::milliseconds>(*command_deadline_ - std::chrono::steady_clock::now()) : std::chrono::milliseconds{60000};
        return options_.connect_timeout.count() > 0 ? std::min(options_.connect_timeout, std::chrono::milliseconds{60000}) : std::chrono::milliseconds{60000};
    };
    auto token = co_await options_.oidc_provider->get(remaining_budget());
    if (!token)
        co_return std::unexpected(token.error());
    for (unsigned attempt{}; attempt < 2; ++attempt)
    {
        oidc_access_token_ = token->access_token;
        auto payload = encode_bson_document(bson_document{{"jwt", token->access_token}});
        if (!payload)
            co_return std::unexpected(payload.error());
        auto reply = co_await sasl_start(oidc_mechanism_name, std::move(*payload));
        if (reply)
        {
            if (!reply->done)
                co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC one-step SASL did not complete"));
            co_return result<void>{};
        }
        if (attempt != 0 || !token->from_cache || reply.error().server_code != 18)
            co_return std::unexpected(reply.error());
        co_await options_.oidc_provider->invalidate(token->access_token);
        token = co_await options_.oidc_provider->get(remaining_budget());
        if (!token)
            co_return std::unexpected(token.error());
    }
    co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC authentication exhausted retries"));
}
} // namespace cnetmod::mongodb
