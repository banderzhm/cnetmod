module;

#include <cnetmod/config.hpp>

/**
 * @brief Provider-neutral managed chat model service contract.
 */
export module cnetmod.application.chat_model_service;

#ifdef CNETMOD_HAS_CHAT_MODEL
import std;
import cnetmod.application.chat_model_template;
import cnetmod.application.managed_service;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.json;

namespace cnetmod::application {

/**
 * @brief Complete provider-owned settings for one chat model generation.
 *
 * The selected provider validates the property schema. Callers must supply a
 * complete replacement rather than a partial patch so credentials and endpoint
 * changes are committed as one generation.
 */
export struct chat_model_reconfiguration
{
    cnetmod::json::document properties = cnetmod::json::object();
};

/**
 * @brief Exposes lifecycle-managed chat model capacity to Application code.
 */
export class chat_model_service : public managed_service
{
public:
    /**
     * @brief Creates a provider-neutral template backed by this service.
     *
     * Implementations keep connection ownership and session coordination
     * private so Application consumers never depend on a provider adapter.
     */
    [[nodiscard]] virtual auto make_template(
        chat_model_template_options options = {}) -> chat_model_template = 0;

    /**
     * @brief Builds and atomically publishes a validated provider generation.
     *
     * A failed build leaves the active generation untouched. Existing leases
     * remain valid after a successful publication and retire naturally.
     */
    [[nodiscard]] virtual auto reconfigure(
        chat_model_reconfiguration configuration,
        cancel_token* cancellation = nullptr)
        -> task<std::expected<void, std::error_code>> = 0;
};

} // namespace cnetmod::application
#endif
