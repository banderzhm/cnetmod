module;

#include <cnetmod/config.hpp>

/**
 * @brief Provider-neutral chat model and conversation facade.
 */
export module cnetmod.application.chat_model_template;

#ifdef CNETMOD_HAS_CHAT_MODEL
import std;
import cnetmod.ai;
import cnetmod.application.chat_model_pool;
import cnetmod.coro.striped_mutex;
import cnetmod.coro.task;

namespace cnetmod::application {

/**
 * @brief Defaults applied to text-first model invocations.
 */
export struct chat_model_template_options
{
    /**
     * @brief Base request copied for each text-first invocation.
     */
    ai::chat_request request;

    /**
     * @brief Optional system instruction prepended before conversation history.
     */
    std::string system_prompt;
};

/**
 * @brief Controls the amount of persisted history loaded per model turn.
 */
export struct chat_conversation_options
{
    /**
     * @brief Maximum recent messages loaded before the current user input.
     *
     * Zero requests all messages from the store.
     */
    std::size_t history_limit = 64;
};

export class chat_conversation;

/**
 * @brief Provides pooled access to any registered chat model provider.
 *
 * The template borrows a lifecycle-managed pool. Each invocation owns one
 * exclusive lease until its complete response or stream terminal event. The
 * public contract depends only on cnetmod.ai, so provider adapters can target
 * OpenAI-compatible APIs, Claude, Gemini, or local inference runtimes.
 *
 * The template implements ai::chat_model, so provider-neutral decorators
 * (routed, resilient and governed models) compose directly over managed,
 * pooled and observed models.
 */
export class chat_model_template : public ai::chat_model
{
public:
    /**
     * @brief Binds the facade to a managed pool and shared session gates.
     */
    explicit chat_model_template(chat_model_pool& models,
        chat_model_template_options options = {},
        std::shared_ptr<striped_async_mutex<std::string>> session_gates = {});

    /**
     * @brief Invokes a pooled model with an explicit request.
     */
    auto invoke(ai::chat_request request,
        const ai::run_config& configuration = {})
        -> task<std::expected<ai::chat_response, std::string>> override;

    /**
     * @brief Invokes a pooled model with configured defaults and user input.
     */
    [[nodiscard]] auto invoke(std::string input,
        ai::run_config configuration = {})
        -> task<std::expected<ai::chat_response, std::string>>;

    /**
     * @brief Streams an explicit request while retaining its model lease.
     */
    auto stream(ai::chat_request request,
        ai::chat_model::stream_handler handler,
        const ai::run_config& configuration = {})
        -> task<std::expected<ai::chat_response, std::string>> override;

    /**
     * @brief Streams user input using configured request defaults.
     */
    [[nodiscard]] auto stream(std::string input,
        ai::chat_model::stream_handler handler,
        ai::run_config configuration = {})
        -> task<std::expected<ai::chat_response, std::string>>;

    /**
     * @brief Creates an append-only session facade over a conversation store.
     */
    [[nodiscard]] auto conversation(std::string session_id,
        ai::conversation_store& store,
        chat_conversation_options options = {}) -> chat_conversation;

private:
    [[nodiscard]] auto make_request(std::string input,
        std::vector<ai::message> history = {}) const -> ai::chat_request;

    chat_model_pool& models_;
    chat_model_template_options options_;
    std::shared_ptr<striped_async_mutex<std::string>> session_gates_;

    friend class chat_conversation;
};

/**
 * @brief Serializes and persists turns for one explicit conversation session.
 *
 * The store remains the single source of truth. A successful turn appends the
 * user and assistant messages atomically; failed model calls never mutate the
 * conversation. Same-session calls share a coroutine gate across templates.
 */
export class chat_conversation
{
public:
    /**
     * @brief Invokes the model with recent persisted session history.
     */
    [[nodiscard]] auto invoke(std::string input,
        ai::run_config configuration = {})
        -> task<std::expected<ai::chat_response, std::string>>;

    /**
     * @brief Streams one turn and persists it after successful completion.
     */
    [[nodiscard]] auto stream(std::string input,
        ai::chat_model::stream_handler handler,
        ai::run_config configuration = {})
        -> task<std::expected<ai::chat_response, std::string>>;

    /**
     * @brief Erases the complete persisted session.
     */
    [[nodiscard]] auto clear()
        -> task<std::expected<void, std::string>>;

    [[nodiscard]] auto session_id() const noexcept -> std::string_view;

private:
    chat_conversation(chat_model_template& owner, std::string session_id,
        ai::conversation_store& store, chat_conversation_options options);

    chat_model_template* owner_;
    std::string session_id_;
    ai::conversation_store* store_;
    chat_conversation_options options_;

    friend class chat_model_template;
};

} // namespace cnetmod::application
#endif
