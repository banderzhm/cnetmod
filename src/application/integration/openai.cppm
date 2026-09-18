module;

#include <cnetmod/config.hpp>

/// Managed OpenAI client connection.
export module cnetmod.application.openai;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.ai;
import cnetmod.application.auto_configuration;
import cnetmod.application.chat_model_pool;
import cnetmod.application.chat_model_service;
import cnetmod.application.chat_model_template;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.task_group;
import cnetmod.coro.mutex;
import cnetmod.coro.striped_mutex;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.observability.openai;
import cnetmod.protocol.openai;

namespace cnetmod::application {

class openai_model_generation;

export class openai_service final : public chat_model_service
{
public:
    openai_service(io_context& io,
        observability::telemetry_hub& telemetry,
        openai::connect_options options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery, std::size_t pool_size = 4);
    /**
     * @brief Returns a lifetime-safe snapshot of the current provider client.
     *
     * Provider-neutral application code should use make_template(). This escape
     * hatch is asynchronous so it cannot race a generation publication.
     */
    [[nodiscard]] auto current_client()
        -> task<std::shared_ptr<openai::client>>;
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
    /**
     * @brief Creates a model template bound to this managed connection.
     */
    [[nodiscard]] auto make_template(
        chat_model_template_options options = {}) -> chat_model_template override;
    [[nodiscard]] auto reconfigure(
        chat_model_reconfiguration configuration,
        cancel_token* cancellation = nullptr)
        -> task<std::expected<void, std::error_code>> override;
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
    io_context& io_;
    std::optional<openai::telemetry_listener> telemetry_listener_;
    chat_model_pool model_pool_;
    async_mutex generation_gate_;
    std::shared_ptr<openai_model_generation> generation_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    std::shared_ptr<striped_async_mutex<std::string>> session_gates_;
};

export [[nodiscard]] auto auto_configure_openai(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
