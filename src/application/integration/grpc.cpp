module cnetmod.application.grpc;

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import std;
import cnetmod.json;

namespace cnetmod::application {

grpc_client_service::grpc_client_service(io_context& io, observability::telemetry_hub& telemetry,
    std::string base_url,
    grpc::client_options options, std::string instance,
    service_requirement requirement)
    : raw_client_(io, std::move(base_url), std::move(options)),
      client_(raw_client_, telemetry.spans(), telemetry.measurements()),
      instance_(std::move(instance)),
      requirement_(requirement)
{
}

auto grpc_client_service::client() noexcept -> observability::instrumented_grpc_client&
{
    return client_;
}

auto grpc_client_service::key() const -> service_key
{
    return {"grpc", instance_};
}

auto grpc_client_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto grpc_client_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    started_ = true;
    co_return {};
}

auto grpc_client_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    raw_client_.close();
    started_ = false;
    co_return {};
}

auto grpc_client_service::probe(service_context& context) -> task<health_report>
{
    (void)context;
    co_return health_report{
        .status = started_ ? service_health::up : service_health::stopped,
        .message = started_ ? "grpc client available" : "grpc client stopped",
    };
}

grpc_server_service::grpc_server_service(observability::telemetry_hub& telemetry, std::string instance,
    service_requirement requirement)
    : telemetry_(&telemetry), instance_(std::move(instance)), requirement_(requirement)
{
}

auto grpc_server_service::router() noexcept -> grpc::service_router&
{
    return router_;
}

auto grpc_server_service::handler() -> http::handler_fn
{
    return observability::grpc_server_handler(router_.make_http_handler(),
        telemetry_->spans(), telemetry_->measurements());
}

auto grpc_server_service::key() const -> service_key
{
    return {"grpc_server", instance_};
}

auto grpc_server_service::requirement() const noexcept
    -> service_requirement
{
    return requirement_;
}

auto grpc_server_service::start(service_context&)
    -> task<std::expected<void, std::error_code>>
{
    started_ = true;
    co_return {};
}

auto grpc_server_service::stop(service_context&)
    -> task<std::expected<void, std::error_code>>
{
    started_ = false;
    co_return {};
}

auto grpc_server_service::probe(service_context&) -> task<health_report>
{
    co_return health_report{
        .status = started_ ? service_health::up : service_health::stopped,
        .message = started_ ? "grpc server route available"
                            : "grpc server route stopped",
    };
}

auto auto_configure_grpc_client(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"base_url"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto base_url = configuration.properties.at("base_url")
                                  .get<std::string>();
        auto service = std::make_shared<grpc_client_service>(context.io, context.telemetry,
            base_url, grpc::client_options{}, configuration.instance,
            configuration.requirement);
        return context.services.add_managed_named<grpc_client_service>(
            configuration.instance, std::move(service));
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
}

auto auto_configure_grpc_server(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties, {"path"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        auto service = std::make_shared<grpc_server_service>(context.telemetry,
            configuration.instance, configuration.requirement);
        const auto path = cnetmod::json::value_or(configuration.properties, "path",
            std::string{"/*path"});
        context.routes.any(path, service->handler());
        return context.services.add_managed_named<grpc_server_service>(
            configuration.instance, std::move(service));
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
}

} // namespace cnetmod::application
#endif
