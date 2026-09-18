module;

#include <cnetmod/config.hpp>

/**
 * @brief Application-oriented facade for OpenAI chat models.
 */
export module cnetmod.application.openai_template;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.protocol.openai;

namespace cnetmod::application {

/**
 * @brief Defaults applied to text-first model invocations.
 */
export struct openai_template_options
{
    /**
     * @brief Base request copied for each text-first invocation.
     */
    openai::chat_request request;

    /**
     * @brief Optional system instruction prepended before default messages.
     */
    std::string system_prompt;
};

/**
 * @brief Provides observable and serialized access to an OpenAI chat model.
 *
 * The facade borrows its model and listener. Application code normally obtains
 * it from application_runtime::openai(). Every operation preserves caller run
 * metadata and cancellation while adding the application telemetry listener
 * exactly once. A shared asynchronous gate prevents concurrent requests from
 * interleaving on a lifecycle-managed OpenAI connection.
 */
export class openai_template
{
public:
    /**
     * @brief Binds the facade to a model and optional application observer.
     */
    explicit openai_template(openai::chat_model& model,
        openai_template_options options = {},
        openai::run_listener* listener = nullptr,
        std::shared_ptr<async_mutex> request_gate = {});

    /**
     * @brief Invokes the model with an explicit protocol request.
     */
    [[nodiscard]] auto invoke(openai::chat_request request,
        openai::run_config configuration = {})
        -> task<std::expected<openai::chat_response, std::string>>;

    /**
     * @brief Invokes the model with a user message and configured defaults.
     */
    [[nodiscard]] auto invoke(std::string input,
        openai::run_config configuration = {})
        -> task<std::expected<openai::chat_response, std::string>>;

    /**
     * @brief Streams an explicit request through an asynchronous chunk handler.
     */
    [[nodiscard]] auto stream(openai::chat_request request,
        openai::chat_model::stream_handler handler,
        openai::run_config configuration = {})
        -> task<std::expected<openai::chat_response, std::string>>;

    /**
     * @brief Streams a user message using configured request defaults.
     */
    [[nodiscard]] auto stream(std::string input,
        openai::chat_model::stream_handler handler,
        openai::run_config configuration = {})
        -> task<std::expected<openai::chat_response, std::string>>;

private:
    [[nodiscard]] auto make_request(std::string input) const
        -> openai::chat_request;
    [[nodiscard]] auto observe(openai::run_config configuration) const
        -> openai::run_config;

    openai::chat_model& model_;
    openai_template_options options_;
    openai::run_listener* listener_ = nullptr;
    std::shared_ptr<async_mutex> request_gate_;
};

} // namespace cnetmod::application
#endif
