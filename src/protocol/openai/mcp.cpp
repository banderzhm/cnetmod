/// cnetmod.protocol.openai:mcp — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.bridge;
import cnetmod.core.process;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import :foundation;
import :tools;
import :mcp;
import cnetmod.json;

namespace cnetmod::openai {

namespace {
    auto decode_mcp_body(std::string_view body)
        -> std::expected<std::vector<json>, std::string>
    {
        auto direct = cnetmod::json::parse_document(body);
        if (direct)
            return std::vector<json>{std::move(*direct)};

        std::vector<json> messages;
        std::size_t offset = 0;
        while (offset < body.size())
        {
            const auto end = body.find('\n', offset);
            auto line = body.substr(offset,
                end == std::string_view::npos ? body.size() - offset
                                              : end - offset);
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            if (line.starts_with("data:"))
            {
                line.remove_prefix(5);
                while (line.starts_with(' '))
                    line.remove_prefix(1);
                if (!line.empty() && line != "[DONE]")
                {
                    auto message = cnetmod::json::parse_document(line);
                    if (!message)
                        return std::unexpected(
                            "MCP SSE event contains invalid JSON");
                    messages.push_back(std::move(*message));
                }
            }
            if (end == std::string_view::npos)
                break;
            offset = end + 1;
        }
        if (messages.empty())
            return std::unexpected("MCP response is neither JSON nor SSE JSON data");
        return messages;
    }
} // namespace

void mcp_transport::set_inbound_handler(mcp_inbound_handler)
{
}

mcp_streamable_http_transport::mcp_streamable_http_transport(
    io_context& context, mcp_http_options options)
    : client_(context, options.client), options_(std::move(options))
{
    if (options_.endpoint.empty())
        throw std::invalid_argument("MCP HTTP endpoint cannot be empty");
}

auto mcp_streamable_http_transport::exchange(json request)
    -> task<std::expected<json, std::string>>
{
    co_return co_await send(std::move(request), true);
}

auto mcp_streamable_http_transport::notify(json notification)
    -> task<std::expected<void, std::string>>
{
    auto result = co_await send(std::move(notification), false);
    if (!result)
        co_return std::unexpected(result.error());
    co_return std::expected<void, std::string>{};
}

void mcp_streamable_http_transport::set_inbound_handler(
    mcp_inbound_handler handler)
{
    inbound_handler_ = std::move(handler);
}

auto mcp_streamable_http_transport::send(json payload, bool expect_response)
    -> task<std::expected<json, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto expected_id = cnetmod::json::value_or(
        payload, "id", json{});
    http::request request(http::http_method::POST, options_.endpoint);
    request.set_header("Content-Type", "application/json");
    request.set_header("Accept", "application/json, text/event-stream");
    if (!session_id_.empty())
        request.set_header("Mcp-Session-Id", session_id_);
    for (const auto& [name, value] : options_.headers)
        request.set_header(name, value);
    request.set_body(cnetmod::json::write_document(payload).value_or("{}"));

    auto response = co_await client_.send(request);
    if (!response)
        co_return std::unexpected("MCP HTTP request failed: " +
            response.error().message());
    if (const auto session = response->get_header("Mcp-Session-Id");
        !session.empty())
        session_id_ = session;
    if (response->status_code() < 200 || response->status_code() >= 300)
        co_return std::unexpected(std::format("MCP HTTP status {}: {}",
            response->status_code(), response->body()));
    if (response->status_code() == 202 || response->body().empty())
        co_return cnetmod::json::object();
    auto messages = decode_mcp_body(response->body());
    if (!messages)
        co_return std::unexpected(messages.error());
    std::optional<json> matched_response;
    for (auto& message : *messages)
    {
        if (expect_response && message.is_object() &&
            message.contains("id") && !message.contains("method") &&
            cnetmod::json::equivalent(message["id"], expected_id))
        {
            matched_response = std::move(message);
            continue;
        }
        if (!inbound_handler_)
            continue;
        auto reply = co_await inbound_handler_(std::move(message));
        if (!reply)
            continue;
        http::request reply_request(http::http_method::POST,
            options_.endpoint);
        reply_request.set_header("Content-Type", "application/json");
        reply_request.set_header("Accept",
            "application/json, text/event-stream");
        if (!session_id_.empty())
            reply_request.set_header("Mcp-Session-Id", session_id_);
        for (const auto& [name, value] : options_.headers)
            reply_request.set_header(name, value);
        reply_request.set_body(
            cnetmod::json::write_document(*reply).value_or("{}"));
        auto acknowledged = co_await client_.send(reply_request);
        if (!acknowledged)
            co_return std::unexpected("MCP HTTP reply failed: " +
                acknowledged.error().message());
        if (acknowledged->status_code() < 200 ||
            acknowledged->status_code() >= 300)
            co_return std::unexpected(std::format(
                "MCP HTTP reply status {}", acknowledged->status_code()));
    }
    if (matched_response)
        co_return std::move(*matched_response);
    if (!expect_response)
        co_return cnetmod::json::object();
    co_return std::unexpected("MCP HTTP response did not contain matching id");
}

mcp_stdio_transport::mcp_stdio_transport(io_context& context,
    thread_pool& pool, mcp_stdio_options options)
    : context_(context), pool_(pool), options_(std::move(options))
{
    if (options_.process.executable.empty())
        throw std::invalid_argument("MCP stdio executable cannot be empty");
}

mcp_stdio_transport::~mcp_stdio_transport()
{
    close();
}

auto mcp_stdio_transport::ensure_started()
    -> std::expected<void, std::string>
{
    if (process_ && process_->running())
        return {};
    process_.reset();
    auto launched = child_process::launch(options_.process);
    if (!launched)
        return std::unexpected("failed to start MCP process: " +
            launched.error().message());
    process_.emplace(std::move(*launched));
    return {};
}

auto mcp_stdio_transport::exchange(json request)
    -> task<std::expected<json, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto expected_id = cnetmod::json::value_or(
        request, "id", json{});
    auto written = co_await blocking_invoke(pool_, context_,
        [this, request = std::move(request)]() mutable
            -> std::expected<void, std::string>
        {
            auto started = ensure_started();
            if (!started)
                return std::unexpected(started.error());
            auto wire = cnetmod::json::write_document(request).value_or("{}");
            wire.push_back('\n');
            auto result = process_->write(wire);
            if (!result)
                return std::unexpected("MCP stdio write failed: " +
                    result.error().message());
            return {};
        });
    if (!written)
        co_return std::unexpected(written.error());

    for (;;)
    {
        auto response = co_await blocking_invoke(pool_, context_,
            [this]() -> std::expected<json, std::string>
            {
                for (;;)
                {
                    auto line = process_->read_line();
                    if (!line)
                        return std::unexpected("MCP stdio read failed: " +
                            line.error().message());
                    if (line->empty())
                        continue;
                    auto response = cnetmod::json::parse_document(*line);
                    if (!response)
                        return std::unexpected(
                            "MCP stdio server returned invalid JSON");
                    return std::move(*response);
                }
            });
        if (!response)
            co_return std::unexpected(response.error());
        if (response->is_object() && response->contains("id") &&
            !response->contains("method") &&
            cnetmod::json::equivalent((*response)["id"], expected_id))
            co_return response;
        if (!inbound_handler_)
            continue;
        auto reply = co_await inbound_handler_(std::move(*response));
        if (!reply)
            continue;
        auto reply_written = co_await blocking_invoke(pool_, context_,
            [this, reply = std::move(*reply)]() mutable
                -> std::expected<void, std::string>
            {
                auto wire = cnetmod::json::write_document(reply).value_or("{}");
                wire.push_back('\n');
                auto result = process_->write(wire);
                if (!result)
                    return std::unexpected("MCP stdio write failed: " +
                        result.error().message());
                return {};
            });
        if (!reply_written)
            co_return std::unexpected(reply_written.error());
    }
}

auto mcp_stdio_transport::notify(json notification)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [this, notification = std::move(notification)]() mutable
            -> std::expected<void, std::string>
        {
            auto started = ensure_started();
            if (!started)
                return std::unexpected(started.error());
            auto wire = cnetmod::json::write_document(notification).value_or("{}");
            wire.push_back('\n');
            auto written = process_->write(wire);
            if (!written)
                return std::unexpected("MCP stdio write failed: " +
                    written.error().message());
            return {};
        });
}

void mcp_stdio_transport::set_inbound_handler(mcp_inbound_handler handler)
{
    inbound_handler_ = std::move(handler);
}

void mcp_stdio_transport::close() noexcept
{
    if (process_)
        process_->terminate();
    process_.reset();
}

mcp_client::mcp_client(mcp_transport& transport, mcp_client_options options)
    : transport_(transport), options_(std::move(options))
{
    transport_.set_inbound_handler(
        [this](json message)
        {
            return handle_inbound(std::move(message));
        });
}

mcp_client::~mcp_client()
{
    transport_.set_inbound_handler({});
}

auto mcp_client::handle_inbound(json message) -> task<std::optional<json>>
{
    if (!message.is_object() || !message.contains("method") ||
        !message["method"].is_string())
        co_return std::nullopt;
    const auto method = message["method"].get<std::string>();
    const auto parameters = cnetmod::json::value_or(
        message, "params", cnetmod::json::object());
    if (!message.contains("id"))
    {
        if (options_.handlers.notification)
            options_.handlers.notification(method, parameters);
        co_return std::nullopt;
    }

    mcp_request_handler* handler = nullptr;
    if (method == "sampling/createMessage")
        handler = &options_.handlers.sampling;
    else if (method == "roots/list")
        handler = &options_.handlers.roots;
    else if (method == "elicitation/create")
        handler = &options_.handlers.elicitation;

    if (!handler || !*handler)
        co_return json{{"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"error", {{"code", -32601}, {"message", "MCP client method is not configured: " + method}}}};
    auto result = co_await (*handler)(parameters);
    if (!result)
        co_return json{{"jsonrpc", "2.0"},
            {"id", message["id"]},
            {"error", {{"code", -32603}, {"message", result.error()}}}};
    co_return json{{"jsonrpc", "2.0"},
        {"id", message["id"]}, {"result", std::move(*result)}};
}

auto mcp_client::request(std::string method, json parameters)
    -> task<std::expected<json, std::string>>
{
    const auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
    auto response = co_await transport_.exchange({{"jsonrpc", "2.0"},
        {"id", id}, {"method", std::move(method)},
        {"params", std::move(parameters)}});
    if (!response)
        co_return std::unexpected(response.error());
    if (!response->is_object())
        co_return std::unexpected("MCP JSON-RPC response must be an object");
    if (response->contains("error"))
    {
        const auto& error = (*response)["error"];
        co_return std::unexpected(std::format("MCP error {}: {}",
            cnetmod::json::value_or(error, "code", 0),
            cnetmod::json::value_or(
                error, "message", std::string{"unknown error"})));
    }
    if (!response->contains("result"))
        co_return std::unexpected("MCP JSON-RPC response has no result");
    co_return (*response)["result"];
}

auto mcp_client::initialize() -> task<std::expected<json, std::string>>
{
    json parameters = cnetmod::json::object();
    parameters["protocolVersion"] = options_.protocol_version;
    parameters["capabilities"] = options_.capabilities;
    parameters["clientInfo"] = cnetmod::json::object(
        {{"name", options_.name}, {"version", options_.version}});
    auto result = co_await request("initialize", std::move(parameters));
    if (!result)
        co_return std::unexpected(result.error());
    server_capabilities_ = cnetmod::json::value_or(
        *result, "capabilities", cnetmod::json::object());
    server_info_ = cnetmod::json::value_or(
        *result, "serverInfo", cnetmod::json::object());
    auto notified = co_await transport_.notify(
        {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    if (!notified)
        co_return std::unexpected(notified.error());
    initialized_ = true;
    co_return result;
}

auto mcp_client::list_tools() -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("tools/list");
}

auto mcp_client::call_tool(std::string name, json arguments)
    -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("tools/call",
        {{"name", std::move(name)}, {"arguments", std::move(arguments)}});
}

auto mcp_client::list_resources() -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("resources/list");
}

auto mcp_client::list_resource_templates()
    -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("resources/templates/list");
}

auto mcp_client::read_resource(std::string uri)
    -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("resources/read", {{"uri", std::move(uri)}});
}

auto mcp_client::request_acknowledgement(std::string method, json parameters)
    -> task<std::expected<void, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    auto acknowledged = co_await request(std::move(method),
        std::move(parameters));
    if (!acknowledged)
        co_return std::unexpected(acknowledged.error());
    co_return std::expected<void, std::string>{};
}

auto mcp_client::subscribe_resource(std::string uri)
    -> task<std::expected<void, std::string>>
{
    co_return co_await request_acknowledgement("resources/subscribe",
        {{"uri", std::move(uri)}});
}

auto mcp_client::unsubscribe_resource(std::string uri)
    -> task<std::expected<void, std::string>>
{
    co_return co_await request_acknowledgement("resources/unsubscribe",
        {{"uri", std::move(uri)}});
}

auto mcp_client::list_prompts() -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("prompts/list");
}

auto mcp_client::get_prompt(std::string name, json arguments)
    -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    co_return co_await request("prompts/get",
        {{"name", std::move(name)}, {"arguments", std::move(arguments)}});
}

auto mcp_client::complete(json reference, json argument, json context)
    -> task<std::expected<json, std::string>>
{
    if (!initialized_)
        co_return std::unexpected("MCP client is not initialized");
    json parameters{{"ref", std::move(reference)},
        {"argument", std::move(argument)}};
    if (!context.empty())
        parameters["context"] = std::move(context);
    co_return co_await request("completion/complete", std::move(parameters));
}

auto mcp_client::set_logging_level(std::string level)
    -> task<std::expected<void, std::string>>
{
    if (level.empty())
        co_return std::unexpected("MCP logging level cannot be empty");
    co_return co_await request_acknowledgement("logging/setLevel",
        {{"level", std::move(level)}});
}

auto mcp_client::ping() -> task<std::expected<void, std::string>>
{
    co_return co_await request_acknowledgement(
        "ping", cnetmod::json::object());
}

auto mcp_client::register_tools(tool_registry& registry)
    -> task<std::expected<std::size_t, std::string>>
{
    auto listed = co_await list_tools();
    if (!listed)
        co_return std::unexpected(listed.error());
    if (!listed->contains("tools") || !(*listed)["tools"].is_array())
        co_return std::unexpected("MCP tools/list result has no tools array");
    std::size_t count = 0;
    for (const auto& remote : (*listed)["tools"].get_array())
    {
        const auto name = cnetmod::json::value_or(
            remote, "name", std::string{});
        if (name.empty())
            co_return std::unexpected("MCP tool has no name");
        auto added = registry.add({.definition = {
                                       .type = "function",
                                       .function_name = name,
                                       .function_description = cnetmod::json::value_or(
                                           remote, "description", std::string{}),
                                       .function_parameters = cnetmod::json::value_or(
                                           remote, "inputSchema", cnetmod::json::object()),
                                       .strict = true},
            .handler = [this, name](const json& arguments) -> task<std::expected<json, std::string>>
            {
                co_return co_await call_tool(name, arguments);
            }});
        if (!added)
            co_return std::unexpected(added.error());
        ++count;
    }
    co_return count;
}

auto mcp_client::server_capabilities() const -> const json&
{
    return server_capabilities_;
}

auto mcp_client::server_info() const -> const json&
{
    return server_info_;
}

auto mcp_client::key() const noexcept -> const std::string&
{
    return options_.key;
}

mcp_tool_provider::mcp_tool_provider(std::vector<mcp_client*> clients,
    mcp_tool_provider_options options)
    : options_(options)
{
    for (auto* client : clients)
    {
        if (!client || std::ranges::contains(clients_, client))
            continue;
        clients_.push_back(client);
    }
}

auto mcp_tool_provider::add_client(mcp_client& client)
    -> std::expected<void, std::string>
{
    concurrent_containers::exclusive_latch_guard guard{latch_};
    if (std::ranges::contains(clients_, &client))
        return std::unexpected("MCP client is already registered");
    clients_.push_back(&client);
    return {};
}

auto mcp_tool_provider::remove_client(const mcp_client& client) noexcept -> bool
{
    concurrent_containers::exclusive_latch_guard guard{latch_};
    const auto original_size = clients_.size();
    std::erase(clients_, &client);
    return clients_.size() != original_size;
}

auto mcp_tool_provider::add_filter(mcp_tool_filter filter)
    -> std::expected<void, std::string>
{
    if (!filter)
        return std::unexpected("MCP tool filter is not configured");
    concurrent_containers::exclusive_latch_guard guard{latch_};
    filters_.push_back(std::move(filter));
    return {};
}

void mcp_tool_provider::clear_filters() noexcept
{
    concurrent_containers::exclusive_latch_guard guard{latch_};
    filters_.clear();
}

auto mcp_tool_provider::is_dynamic() const noexcept -> bool
{
    return options_.dynamic;
}

auto mcp_tool_provider::provide(const tool_provider_request&)
    -> task<std::expected<tool_provider_result, std::string>>
{
    std::vector<mcp_client*> clients;
    std::vector<mcp_tool_filter> filters;
    {
        concurrent_containers::shared_latch_guard guard{latch_};
        clients = clients_;
        filters = filters_;
    }

    tool_provider_result result;
    std::set<std::string, std::less<>> names;
    for (auto* client : clients)
    {
        auto listed = co_await client->list_tools();
        if (!listed)
        {
            if (options_.fail_if_any_client_fails)
                co_return std::unexpected(std::format(
                    "MCP client '{}' tools/list failed: {}",
                    client->key(), listed.error()));
            continue;
        }
        if (!listed->contains("tools") || !(*listed)["tools"].is_array())
        {
            if (options_.fail_if_any_client_fails)
                co_return std::unexpected(std::format(
                    "MCP client '{}' returned no tools array", client->key()));
            continue;
        }

        for (const auto& remote : (*listed)["tools"].get_array())
        {
            tool definition{
                .type = "function",
                .function_name = cnetmod::json::value_or(
                    remote, "name", std::string{}),
                .function_description = cnetmod::json::value_or(
                    remote, "description", std::string{}),
                .function_parameters = cnetmod::json::value_or(
                    remote,
                    "inputSchema", cnetmod::json::object()),
                .strict = true};
            if (definition.function_name.empty())
                continue;
            bool rejected = false;
            for (const auto& filter : filters)
                if (!filter(*client, definition))
                {
                    rejected = true;
                    break;
                }
            if (rejected)
                continue;
            if (!names.insert(definition.function_name).second)
                co_return std::unexpected(std::format(
                    "duplicate MCP tool '{}'; add a client-aware filter",
                    definition.function_name));
            auto* selected_client = client;
            auto name = definition.function_name;
            result.tools.push_back({.definition = std::move(definition),
                .handler = [selected_client, name](const json& arguments)
                    -> task<std::expected<json, std::string>>
                {
                    co_return co_await selected_client->call_tool(
                        name, arguments);
                }});
        }
    }
    co_return result;
}

} // namespace cnetmod::openai
