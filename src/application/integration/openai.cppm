module;

#include <cnetmod/config.hpp>

/// Managed OpenAI client connection.
export module cnetmod.application.openai;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.observability.openai;
import cnetmod.protocol.openai;

namespace cnetmod::application {

export class openai_service final : public managed_service
{
public:
    openai_service(io_context& io,
        observability::telemetry_hub& telemetry,
        openai::connect_options options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery);
    [[nodiscard]] auto client() noexcept -> openai::client&;
    /**
     * @brief Returns the optional listener; null means observation is disabled.
     */
    [[nodiscard]] auto telemetry_listener() noexcept
        -> openai::telemetry_listener*;
    /**
     * @brief Adds the managed listener once while preserving caller configuration.
     *
     * Returned listener pointers remain valid while this service is alive.
     */
    [[nodiscard]] auto run_configuration(openai::run_config configuration = {})
        -> openai::run_config;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    openai::client client_;
    std::optional<openai::telemetry_listener> telemetry_listener_;
    openai::connect_options options_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
};

export [[nodiscard]] auto auto_configure_openai(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
