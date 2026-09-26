#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.core.socket;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.authorization;

namespace http = cnetmod::http;

namespace {

using matched_endpoint = std::optional<http::endpoint_metadata>;

struct outcome
{
    bool reached = false;
    int status = http::status::ok;
    int authentications = 0;
};

auto invoke(http::authorization_options options, matched_endpoint endpoint,
    std::optional<http::authorization_principal> principal,
    http::authorization_error_code failure =
        http::authorization_error_code::unauthenticated) -> outcome
{
    outcome result;
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    http::response response;
    http::header_map headers;
    http::route_params params;
    if (endpoint)
    {
        auto described = std::make_shared<http::endpoint>();
        described->pattern = "/resource";
        described->metadata = std::move(*endpoint);
        params.matched = std::move(described);
    }
    http::request_context request{*io, peer, "GET", "/resource", headers, {},
        response, std::move(params)};
    options.authenticate = [&result, principal, failure](http::request_context&)
        -> std::expected<http::authorization_principal, http::authorization_error>
    {
        ++result.authentications;
        if (principal)
            return *principal;
        return std::unexpected(http::authorization_error{.code = failure});
    };
    auto middleware = http::authorize(std::move(options));
    cnetmod::sync_wait(middleware(request,
        [&result]() -> cnetmod::task<void>
        {
            result.reached = true;
            co_return;
        }));
    result.status = response.status_code();
    return result;
}

auto reader() -> http::authorization_principal
{
    return {.subject = "7", .permissions = {"orders:read"}};
}

} // namespace

TEST(authorization_enforces_permissions_declared_on_the_endpoint)
{
    const http::endpoint_metadata guarded{
        http::required_permissions{.all_of = {"orders:read"}}};
    const auto allowed = invoke({}, guarded, reader());
    ASSERT_TRUE(allowed.reached);

    const http::endpoint_metadata writes{
        http::required_permissions{.all_of = {"orders:write"}}};
    const auto denied = invoke({}, writes, reader());
    ASSERT_FALSE(denied.reached);
    ASSERT_EQ(denied.status, http::status::forbidden);
}

TEST(authorization_supports_wildcard_and_any_of_requirements)
{
    const http::endpoint_metadata any{http::required_permissions{
        .any_of = {"orders:write", "orders:*"}}};
    ASSERT_TRUE(invoke({}, any, reader()).reached);
    ASSERT_TRUE(http::permission_matches("orders:*", "orders:read"));
    ASSERT_FALSE(http::permission_matches("orders:read", "orders:write"));
}

TEST(authorization_bypasses_anonymous_endpoints_and_unmatched_requests)
{
    const http::endpoint_metadata anonymous{http::allow_anonymous{}};
    const auto open = invoke({}, anonymous, std::nullopt);
    ASSERT_TRUE(open.reached);
    ASSERT_EQ(open.authentications, 0);

    const auto unmatched = invoke({}, std::nullopt, std::nullopt);
    ASSERT_TRUE(unmatched.reached);
    ASSERT_EQ(unmatched.authentications, 0);

    const auto enforced = invoke({.authorize_unmatched = true}, std::nullopt,
        std::nullopt);
    ASSERT_FALSE(enforced.reached);
    ASSERT_EQ(enforced.status, http::status::unauthorized);
}

TEST(authorization_admits_anonymous_callers_on_optional_endpoints_without_permissions)
{
    const http::endpoint_metadata optional{http::optional_authentication{}};
    ASSERT_TRUE(invoke({}, optional, std::nullopt).reached);

    const http::endpoint_metadata optional_guarded{http::optional_authentication{},
        http::required_permissions{.all_of = {"orders:read"}}};
    const auto rejected = invoke({}, optional_guarded, std::nullopt);
    ASSERT_FALSE(rejected.reached);
    ASSERT_EQ(rejected.status, http::status::unauthorized);
}

TEST(authorization_reports_verifier_failure_as_service_unavailable)
{
    const auto failed = invoke({}, http::endpoint_metadata{}, std::nullopt,
        http::authorization_error_code::verifier_failure);
    ASSERT_FALSE(failed.reached);
    ASSERT_EQ(failed.status, http::status::service_unavailable);
}

TEST(authorization_requirement_override_takes_precedence)
{
    const http::endpoint_metadata declared{
        http::required_permissions{.all_of = {"orders:read"}}};
    const auto denied = invoke({.requirement_for = [](const http::request_context&)
                                   -> std::optional<http::authorization_requirement>
                                   {
                                       return http::authorization_requirement{
                                           .all_of = {"admin:*"}};
                                   }},
        declared, reader());
    ASSERT_FALSE(denied.reached);
    ASSERT_EQ(denied.status, http::status::forbidden);
}

TEST(authorization_uses_the_application_failure_envelope)
{
    int observed_status = 0;
    std::string observed_code;
    const http::endpoint_metadata writes{
        http::required_permissions{.all_of = {"orders:write"}}};
    const auto denied = invoke({.on_failure = [&](http::request_context& request,
                                                int status, std::string_view code)
                                   {
                                       observed_status = status;
                                       observed_code = std::string{code};
                                       request.json(status, R"({"custom":true})");
                                   }},
        writes, reader());
    ASSERT_FALSE(denied.reached);
    ASSERT_EQ(denied.status, http::status::forbidden);
    ASSERT_EQ(observed_status, http::status::forbidden);
    ASSERT_EQ(observed_code, "FORBIDDEN");
}

TEST(authorization_requires_an_authenticator)
{
    bool rejected = false;
    try
    {
        (void)http::authorize({});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    ASSERT_TRUE(rejected);
}

RUN_TESTS()
