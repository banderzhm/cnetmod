module cnetmod.application.openai;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.ai;
import cnetmod.observability.openai;
import cnetmod.coro.striped_mutex;

namespace cnetmod::application {

namespace {

    class observed_openai_chat_model final : public ai::chat_model
    {
    public:
        observed_openai_chat_model(openai::client& client,
            openai::run_listener* listener) noexcept
            : model_(client), listener_(listener)
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

        openai::openai_chat_model model_;
        openai::run_listener* listener_;
    };

} // namespace

openai_service::openai_service(io_context& io,
    observability::telemetry_hub& telemetry, openai::connect_options options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery, std::size_t pool_size)
    : io_(io),
      model_pool_(io_),
      options_(std::move(options)),
      instance_(std::move(instance)),
      requirement_(requirement),
      recovery_(recovery),
      pool_size_(pool_size),
      session_gates_(
          std::make_shared<striped_async_mutex<std::string>>())
{
    if (pool_size_ == 0)
        throw std::invalid_argument("OpenAI pool size must be positive");
    auto sink = telemetry.spans();
    if (sink || telemetry.records_metrics())
    {
        openai::telemetry_options settings;
        settings.record_metrics = telemetry.records_metrics();
        telemetry_listener_.emplace(telemetry.measurements(), std::move(sink),
            std::move(settings));
    }
    clients_.reserve(pool_size_);
    models_.reserve(pool_size_);
    for (std::size_t index = 0; index < pool_size_; ++index)
    {
        auto client = std::make_unique<openai::client>(io_);
        models_.push_back(std::make_shared<observed_openai_chat_model>(
            *client, telemetry_listener()));
        clients_.push_back(std::move(client));
    }
}

auto openai_service::client() noexcept -> openai::client&
{
    return *clients_.front();
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
    (void)context;
    if (std::ranges::all_of(clients_,
            [](const auto& client)
            {
                return client->is_connected();
            }))
        co_return {};
    for (auto& client : clients_)
    {
        if (context.cancellation.is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        if (client->is_connected())
            continue;
        auto connected = co_await client->connect(options_);
        if (!connected)
        {
            for (auto& opened : clients_)
                opened->close();
            co_return std::unexpected(
                std::make_error_code(std::errc::connection_refused));
        }
    }
    if (auto reset = co_await model_pool_.reset(models_); !reset)
        co_return std::unexpected(reset.error());
    co_return {};
}

auto openai_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    co_await model_pool_.close();
    for (auto& client : clients_)
        client->close();
    co_return {};
}

auto openai_service::probe(service_context& context) -> task<health_report>
{
    (void)context;
    const auto connected = static_cast<std::size_t>(std::ranges::count_if(
        clients_, [](const auto& client)
        {
            return client->is_connected();
        }));
    co_return health_report{.status = connected == clients_.size()
            ? service_health::up
            : connected > 0 ? service_health::degraded
                            : service_health::down,
        .message = std::format("openai pool connections {}/{}", connected,
            clients_.size())};
}

auto auto_configure_openai(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties,
            {"base_url", "api_key", "tls_verify", "timeout_seconds",
                "pool_size"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    openai::connect_options options;
    if (!integer_property_in_range(configuration.properties, "timeout_seconds", 1,
            std::numeric_limits<int>::max()))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(
            configuration.properties, "pool_size", 1, 1024))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    std::size_t pool_size = 4;
    try
    {
        options.api_base = configuration.properties.value("base_url",
            options.api_base);
        options.api_key = configuration.properties.value("api_key",
            options.api_key);
        options.tls_verify = configuration.properties.value("tls_verify",
            options.tls_verify);
        options.timeout_seconds = configuration.properties.value(
            "timeout_seconds", options.timeout_seconds);
        pool_size = configuration.properties.value("pool_size", pool_size);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    if (options.api_key.empty())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    auto service = std::make_shared<openai_service>(context.io,
        context.telemetry,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery, pool_size);
    auto registered = context.services.add_managed_named<chat_model_service>(
        configuration.instance, service);
    if (!registered)
        return registered;
    return context.services.add_named<openai_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
