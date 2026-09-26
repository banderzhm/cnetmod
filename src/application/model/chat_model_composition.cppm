module;

#include <cnetmod/config.hpp>

/**
 * @brief Composes managed chat model instances into one provider-neutral model.
 *
 * Each configured instance (for example one per API key or per provider) is a
 * lifecycle-managed, pooled and observed chat_model_template. The composition
 * applies, from inside out:
 *
 * 1. resilience: per-instance retry, then ordered fallback to the remaining
 *    instances. Retries never happen after the first stream chunk was
 *    delivered, so a client never receives duplicated or spliced output;
 * 2. routing: round-robin spreads load across instances, ordered always
 *    starts with the first instance;
 * 3. governance: bulkhead concurrency, circuit breaker and request rate.
 *
 * The result is an ai::chat_model, so agents, AI services and further
 * decorators compose over it unchanged.
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

namespace cnetmod::application {

/**
 * @brief Instance selection policy for each call.
 */
export enum class chat_model_routing
{
    /// Rotate the starting instance per call; fallbacks follow in order.
    round_robin,
    /// Always start with the first instance; fallbacks follow in order.
    ordered,
};

/**
 * @brief Declarative composition of managed chat model instances.
 */
export struct chat_model_composition
{
    /// Managed chat_model_service instance names; at least one.
    std::vector<std::string> instances;
    chat_model_routing routing = chat_model_routing::round_robin;
    /// Enables retry and fallback across instances when set.
    std::optional<ai::resilient_model_options> resilience;
    /// Enables bulkhead, circuit breaker and rate limit when set.
    std::optional<ai::governed_model_options> governance;
    /// Request defaults shared by every instance template.
    chat_model_template_options template_options;
};

/**
 * @brief ai::chat_model backed by several managed instances.
 */
export class composed_chat_model final : public ai::chat_model
{
public:
    /**
     * @brief Resolves every instance and assembles the decorator chain.
     */
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

    /**
     * @brief Instance names in declaration order.
     */
    [[nodiscard]] auto instances() const noexcept
        -> std::span<const std::string>;

private:
    composed_chat_model() = default;

    [[nodiscard]] auto select() -> ai::chat_model*;

    std::vector<std::string> instances_;
    chat_model_routing routing_ = chat_model_routing::round_robin;
    std::vector<std::unique_ptr<chat_model_template>> templates_;
    std::vector<std::unique_ptr<ai::chat_model>> candidates_;
    std::unique_ptr<ai::functional_chat_model_router> router_;
    std::unique_ptr<ai::routed_chat_model> routed_;
    std::unique_ptr<ai::governed_chat_model> governed_;
    ai::chat_model* top_ = nullptr;
    std::atomic<std::size_t> cursor_{0};
};

/**
 * @brief Registers a composed model as the ai::chat_model component `name`.
 */
export auto add_chat_model(component_collection& components, std::string name,
    chat_model_composition composition) -> component_collection&;

} // namespace cnetmod::application
#endif
