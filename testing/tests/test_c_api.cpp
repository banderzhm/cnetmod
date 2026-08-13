#include "test_framework.hpp"
#include <cnetmod/c_api.h>

import std;
import cnetmod.core;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.http;

namespace {

struct c_api_completion_state
{
    cnetmod_runtime* runtime{};
    cnetmod_http_response* response{};
    int error_code{};
    std::string error_message;
    bool completed{};
};

void record_completion(void* raw, cnetmod_http_response* response, int error_code,
    const char* error_message)
{
    auto& state = *static_cast<c_api_completion_state*>(raw);
    state.response = response;
    state.error_code = error_code;
    if (error_message)
        state.error_message = error_message;
    state.completed = true;
    cnetmod_runtime_stop(state.runtime);
}

} // namespace

TEST(c_api_exposes_defaults_and_null_handle_safety)
{
    cnetmod_http_client_options options{};
    cnetmod_http_client_options_default(&options);
    ASSERT_EQ(options.struct_size, sizeof(cnetmod_http_client_options));
    ASSERT_EQ(options.request_timeout_ms, 30000U);

    ASSERT_EQ(cnetmod_runtime_poll(nullptr), 0U);
    ASSERT_EQ(cnetmod_runtime_run_one(nullptr), 0U);
    ASSERT_TRUE(cnetmod_http_client_create(nullptr, &options) == nullptr);
    ASSERT_TRUE(cnetmod_http_request_start(nullptr, CNETMOD_HTTP_GET,
                    "http://127.0.0.1/", nullptr, 0U, nullptr, nullptr) == nullptr);

    cnetmod_http_request_cancel(nullptr);
    cnetmod_http_request_destroy(nullptr);
    cnetmod_http_client_destroy(nullptr);
    cnetmod_runtime_destroy(nullptr);

    auto* runtime = cnetmod_runtime_create();
    ASSERT_TRUE(runtime != nullptr);
    auto* client = cnetmod_http_client_create(runtime, &options);
    ASSERT_TRUE(client != nullptr);
    ASSERT_TRUE(cnetmod_http_request_start(client, CNETMOD_HTTP_GET, "not a valid uri", nullptr, 0U, +[](void*, cnetmod_http_response*, int, const char*) {}, nullptr) == nullptr);
    ASSERT_EQ(cnetmod_runtime_poll(runtime), 0U);
    cnetmod_http_client_destroy(client);
    cnetmod_runtime_destroy(runtime);
}

TEST(c_api_gets_a_real_loopback_http_response)
{
    constexpr std::uint16_t port = 19432;
    auto server_context = cnetmod::make_io_context();
    cnetmod::net_init network;
    cnetmod::http::router routes;
    routes.get("/c-api", [](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            request.text(cnetmod::http::status::ok, "c-api-e2e");
            co_return;
        });
    cnetmod::http::server server{*server_context};
    server.set_router(std::move(routes));
    ASSERT_TRUE(server.listen("127.0.0.1", port).has_value());

    std::thread server_thread{[&]
        {
            cnetmod::spawn(*server_context, server.run());
            server_context->run();
        }};

    auto* runtime = cnetmod_runtime_create();
    ASSERT_TRUE(runtime != nullptr);
    cnetmod_http_client_options options{};
    cnetmod_http_client_options_default(&options);
    options.version_preference = CNETMOD_HTTP_1_ONLY;
    auto* client = cnetmod_http_client_create(runtime, &options);
    ASSERT_TRUE(client != nullptr);

    c_api_completion_state completion{.runtime = runtime};
    const auto url = "http://127.0.0.1:" + std::to_string(port) + "/c-api";
    auto* pending = cnetmod_http_request_start(client, CNETMOD_HTTP_GET,
        url.c_str(), nullptr, 0U, record_completion, &completion);
    ASSERT_TRUE(pending != nullptr);
    while (!completion.completed)
    {
        cnetmod_runtime_run_one(runtime);
        if (!completion.completed)
            cnetmod_runtime_restart(runtime);
    }

    ASSERT_EQ(completion.error_code, 0);
    ASSERT_TRUE(completion.response != nullptr);
    ASSERT_EQ(cnetmod_http_response_status(completion.response), 200);
    size_t body_size{};
    const auto* body = cnetmod_http_response_body(completion.response, &body_size);
    const std::string_view response_body{reinterpret_cast<const char*>(body), body_size};
    ASSERT_EQ(response_body, "c-api-e2e");

    cnetmod_http_response_free(completion.response);
    cnetmod_http_request_destroy(pending);
    cnetmod_http_client_destroy(client);
    cnetmod_runtime_destroy(runtime);
    server.stop();
    server_context->stop();
    server_thread.join();
}

TEST(c_api_cross_thread_cancel_aborts_an_inflight_http_request)
{
    constexpr std::uint16_t port = 19433;
    auto server_context = cnetmod::make_io_context();
    cnetmod::net_init network;
    cnetmod::http::router routes;
    routes.get("/slow", [&context = *server_context](cnetmod::http::request_context&) -> cnetmod::task<void>
        {
            // Keep the server-side request alive long enough for the client
            // cancellation path to reach a real pending TCP read.
            co_await cnetmod::async_sleep(context, std::chrono::seconds{5});
            co_return;
        });
    cnetmod::http::server server{*server_context};
    server.set_router(std::move(routes));
    ASSERT_TRUE(server.listen("127.0.0.1", port).has_value());
    std::thread server_thread{[&]
        {
            cnetmod::spawn(*server_context, server.run());
            server_context->run();
        }};

    auto* runtime = cnetmod_runtime_create();
    ASSERT_TRUE(runtime != nullptr);
    cnetmod_http_client_options options{};
    cnetmod_http_client_options_default(&options);
    options.version_preference = CNETMOD_HTTP_1_ONLY;
    auto* client = cnetmod_http_client_create(runtime, &options);
    ASSERT_TRUE(client != nullptr);

    c_api_completion_state completion{.runtime = runtime};
    const auto url = "http://127.0.0.1:" + std::to_string(port) + "/slow";
    auto* pending = cnetmod_http_request_start(client, CNETMOD_HTTP_GET,
        url.c_str(), nullptr, 0U, record_completion, &completion);
    ASSERT_TRUE(pending != nullptr);
    std::thread canceller{[pending]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            cnetmod_http_request_cancel(pending);
        }};
    while (!completion.completed)
    {
        cnetmod_runtime_run_one(runtime);
        if (!completion.completed)
            cnetmod_runtime_restart(runtime);
    }
    canceller.join();

    ASSERT_TRUE(completion.response == nullptr);
    ASSERT_NE(completion.error_code, 0);
    cnetmod_http_request_destroy(pending);
    cnetmod_http_client_destroy(client);
    cnetmod_runtime_destroy(runtime);
    server.stop();
    server_context->stop();
    server_thread.join();
}

RUN_TESTS()
