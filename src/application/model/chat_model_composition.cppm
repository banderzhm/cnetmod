module;

#include <cnetmod/config.hpp>

/**
 * @brief Explicitly composes managed chat model instances.
 *
 * Multi-instance routing and cross-instance fallback are opt-in. Merely listing
 * several instances never enables either behavior implicitly.
 */
export module cnetmod.application.chat_model_composition;

#ifdef CNETMOD_HAS_CHAT_MODEL
import std;
import cnetmod.ai;
import cnetmod.application.chat_model_service;
import cnetmod.application.chat_model_template;
import cnetmod.application.components;
import cnetmod.application.runtime;
import cnetmod.application.service_registry;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::application {

export enum class chat_model_routing
{
    round_robin,
    ordered,
};

export struct chat_model_composition
{
    std::vector<std::string> instances;
    /// Required when more than one instance is configured. This prevents
    /// accidental load balancing merely because a second binding was added.
    std::optional<chat_model_routing> routing;
    /// Retry policy for each selected instance. This alone does not switch to
    /// another instance.
    std::optional<ai::resilient_model_options> resilience;
    /// Explicit permission to use remaining instances as ordered fallbacks.
    bool fallback_to_remaining_instances = false;
    std::optional<ai::governed_model_options> governance;
    chat_model_template_options template_options;
};

export class composed_chat_model final : public ai::chat_model
{
public:
    [[nodiscard]] static auto create(service_registry& services,
        execution_context& executor, chat_model_composition composition)
        -> std::expected<std::shared_ptr<composed_chat_model>, std::error_code>;

    composed_chat_model(const composed_chat_model&) = delete;
    auto operator=(const composed_chat_model&) -> composed_chat_model& = delete;

    auto invoke(ai::chat_request request, const ai::run_config& config = {})
        -> task<std::expected<ai::chat_response, std::string>> override;
    auto stream(ai::chat_request request, stream_handler handler,
        const ai::run_config& config = {})
        -> task<std::expected<ai::chat_response, std::string>> override;

    [[nodiscard]] auto instances() const noexcept
        -> std::span<const std::string>;

private:
    composed_chat_model() = default;

    [[nodiscard]] auto select() -> ai::chat_model*;

    std::vector<std::string> instances_;
    chat_model_routing routing_ = chat_model_routing::ordered;
    std::vector<std::unique_ptr<chat_model_template>> templates_;
    std::vector<std::unique_ptr<ai::chat_model>> candidates_;
    std::unique_ptr<ai::functional_chat_model_router> router_;
    std::unique_ptr<ai::routed_chat_model> routed_;
    std::unique_ptr<ai::governed_chat_model> governed_;
    ai::chat_model* top_ = nullptr;
    io_context* event_loop_ = nullptr;
    std::atomic<std::size_t> cursor_{0};
};

export auto add_chat_model(component_collection& components, std::string name,
    chat_model_composition composition) -> component_collection&;

} // namespace cnetmod::application
#endif
