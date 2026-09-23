#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.core.socket;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.jwt_auth;

namespace {

auto invoke(cnetmod::jwt_auth_options options,
    std::string_view path, std::string_view authorization)
    -> std::pair<bool, int>
{
    auto io = cnetmod::make_io_context();
    cnetmod::socket peer;
    cnetmod::http::response response;
    cnetmod::http::header_map headers;
    if (!authorization.empty())
        headers["Authorization"] = std::string{authorization};
    cnetmod::http::request_context request{*io, peer, "GET", path,
        headers, {}, response, {}};
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

TEST(jwt_auth_skips_public_path_before_authentication)
{
    auto [allowed, status] = invoke({
                                        .skip_paths = {"/login"},
                                    },
        "/login", {});
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);
}

TEST(jwt_auth_skips_public_path_descendants)
{
    auto [allowed, status] = invoke({
                                        .skip_paths = {"/login"},
                                    },
        "/login/help", {});
    ASSERT_TRUE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::ok);
}

TEST(jwt_auth_does_not_skip_a_similar_private_path)
{
    auto [allowed, status] = invoke({
                                        .skip_paths = {"/login"},
                                    },
        "/login-admin", {});
    ASSERT_FALSE(allowed);
    ASSERT_EQ(status, cnetmod::http::status::unauthorized);
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

RUN_TESTS()
