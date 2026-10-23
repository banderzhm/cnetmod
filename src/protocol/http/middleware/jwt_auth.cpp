module cnetmod.protocol.http.middleware.jwt_auth;

import std;
import cnetmod.coro.task;
import cnetmod.json;
import cnetmod.protocol.http;
import cnetmod.protocol.http.semantics;

namespace cnetmod {

namespace {

[[nodiscard]] auto authentication_error(std::string_view message) -> std::string
{
    auto body = json::document::object();
    body["error"] = message;
    return json::write_document(body).value_or(std::string{});
}

} // namespace

auto jwt_auth(jwt_auth_options opts) -> http::middleware_fn
{
    return [opts = std::move(opts)](http::request_context& ctx,
               http::next_fn next) -> task<void>
    {
        auto reject = [&opts, &ctx](jwt_auth_failure failure)
        {
            if (opts.on_failure)
                opts.on_failure(ctx, failure);
            else
                ctx.json(failure.status, authentication_error(failure.message));
        };

        const auto mode = opts.mode_for
            ? opts.mode_for(ctx)
            : endpoint_authentication_mode(ctx, opts.unmatched);
        if (mode == jwt_auth_mode::skip)
        {
            co_await next();
            co_return;
        }

        const bool anonymous_on_invalid = mode == jwt_auth_mode::optional &&
            opts.invalid_optional ==
                invalid_optional_credentials::continue_anonymous;

        const auto authorization = ctx.get_header(opts.header_name);
        if (authorization.empty())
        {
            if (mode == jwt_auth_mode::optional)
            {
                co_await next();
                co_return;
            }
            reject({.message = "missing authorization header",
                .reason = jwt_auth_reason::missing_credentials});
            co_return;
        }

        if (!opts.token_prefix.empty() &&
            !authorization.starts_with(opts.token_prefix))
        {
            if (anonymous_on_invalid)
            {
                co_await next();
                co_return;
            }
            reject({.message = "invalid authorization format",
                .reason = jwt_auth_reason::malformed_credentials});
            co_return;
        }

        const auto token = std::string_view{authorization}.substr(
            opts.token_prefix.size());
        if (token.empty())
        {
            if (anonymous_on_invalid)
            {
                co_await next();
                co_return;
            }
            reject({.message = "invalid authorization format",
                .reason = jwt_auth_reason::malformed_credentials});
            co_return;
        }

        if (opts.authenticate_async)
        {
            const std::string owned_token{token};
            auto result = co_await opts.authenticate_async(ctx, owned_token);
            if (!result)
            {
                if (anonymous_on_invalid &&
                    result.error().status == http::status::unauthorized)
                {
                    co_await next();
                    co_return;
                }
                reject(std::move(result.error()));
                co_return;
            }
        }
        else if (!opts.verify || !opts.verify(token))
        {
            if (anonymous_on_invalid)
            {
                co_await next();
                co_return;
            }
            reject({.message = "invalid or expired token"});
            co_return;
        }

        co_await next();
    };
}

} // namespace cnetmod
