/// Managed trace-aware outbound HTTP client.
export module cnetmod.application.http_client;

import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.observability.http;
import cnetmod.protocol.http;

namespace cnetmod::application {

export class http_client_service final : public managed_service
{
public:
    http_client_service(io_context& io, observability::telemetry_hub& telemetry,
        http::client_options options, std::string instance,
        service_requirement requirement);
    [[nodiscard]] auto client() noexcept
        -> observability::instrumented_http_client&;
    [[nodiscard]] auto raw_client() noexcept -> http::client&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    http::client raw_client_;
    observability::instrumented_http_client client_;
    std::string instance_;
    service_requirement requirement_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_http_client(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
