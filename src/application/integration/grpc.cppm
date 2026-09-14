module;

#include <cnetmod/config.hpp>

/// Managed gRPC client facade.
export module cnetmod.application.grpc;

#ifdef CNETMOD_HAS_PROTOCOL_GRPC
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.observability.grpc;
import cnetmod.observability.grpc_server;
import cnetmod.protocol.grpc;
import cnetmod.protocol.http;

namespace cnetmod::application {

export class grpc_client_service final : public managed_service
{
public:
    grpc_client_service(io_context& io, observability::telemetry_hub& telemetry,
        std::string base_url,
        grpc::client_options options, std::string instance,
        service_requirement requirement);
    /** @brief Returns the automatically observed client facade. */
    [[nodiscard]] auto client() noexcept -> observability::instrumented_grpc_client&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    grpc::client raw_client_;
    observability::instrumented_grpc_client client_;
    std::string instance_;
    service_requirement requirement_;
    bool started_ = false;
};

export class grpc_server_service final : public managed_service
{
public:
    grpc_server_service(observability::telemetry_hub& telemetry, std::string instance,
        service_requirement requirement);
    [[nodiscard]] auto router() noexcept -> grpc::service_router&;
    /** @brief Returns the router handler with optional RPC-semantic observation. */
    [[nodiscard]] auto handler() -> http::handler_fn;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    grpc::service_router router_;
    observability::telemetry_hub* telemetry_;
    std::string instance_;
    service_requirement requirement_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_grpc_client(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;
export [[nodiscard]] auto auto_configure_grpc_server(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
