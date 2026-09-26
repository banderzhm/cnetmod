module cnetmod.application.openai;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.json;
import cnetmod.ai;
import cnetmod.observability.openai;
import cnetmod.coro.mutex;
import cnetmod.coro.striped_mutex;
import cnetmod.coro.task_group;
import cnetmod.json;

namespace cnetmod::application {

namespace {

    struct openai_service_settings
    {
        openai::connect_options connection;
        std::size_t pool_size = 4;
    };

    [[nodiscard]] auto parse_openai_settings(
        const cnetmod::json::document& properties)
        -> std::expected<openai_service_settings, std::error_code>
    {
        if (!properties_are_known(properties,
                {"base_url", "api_key", "tls_verify", "timeout_seconds",
                    "pool_size"}) ||
            !integer_property_in_range(properties, "timeout_seconds", 1,
                std::numeric_limits<int>::max()) ||
            !integer_property_in_range(properties, "pool_size", 1, 1024))
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        try
        {
            openai_service_settings settings;
            settings.connection.api_base = cnetmod::json::value_or(properties, "base_url",
                settings.connection.api_base);
            settings.connection.api_key = cnetmod::json::value_or(properties, "api_key",
                settings.connection.api_key);
            settings.connection.tls_verify = cnetmod::json::value_or(properties, "tls_verify",
                settings.connection.tls_verify);
            settings.connection.timeout_seconds = cnetmod::json::value_or(properties,
                "timeout_seconds", settings.connection.timeout_seconds);
            settings.pool_size = cnetmod::json::value_or(properties,
                "pool_size", settings.pool_size);
            if (settings.connection.api_key.empty())
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            return settings;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                std::make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        }
    }

    class observed_openai_chat_model final : public ai::chat_model
    {
    public:
        observed_openai_chat_model(std::shared_ptr<openai::client> client,
            openai::run_listener* listener) noexcept
            : client_(std::move(client)), model_(*client_), listener_(listener)
        {
        }

        auto invoke(ai::chat_request request, const ai::run_config& configuration)
            -> task<std::expected<ai::chat_response, std::string>> override
        {
            co_return co_await model_.invoke(
                std::move(request), observe(configuration));
        }

        auto stream(ai::chat_request request, stream_handler handler,
            const ai::run_config& configuration)
            -> task<std::expected<ai::chat_response, std::string>> override
        {
            co_return co_await model_.stream(std::move(request),
                std::move(handler), observe(configuration));
        }

    private:
        [[nodiscard]] auto observe(ai::run_config configuration) const
            -> ai::run_config
        {
            if (listener_ && std::ranges::find(configuration.listeners, listener_) == configuration.listeners.end())
                configuration.listeners.push_back(listener_);
            return configuration;
        }

        std::shared_ptr<openai::client> client_;
        openai::openai_chat_model model_;
        openai::run_listener* listener_;
    };

} // namespace

class openai_model_generation
{
public:
    openai_model_generation(io_context& io, openai_service_settings settings,
        openai::run_listener* listener)
        : options(std::move(settings.connection))
    {
        clients.reserve(settings.pool_size);
        models.reserve(settings.pool_size);
        for (std::size_t index = 0; index < settings.pool_size; ++index)
        {
            auto client = std::make_shared<openai::client>(io);
            models.push_back(std::make_shared<observed_openai_chat_model>(
                client, listener));
            clients.push_back(std::move(client));
        }
    }

    openai::connect_options options;
    std::vector<std::shared_ptr<openai::client>> clients;
    std::vector<std::shared_ptr<ai::chat_model>> models;
};

namespace {

    auto connect_generation(io_context& io,
        openai_model_generation& generation,
        cancel_token* cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        if (cancellation && cancellation->is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        task_group connections{io};
        for (auto& client : generation.clients)
        {
            if (client->is_connected())
                continue;
            const auto accepted = connections.run(
                [client, options = generation.options](cancel_token&)
                    -> task<std::expected<void, std::error_code>>
                {
                    auto connected = co_await client->connect(options);
                    if (!connected)
                        co_return std::unexpected(std::make_error_code(
                            std::errc::connection_refused));
                    co_return std::expected<void, std::error_code>{};
                });
            if (!accepted)
            {
                connections.cancel();
                (void)co_await connections.join();
                for (auto& opened : generation.clients)
                    opened->close();
                co_return std::unexpected(
                    std::make_error_code(std::errc::not_enough_memory));
            }
        }
        auto connected = co_await connections.join();
        if (!connected || (cancellation && cancellation->is_cancelled()))
        {
            for (auto& opened : generation.clients)
                opened->close();
            if (!connected)
                co_return std::unexpected(connected.error());
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        }
        co_return std::expected<void, std::error_code>{};
    }

} // namespace

openai_service::openai_service(io_context& io,
    observability::telemetry_hub& telemetry, openai::connect_options options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery, std::size_t pool_size)
    : io_(io),
      model_pool_(io_),
      instance_(std::move(instance)),
      requirement_(requirement),
      recovery_(recovery),
      session_gates_(
          std::make_shared<striped_async_mutex<std::string>>())
{
    if (pool_size == 0)
        throw std::invalid_argument("OpenAI pool size must be positive");
    auto sink = telemetry.spans();
    if (sink || telemetry.records_metrics())
    {
        openai::telemetry_options settings;
        settings.record_metrics = telemetry.records_metrics();
        telemetry_listener_.emplace(telemetry.measurements(), std::move(sink),
            std::move(settings));
    }
    generation_ = std::make_shared<openai_model_generation>(io_,
        openai_service_settings{std::move(options), pool_size},
        telemetry_listener());
}

auto openai_service::current_client()
    -> task<std::shared_ptr<openai::client>>
{
    auto* caller = io_context::current();
    if (caller != nullptr && caller != &io_)
        throw std::logic_error{
            "OpenAI client handle cannot cross its owning event loop"};
    co_await generation_gate_.lock();
    async_lock_guard guard{generation_gate_, std::adopt_lock};
    co_return generation_->clients.front();
}

auto openai_service::telemetry_listener() noexcept
    -> openai::telemetry_listener*
{
    return telemetry_listener_ ? &*telemetry_listener_ : nullptr;
}

auto openai_service::run_configuration(openai::run_config configuration)
    -> openai::run_config
{
    auto* listener = telemetry_listener();
    if (listener && std::ranges::find(configuration.listeners, listener) == configuration.listeners.end())
        configuration.listeners.push_back(listener);
    return configuration;
}

auto openai_service::make_template(chat_model_template_options options)
    -> chat_model_template
{
    return chat_model_template{
        model_pool_, std::move(options), session_gates_};
}

auto openai_service::reconfigure(chat_model_reconfiguration configuration,
    cancel_token* cancellation)
    -> task<std::expected<void, std::error_code>>
{
    auto* caller = io_context::current();
    if (caller != nullptr && caller != &io_)
        co_return co_await resume_on(*caller, starts_on(io_,
            reconfigure(std::move(configuration), cancellation)));
    auto parsed = parse_openai_settings(configuration.properties);
    if (!parsed)
        co_return std::unexpected(parsed.error());
    if (cancellation && cancellation->is_cancelled())
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));
    std::shared_ptr<openai_model_generation> next;
    try
    {
        next = std::make_shared<openai_model_generation>(
            io_, std::move(*parsed), telemetry_listener());
    }
    catch (const std::bad_alloc&)
    {
        co_return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
    co_await generation_gate_.lock();
    async_lock_guard guard{generation_gate_, std::adopt_lock};
    auto connected = co_await connect_generation(io_, *next, cancellation);
    if (!connected)
        co_return std::unexpected(connected.error());
    auto published = co_await model_pool_.reset(next->models);
    if (!published)
    {
        for (auto& client : next->clients)
            client->close();
        co_return std::unexpected(published.error());
    }
    generation_ = std::move(next);
    co_return std::expected<void, std::error_code>{};
}

auto openai_service::key() const -> service_key
{
    return {"openai", instance_};
}

auto openai_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto openai_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto openai_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    co_await generation_gate_.lock();
    async_lock_guard guard{generation_gate_, std::adopt_lock};
    if (!std::ranges::all_of(generation_->clients,
            [](const auto& client)
            {
                return client->is_connected();
            }))
    {
        auto connected = co_await connect_generation(
            io_, *generation_, &context.cancellation);
        if (!connected)
            co_return std::unexpected(connected.error());
    }
    if (auto reset = co_await model_pool_.reset(generation_->models); !reset)
        co_return std::unexpected(reset.error());
    co_return {};
}

auto openai_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    co_await generation_gate_.lock();
    async_lock_guard guard{generation_gate_, std::adopt_lock};
    co_await model_pool_.close();
    for (auto& client : generation_->clients)
        client->close();
    co_return {};
}

auto openai_service::probe(service_context& context) -> task<health_report>
{
    (void)context;
    if (!generation_gate_.try_lock())
        co_return health_report{
            .status = service_health::degraded,
            .message = "openai generation reconfiguration in progress"};
    async_lock_guard guard{generation_gate_, std::adopt_lock};
    const auto connected = static_cast<std::size_t>(std::ranges::count_if(
        generation_->clients, [](const auto& client)
        {
            return client->is_connected();
        }));
    co_return health_report{.status = connected == generation_->clients.size()
            ? service_health::up
            : connected > 0 ? service_health::degraded
                            : service_health::down,
        .message = std::format("openai pool connections {}/{}", connected,
            generation_->clients.size())};
}

auto auto_configure_openai(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    auto settings = parse_openai_settings(configuration.properties);
    if (!settings)
        return std::unexpected(settings.error());
    auto service = std::make_shared<openai_service>(context.io,
        context.telemetry,
        std::move(settings->connection), configuration.instance,
        configuration.requirement, configuration.recovery,
        settings->pool_size);
    auto registered = context.services.add_managed_named<chat_model_service>(
        configuration.instance, service);
    if (!registered)
        return registered;
    return context.services.add_named<openai_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
