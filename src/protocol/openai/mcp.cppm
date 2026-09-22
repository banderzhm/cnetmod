/// cnetmod.protocol.openai:mcp — Model Context Protocol client and tool adapter

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:mcp;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
export import cnetmod.core.process;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.protocol.http;
import :foundation;
import :tools;
import cnetmod.json;

namespace cnetmod::openai {

export using mcp_inbound_handler =
    std::function<task<std::optional<json>>(json message)>;

export class mcp_transport
{
public:
    virtual ~mcp_transport() = default;
    virtual auto exchange(json request)
        -> task<std::expected<json, std::string>> = 0;
    virtual auto notify(json notification)
        -> task<std::expected<void, std::string>> = 0;
    virtual void set_inbound_handler(mcp_inbound_handler handler);
};

export struct mcp_http_options
{
    std::string endpoint;
    std::map<std::string, std::string, std::less<>> headers;
    http::client_options client;
};

/// Strategy implementing MCP Streamable HTTP over cnetmod's async HTTP client.
export class mcp_streamable_http_transport final : public mcp_transport
{
public:
    mcp_streamable_http_transport(io_context& context, mcp_http_options options);
    auto exchange(json request)
        -> task<std::expected<json, std::string>> override;
    auto notify(json notification)
        -> task<std::expected<void, std::string>> override;
    void set_inbound_handler(mcp_inbound_handler handler) override;

private:
    auto send(json payload, bool expect_response)
        -> task<std::expected<json, std::string>>;

    http::client client_;
    mcp_http_options options_;
    std::string session_id_;
    async_mutex mutex_;
    mcp_inbound_handler inbound_handler_;
};

export struct mcp_stdio_options
{
    process_options process;
};

/// Strategy implementing MCP's newline-delimited stdio transport.
export class mcp_stdio_transport final : public mcp_transport
{
public:
    mcp_stdio_transport(io_context& context, thread_pool& pool,
        mcp_stdio_options options);
    ~mcp_stdio_transport() override;

    auto exchange(json request)
        -> task<std::expected<json, std::string>> override;
    auto notify(json notification)
        -> task<std::expected<void, std::string>> override;
    void set_inbound_handler(mcp_inbound_handler handler) override;
    void close() noexcept;

private:
    auto ensure_started() -> std::expected<void, std::string>;

    io_context& context_;
    thread_pool& pool_;
    mcp_stdio_options options_;
    std::optional<child_process> process_;
    async_mutex mutex_;
    mcp_inbound_handler inbound_handler_;
};

export using mcp_request_handler = std::function<
    task<std::expected<json, std::string>>(const json& parameters)>;
export using mcp_notification_handler =
    std::function<void(std::string_view method, const json& parameters)>;

export struct mcp_client_handlers
{
    mcp_request_handler sampling;
    mcp_request_handler roots;
    mcp_request_handler elicitation;
    mcp_notification_handler notification;
};

export struct mcp_client_options
{
    std::string key;
    std::string name = "cnetmod";
    std::string version = "2.0.0";
    std::string protocol_version = "2025-11-25";
    json capabilities = json::object();
    mcp_client_handlers handlers;
};

/// Facade for MCP lifecycle, tools, resources and prompts.
export class mcp_client
{
public:
    explicit mcp_client(mcp_transport& transport,
        mcp_client_options options = {});
    ~mcp_client();
    mcp_client(const mcp_client&) = delete;
    auto operator=(const mcp_client&) -> mcp_client& = delete;

    auto initialize() -> task<std::expected<json, std::string>>;
    auto list_tools() -> task<std::expected<json, std::string>>;
    auto call_tool(std::string name, json arguments = json::object())
        -> task<std::expected<json, std::string>>;
    auto list_resources() -> task<std::expected<json, std::string>>;
    auto list_resource_templates() -> task<std::expected<json, std::string>>;
    auto read_resource(std::string uri)
        -> task<std::expected<json, std::string>>;
    auto subscribe_resource(std::string uri)
        -> task<std::expected<void, std::string>>;
    auto unsubscribe_resource(std::string uri)
        -> task<std::expected<void, std::string>>;
    auto list_prompts() -> task<std::expected<json, std::string>>;
    auto get_prompt(std::string name, json arguments = json::object())
        -> task<std::expected<json, std::string>>;
    auto complete(json reference, json argument,
        json context = json::object())
        -> task<std::expected<json, std::string>>;
    auto set_logging_level(std::string level)
        -> task<std::expected<void, std::string>>;
    auto ping() -> task<std::expected<void, std::string>>;
    auto register_tools(tool_registry& registry)
        -> task<std::expected<std::size_t, std::string>>;

    [[nodiscard]] auto server_capabilities() const -> const json&;
    [[nodiscard]] auto server_info() const -> const json&;
    [[nodiscard]] auto key() const noexcept -> const std::string&;

private:
    auto request(std::string method, json parameters = json::object())
        -> task<std::expected<json, std::string>>;
    auto handle_inbound(json message) -> task<std::optional<json>>;
    auto request_acknowledgement(std::string method, json parameters)
        -> task<std::expected<void, std::string>>;

    mcp_transport& transport_;
    mcp_client_options options_;
    std::atomic<std::uint64_t> next_id_{1};
    bool initialized_ = false;
    json server_capabilities_ = json::object();
    json server_info_ = json::object();
};

export using mcp_tool_filter = std::function<bool(
    const mcp_client& client, const tool& definition)>;

export struct mcp_tool_provider_options
{
    bool fail_if_any_client_fails = false;
    bool dynamic = false;
};

/// Dynamic adapter that merges and filters tools from multiple MCP clients.
export class mcp_tool_provider final : public tool_provider
{
public:
    explicit mcp_tool_provider(std::vector<mcp_client*> clients = {},
        mcp_tool_provider_options options = {});

    [[nodiscard]] auto add_client(mcp_client& client)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto remove_client(const mcp_client& client) noexcept -> bool;
    [[nodiscard]] auto add_filter(mcp_tool_filter filter)
        -> std::expected<void, std::string>;
    void clear_filters() noexcept;

    [[nodiscard]] auto is_dynamic() const noexcept -> bool override;
    auto provide(const tool_provider_request& request)
        -> task<std::expected<tool_provider_result, std::string>> override;

private:
    mutable concurrent_containers::atomic_rw_latch latch_;
    std::vector<mcp_client*> clients_;
    std::vector<mcp_tool_filter> filters_;
    mcp_tool_provider_options options_;
};

} // namespace cnetmod::openai
