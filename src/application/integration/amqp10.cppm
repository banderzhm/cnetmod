module;
#include <cnetmod/config.hpp>
/// Managed AMQP 1.0 client.
export module cnetmod.application.amqp10;
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.amqp10;
namespace cnetmod::application {
export class amqp10_service final : public managed_service
{
public:
    amqp10_service(io_context&, amqp10::client_options, std::string,
        service_requirement, recovery_policy);
    [[nodiscard]] auto client() noexcept -> amqp10::client&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    auto start(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context&) -> task<health_report> override;
private:
    amqp10::client_options options_;
    amqp10::client client_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
};
export [[nodiscard]] auto auto_configure_amqp10(const configured_service&,
    auto_configuration_context&) -> std::expected<void, std::error_code>;
} // namespace cnetmod::application
#endif
