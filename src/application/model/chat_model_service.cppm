module;

#include <cnetmod/config.hpp>

/**
 * @brief Provider-neutral managed chat model service contract.
 */
export module cnetmod.application.chat_model_service;

#ifdef CNETMOD_HAS_CHAT_MODEL
import cnetmod.application.chat_model_template;
import cnetmod.application.managed_service;

namespace cnetmod::application {

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
};

} // namespace cnetmod::application
#endif
