module;
#include <cnetmod/config.hpp>
/**
 * @brief Managed AMQP 0-9-1 client and frame pump.
 */
export module cnetmod.application.amqp091;
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.amqp091;

namespace cnetmod::application {
export class amqp091_service final : public managed_service
{
public:
    amqp091_service(io_context&, amqp091::connection_options, std::string,
        service_requirement, recovery_policy);
    [[nodiscard]] auto client() noexcept -> amqp091::amqp091_client&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    /**
     * @brief Retains shutdown ownership even when frame-pump registration fails.
     */
    [[nodiscard]] auto shutdown_required() const noexcept -> bool override;
    /**
     * @brief Starts the connection and registers its frame pump.
     *
     * Rejects an already cancelled or expired operation before network work.
     * Cancellation takes precedence when both admission conditions fail.
     */
    auto start(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context&) -> task<health_report> override;

private:
    struct session_startup;
    auto run_session(std::shared_ptr<session_startup>, cancel_token&) -> task<std::expected<void, std::error_code>>;
    auto wait_for_startup(std::shared_ptr<session_startup>, cancel_token&) -> task<std::expected<void, std::error_code>>;
    std::shared_ptr<session_startup> startup_;
    amqp091::connection_options options_;
    amqp091::amqp091_client client_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
};

export [[nodiscard]] auto auto_configure_amqp091(const configured_service&,
    auto_configuration_context&) -> std::expected<void, std::error_code>;
} // namespace cnetmod::application
#endif
