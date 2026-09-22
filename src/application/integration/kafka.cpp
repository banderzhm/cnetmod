module cnetmod.application.kafka;
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.json;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

namespace cnetmod::application {
namespace {
    /**
     * @brief Preserves Kafka error identity without exposing broker diagnostics.
     */
    class kafka_category final : public std::error_category
    {
    public:
        auto name() const noexcept -> const char* override
        {
            return "cnetmod.kafka";
        }

        auto message(int value) const -> std::string override
        {
            return std::format("Kafka error {}", value);
        }

        auto default_error_condition(int value) const noexcept -> std::error_condition override
        {
            switch (static_cast<kafka::error_code>(value))
            {
            case kafka::error_code::configuration:
                return std::make_error_condition(std::errc::invalid_argument);
            case kafka::error_code::transport:
                return std::make_error_condition(std::errc::io_error);
            case kafka::error_code::malformed_response:
                return std::make_error_condition(std::errc::protocol_error);
            case kafka::error_code::topic_authorization_failed:
            case kafka::error_code::group_authorization_failed:
            case kafka::error_code::cluster_authorization_failed:
            case kafka::error_code::transactional_id_authorization_failed:
                return std::make_error_condition(std::errc::permission_denied);
            default:
                return {value, *this};
            }
        }
    };

    /**
     * @brief Adapts Kafka operations to the lifecycle cancellation contract.
     */
    auto lifecycle_operation(task<kafka::result<void>> operation, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>
    {
        if (cancellation.is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        auto result = co_await std::move(operation);
        if (result)
            co_return {};
        if (cancellation.is_cancelled() || result.error().code == kafka::error_code::cancelled)
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (result.error().code == kafka::error_code::request_timed_out)
            co_return std::unexpected(std::make_error_code(std::errc::timed_out));
        static const kafka_category category;
        co_return std::unexpected(std::error_code{static_cast<int>(result.error().code), category});
    }
} // namespace

kafka_service::kafka_service(io_context& io, kafka::client_options options,
    std::string instance, service_requirement requirement, recovery_policy recovery,
    instrumentation::span_exporter sink)
    : client_(io, std::move(options)), instance_(std::move(instance)), requirement_(requirement), recovery_(recovery), sink_(std::move(sink)) {}

auto kafka_service::make_producer(kafka::producer_options options, std::unique_ptr<kafka::partitioner> partitioner)
    -> kafka::result<observability::instrumented_kafka_producer>
{
    auto producer = client_.make_producer(std::move(options), std::move(partitioner));
    if (!producer)
        return std::unexpected(std::move(producer.error()));
    instrumentation::span_exporter sink;
    try
    {
        sink = sink_;
    }
    catch (...)
    {
        // An unavailable observation sink must not invalidate a usable producer.
    }
    return observability::instrumented_kafka_producer{std::move(*producer), std::move(sink)};
}

auto kafka_service::client() noexcept -> kafka::client_facade&
{
    return client_;
}

auto kafka_service::key() const -> service_key
{
    return {"kafka", instance_};
}

auto kafka_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto kafka_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto kafka_service::start(service_context& context) -> task<std::expected<void, std::error_code>>
{
    if (started_)
    {
        if (context.cancellation.is_cancelled())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        co_return client_.restart_failed_maintenance();
    }
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        lifecycle_operation(client_.connect(&context.cancellation), context.cancellation),
        context.cancellation);
    if (!result)
        co_return std::unexpected(result.error());
    started_ = true;
    co_return {};
}

auto kafka_service::stop(service_context& context) -> task<std::expected<void, std::error_code>>
{
    if (!client_.requires_async_close())
    {
        client_.close();
        started_ = false;
        co_return {};
    }
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        lifecycle_operation(client_.async_close(&context.cancellation), context.cancellation),
        context.cancellation);
    if (!result)
        co_return std::unexpected(result.error());
    started_ = false;
    co_return {};
}

auto kafka_service::probe(service_context& context) -> task<health_report>
{
    if (!started_)
        co_return health_report{.status = service_health::down, .message = "kafka client unavailable"};
    if (const auto failure = client_.background_error())
        co_return health_report{.status = service_health::down,
            .message = "kafka consumer maintenance failed",
            .error = failure};
    auto result = co_await with_deadline(context.io, context.operation_deadline,
        lifecycle_operation(client_.refresh_metadata({}, &context.cancellation), context.cancellation),
        context.cancellation);
    if (const auto failure = client_.background_error())
        co_return health_report{.status = service_health::down,
            .message = "kafka consumer maintenance failed",
            .error = failure};
    co_return health_report{.status = result ? service_health::up : service_health::down,
        .message = result ? "kafka metadata available" : "kafka metadata unavailable",
        .error = result ? std::error_code{} : result.error()};
}

auto auto_configure_kafka(const configured_service& configuration,
    auto_configuration_context& context) -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"host", "port", "client_id", "username", "password"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        kafka::client_options options;
        kafka::client_endpoint endpoint;
        endpoint.host = cnetmod::json::value_or(
            configuration.properties, "host", endpoint.host);
        endpoint.port = cnetmod::json::value_or(
            configuration.properties, "port", endpoint.port);
        options.bootstrap_servers.push_back(std::move(endpoint));
        options.client_id = cnetmod::json::value_or(
            configuration.properties, "client_id", options.client_id);
        options.credentials.username = cnetmod::json::value_or(
            configuration.properties, "username", std::string{});
        options.credentials.password = cnetmod::json::value_or(
            configuration.properties, "password", std::string{});
        auto service = std::make_shared<kafka_service>(context.io, std::move(options),
            configuration.instance, configuration.requirement, configuration.recovery, context.telemetry.spans());
        return context.services.add_managed_named<kafka_service>(
            configuration.instance, std::move(service));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
}
} // namespace cnetmod::application
#endif
