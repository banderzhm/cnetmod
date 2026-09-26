#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.core.socket;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.jwt_auth;

namespace {

/// Declared policy of the endpoint the request matched; nullopt = no route.
using matched_endpoint = std::optional<cnetmod::http::endpoint_metadata>;

auto invoke(cnetmod::jwt_auth_options options,
    std::string_view path, std::string_view authorization,
    std::string_view method = "GET",
    matched_endpoint endpoint = cnetmod::http::endpoint_metadata{})
    -> std::pair<bool, int>
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    cnetmod::http::response response;
    cnetmod::http::header_map headers;
    if (!authorization.empty())
        headers["Authorization"] = std::string{authorization};
    cnetmod::http::route_params params;
    if (endpoint)
    {
        auto described = std::make_shared<cnetmod::http::endpoint>();
        described->pattern = std::string{path};
        described->metadata = std::move(*endpoint);
        params.matched = std::move(described);
    }
    cnetmod::http::request_context request{*io, peer, method, path,
        headers, {}, response, std::move(params)};
    bool next_called = false;
    auto middleware = cnetmod::jwt_auth(std::move(options));
    cnetmod::sync_wait(middleware(request,
        [&]() -> cnetmod::task<void>
        {
            next_called = true;
            co_return;
        }));
    return {next_called, response.status_code()};
}

auto rejecting_authenticator(int& calls)
{
    return [&calls](cnetmod::http::request_context&, std::string_view)
               -> cnetmod::task<std::expected<void, cnetmod::jwt_auth_failure>>
    {
        ++calls;
        co_return std::unexpected(cnetmod::jwt_auth_failure{});
    };
}

auto anonymous() -> cnetmod::http::endpoint_metadata
{
    return cnetmod::http::endpoint_metadata{cnetmod::http::allow_anonymous{}};
}

auto optional() -> cnetmod::http::endpoint_metadata
{
    return cnetmod::http::endpoint_metadata{
        cnetmod::http::optional_authentication{}};
}

} // namespace

TEST(jwt_auth_keeps_synchronous_verifier)
{
    auto [allowed, status] = invoke({
                                        .verify = [](std::string_view token)
                                        {
                                            return token == "good";
                                        },
                                    },
        "/private", "Bearer good");
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);
}

TEST(jwt_auth_uses_coroutine_authenticator)
{
    bool authenticated = false;
    auto [allowed, status] = invoke({
                                        .authenticate_async = [&authenticated](
                                                                  cnetmod::http::request_context&, std::string_view token)
                                            -> cnetmod::task<std::expected<void, cnetmod::jwt_auth_failure>>
                                        {
                                            authenticated = token == "good";
                                            co_return {};
                                        },
                                    },
        "/private", "Bearer good");
    ASSERT_TRUE(authenticated);
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);
}

TEST(jwt_auth_propagates_async_failure)
{
    auto [allowed, status] = invoke({
                                        .authenticate_async = [](
                                                                  cnetmod::http::request_context&, std::string_view)
                                            -> cnetmod::task<std::expected<void, cnetmod::jwt_auth_failure>>
                                        {
                                            co_return std::unexpected(cnetmod::jwt_auth_failure{
                                                .status = cnetmod::http::status::service_unavailable,
                                                .message = "identity unavailable"});
                                        },
                                    },
        "/private", "Bearer good");
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::service_unavailable);
}

TEST(jwt_auth_uses_application_failure_response)
{
    std::string message;
    auto [allowed, status] = invoke({
                                        .authenticate_async = [](
                                                                  cnetmod::http::request_context&, std::string_view)
                                            -> cnetmod::task<std::expected<void, cnetmod::jwt_auth_failure>>
                                        {
                                            co_return std::unexpected(cnetmod::jwt_auth_failure{
                                                .message = "principal unavailable"});
                                        },
                                        .on_failure = [&message](cnetmod::http::request_context& request,
                                                          const cnetmod::jwt_auth_failure& failure)
                                        {
                                            message = failure.message;
                                            request.json(failure.status, R"({"custom":true})");
                                        },
                                    },
        "/private", "Bearer good");
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::unauthorized);
    ASSERT_EQ(message, "principal unavailable");
}

TEST(jwt_auth_skips_anonymous_endpoints_before_authentication)
{
    int calls = 0;
    auto [allowed, status] = invoke({.authenticate_async = rejecting_authenticator(calls)},
        "/login", "Bearer ignored", "POST", anonymous());
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);
    ASSERT_EQ(calls, 0);
}

TEST(jwt_auth_requires_credentials_on_endpoints_without_policy)
{
    auto [allowed, status] = invoke({}, "/login-admin", {});
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::unauthorized);
}

TEST(jwt_auth_lets_unmatched_requests_reach_the_router)
{
    auto [allowed, status] = invoke({}, "/missing", {}, "GET", std::nullopt);
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);

    auto [guarded, guarded_status] = invoke(
        {.unmatched = cnetmod::jwt_auth_mode::required}, "/missing", {}, "GET",
        std::nullopt);
    ASSERT_FALSE(guarded);
    ASSERT_EQ(guarded_status, cnetmod::http::status::unauthorized);
}

TEST(jwt_auth_rejects_empty_bearer_token)
{
    auto [allowed, status] = invoke({
                                        .verify = [](std::string_view)
                                        {
                                            return true;
                                        },
                                    },
        "/private", "Bearer ");
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::unauthorized);
}

TEST(jwt_auth_optional_endpoint_accepts_absent_but_rejects_invalid_credentials)
{
    int calls = 0;
    cnetmod::jwt_auth_options options{
        .authenticate_async = rejecting_authenticator(calls)};
    ASSERT_TRUE(invoke(options, "/shared", {}, "GET", optional()).first);
    const auto [bad_allowed, bad_status] =
        invoke(options, "/shared", "Bearer bad", "GET", optional());
    ASSERT_FALSE(bad_allowed);
    ASSERT_EQ(bad_status, cnetmod::http::status::unauthorized);
    const auto [format_allowed, format_status] =
        invoke(options, "/shared", "Basic abc", "GET", optional());
    ASSERT_FALSE(format_allowed);
    ASSERT_EQ(format_status, cnetmod::http::status::unauthorized);
    ASSERT_EQ(calls, 1);
}

TEST(jwt_auth_optional_endpoint_can_continue_anonymously_on_invalid_credentials)
{
    int calls = 0;
    cnetmod::jwt_auth_options options{
        .invalid_optional = cnetmod::invalid_optional_credentials::continue_anonymous,
        .authenticate_async = rejecting_authenticator(calls),
    };
    ASSERT_TRUE(invoke(options, "/shared", {}, "GET", optional()).first);
    ASSERT_TRUE(invoke(options, "/shared", "Basic abc", "GET", optional()).first);
    ASSERT_TRUE(invoke(options, "/shared", "Bearer ", "GET", optional()).first);
    ASSERT_TRUE(invoke(options, "/shared", "Bearer bad", "GET", optional()).first);
    ASSERT_EQ(calls, 1);
    const auto [allowed, status] = invoke(options, "/shared", {}, "DELETE");
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::unauthorized);
}

TEST(jwt_auth_optional_propagates_service_failure)
{
    auto [allowed, status] = invoke({
        .invalid_optional = cnetmod::invalid_optional_credentials::continue_anonymous,
        .authenticate_async = [](cnetmod::http::request_context&,
                                  std::string_view) -> cnetmod::task<
            std::expected<void, cnetmod::jwt_auth_failure>>
        {
            co_return std::unexpected(cnetmod::jwt_auth_failure{
                .status = cnetmod::http::status::service_unavailable,
                .message = "session service unavailable"});
        },
    }, "/shared", "Bearer good", "GET", optional());
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::service_unavailable);
}

TEST(jwt_auth_request_policy_override_takes_precedence)
{
    auto [allowed, status] = invoke({
        .mode_for = [](const cnetmod::http::request_context&)
        {
            return cnetmod::jwt_auth_mode::skip;
        },
    }, "/health", {});
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);
}

TEST(router_exposes_declared_endpoint_metadata_after_matching)
{
    cnetmod::http::router routes;
    routes.get("/orders/:id",
        [](cnetmod::http::request_context&) -> cnetmod::task<void> { co_return; },
        cnetmod::http::endpoint_metadata{
            cnetmod::http::required_permissions{.all_of = {"orders:read"}},
            cnetmod::http::endpoint_name{"orders.read"}});
    routes.del("/orders/:id",
        [](cnetmod::http::request_context&) -> cnetmod::task<void> { co_return; });

    const auto read = routes.match("GET", "/orders/42");
    ASSERT_TRUE(read.has_value());
    if (read)
    {
        ASSERT_TRUE(read->params.matched != nullptr);
        ASSERT_EQ(read->params.matched->pattern, "/orders/:id");
        ASSERT_EQ(read->params.matched->name, "orders.read");
        const auto* permissions =
            read->params.matched->metadata.find<cnetmod::http::required_permissions>();
        ASSERT_TRUE(permissions != nullptr);
        if (permissions != nullptr)
            ASSERT_EQ(permissions->all_of.front(), "orders:read");
        ASSERT_EQ(read->params.get("id"), "42");
    }
    const auto removed = routes.match("DELETE", "/orders/42");
    ASSERT_TRUE(removed.has_value());
    if (removed)
        ASSERT_TRUE(removed->params.matched->metadata.empty());
    ASSERT_EQ(routes.endpoints().size(), std::size_t{2});
    ASSERT_FALSE(routes.match("GET", "/missing").has_value());

    cnetmod::http::endpoint_metadata replaced{cnetmod::http::endpoint_name{"a"}};
    replaced.add(cnetmod::http::endpoint_name{"b"});
    ASSERT_EQ(replaced.size(), std::size_t{1});
    ASSERT_EQ(replaced.find<cnetmod::http::endpoint_name>()->value, "b");
}

RUN_TESTS()
