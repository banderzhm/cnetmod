import std;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.coro.cancel;
import cnetmod.coro.spawn;
import cnetmod.coro.task;

#include <cnetmod/c_api.h>

struct cnetmod_http_response
{
    int status{};
    std::string body;
};

namespace {

using namespace cnetmod;

struct runtime_state
{
    std::unique_ptr<io_context> context{make_io_context()};
};

struct client_state
{
    std::shared_ptr<runtime_state> runtime;
    std::unique_ptr<http::client> client;
};

struct request_state
{
    std::shared_ptr<client_state> client;
    cancel_token cancellation;
    cnetmod_http_completion completion{};
    void* user_data{};
};

auto to_method(cnetmod_http_method method) -> std::optional<http::http_method>
{
    switch (method)
    {
    case CNETMOD_HTTP_GET:
        return http::http_method::GET;
    case CNETMOD_HTTP_POST:
        return http::http_method::POST;
    case CNETMOD_HTTP_PUT:
        return http::http_method::PUT;
    case CNETMOD_HTTP_DELETE:
        return http::http_method::DELETE_;
    case CNETMOD_HTTP_PATCH:
        return http::http_method::PATCH;
    case CNETMOD_HTTP_HEAD:
        return http::http_method::HEAD;
    case CNETMOD_HTTP_OPTIONS:
        return http::http_method::OPTIONS;
    }
    return std::nullopt;
}

auto to_version(cnetmod_http_version_preference preference)
    -> http::http_version_preference
{
    switch (preference)
    {
    case CNETMOD_HTTP_1_ONLY:
        return http::http_version_preference::http1_only;
    case CNETMOD_HTTP_2_ONLY:
        return http::http_version_preference::http2_only;
    case CNETMOD_HTTP_1_PREFERRED:
        return http::http_version_preference::http1_preferred;
    case CNETMOD_HTTP_3_ONLY:
        return http::http_version_preference::http3_only;
    case CNETMOD_HTTP_3_PREFERRED:
        return http::http_version_preference::http3_preferred;
    case CNETMOD_HTTP_2_PREFERRED:
    default:
        return http::http_version_preference::http2_preferred;
    }
}

auto request_runner(std::shared_ptr<request_state> state,
    http::http_method method, std::string url, std::string body) -> task<void>
{
    http::request request{method, std::move(url)};
    request.set_body(std::move(body));
    auto result = co_await state->client->client->send(request, state->cancellation);
    if (result)
    {
        auto* response = new cnetmod_http_response{.status = result->status_code(),
            .body = std::string(result->body())};
        state->completion(state->user_data, response, 0, nullptr);
    }
    else
    {
        const auto message = result.error().message();
        state->completion(state->user_data, nullptr, result.error().value(),
            message.c_str());
    }
}

} // namespace

struct cnetmod_runtime
{
    std::shared_ptr<runtime_state> state;
};

struct cnetmod_http_client
{
    std::shared_ptr<client_state> state;
};

struct cnetmod_http_request
{
    std::shared_ptr<request_state> state;
};

extern "C"
{

    cnetmod_runtime* cnetmod_runtime_create(void)
    {
        try
        {
            return new cnetmod_runtime{std::make_shared<runtime_state>()};
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void cnetmod_runtime_destroy(cnetmod_runtime* runtime)
    {
        delete runtime;
    }

    size_t cnetmod_runtime_poll(cnetmod_runtime* runtime)
    {
        return runtime && runtime->state && runtime->state->context
            ? runtime->state->context->poll()
            : 0U;
    }

    size_t cnetmod_runtime_run_one(cnetmod_runtime* runtime)
    {
        return runtime && runtime->state && runtime->state->context
            ? runtime->state->context->run_one()
            : 0U;
    }

    void cnetmod_runtime_stop(cnetmod_runtime* runtime)
    {
        if (runtime && runtime->state && runtime->state->context)
            runtime->state->context->stop();
    }

    void cnetmod_runtime_restart(cnetmod_runtime* runtime)
    {
        if (runtime && runtime->state && runtime->state->context)
            runtime->state->context->restart();
    }

    void cnetmod_http_client_options_default(cnetmod_http_client_options* options)
    {
        if (!options)
            return;
        *options = {.struct_size = sizeof(cnetmod_http_client_options),
            .connect_timeout_ms = 5000U,
            .request_timeout_ms = 30000U,
            .max_redirects = 10U,
            .follow_redirects = 1U,
            .verify_peer = 1U,
            .keep_alive = 1U,
            .enable_cookies = 1U,
            .version_preference = CNETMOD_HTTP_2_PREFERRED,
            .user_agent = nullptr};
    }

    cnetmod_http_client* cnetmod_http_client_create(cnetmod_runtime* runtime,
        const cnetmod_http_client_options* options)
    {
        if (!runtime || !runtime->state || !runtime->state->context)
            return nullptr;
        try
        {
            http::client_options native{};
            if (options && options->struct_size >= sizeof(cnetmod_http_client_options))
            {
                native.connect_timeout = std::chrono::milliseconds{options->connect_timeout_ms};
                native.request_timeout = std::chrono::milliseconds{options->request_timeout_ms};
                native.max_redirects = options->max_redirects;
                native.follow_redirects = options->follow_redirects != 0U;
                native.verify_peer = options->verify_peer != 0U;
                native.keep_alive = options->keep_alive != 0U;
                native.enable_cookies = options->enable_cookies != 0U;
                native.version_pref = to_version(options->version_preference);
                if (options->user_agent)
                    native.user_agent = options->user_agent;
            }
            auto state = std::make_shared<client_state>();
            state->runtime = runtime->state;
            state->client = std::make_unique<http::client>(*state->runtime->context,
                std::move(native));
            return new cnetmod_http_client{std::move(state)};
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void cnetmod_http_client_destroy(cnetmod_http_client* client)
    {
        delete client;
    }

    cnetmod_http_request* cnetmod_http_request_start(cnetmod_http_client* client,
        cnetmod_http_method method, const char* url, const uint8_t* body,
        size_t body_size, cnetmod_http_completion completion, void* user_data)
    {
        const auto native_method = to_method(method);
        if (!client || !client->state || !client->state->runtime || !url || !completion ||
            !native_method || (body_size != 0U && !body))
            return nullptr;
        if (!http::url::parse(url))
            return nullptr;
        try
        {
            auto state = std::make_shared<request_state>();
            state->client = client->state;
            state->completion = completion;
            state->user_data = user_data;
            std::string request_body;
            if (body_size != 0U)
                request_body.assign(reinterpret_cast<const char*>(body), body_size);
            spawn(*state->client->runtime->context,
                request_runner(state, *native_method, url, std::move(request_body)));
            return new cnetmod_http_request{std::move(state)};
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void cnetmod_http_request_cancel(cnetmod_http_request* request)
    {
        if (request && request->state)
            request->state->cancellation.cancel();
    }

    void cnetmod_http_request_destroy(cnetmod_http_request* request)
    {
        delete request;
    }

    int cnetmod_http_response_status(const cnetmod_http_response* response)
    {
        return response ? response->status : 0;
    }

    const uint8_t* cnetmod_http_response_body(const cnetmod_http_response* response,
        size_t* body_size)
    {
        if (body_size)
            *body_size = response ? response->body.size() : 0U;
        return response && !response->body.empty()
            ? reinterpret_cast<const uint8_t*>(response->body.data())
            : nullptr;
    }

    void cnetmod_http_response_free(cnetmod_http_response* response)
    {
        delete response;
    }

} // extern "C"
