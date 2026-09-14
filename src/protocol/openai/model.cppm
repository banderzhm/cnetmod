/// cnetmod.protocol.openai:model — Provider-neutral model strategies

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:model;

import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.circuit_breaker;
import cnetmod.coro.semaphore;
import cnetmod.coro.rate_limiter;
import cnetmod.io.io_context;
import cnetmod.protocol.http.middleware.tracing;
import :foundation;
import :chat;
import :embeddings;
import :images;
import :moderation;
import :client;

namespace cnetmod::openai {

export enum class run_event_type
{
    model_start,
    model_end,
    model_error,
    model_retry,
    model_rejected,
    tool_start,
    tool_end,
    tool_error,
    retriever_start,
    retriever_end,
    retriever_error,
    agent_start,
    agent_end,
    agent_error
};

export struct run_event
{
    run_event_type type = run_event_type::model_start;
    std::string run_id;
    std::string name;
    std::string detail;
    std::size_t attempt = 0;
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();
    json attributes = {};
    /// Optional distributed-trace parent copied from run_config. It is a
    /// value, not a thread-local scope, so coroutine migration is safe.
    std::optional<http::tracing::trace_context> trace_parent;
    std::string parent_operation_id;
    /**
     * @brief Identifies this operation independently of optional JSON attributes.
     */
    std::string operation_id;
};

export using run_callback = std::function<void(const run_event&)>;

/// Observer receiving lifecycle events from nested model, tool and RAG calls.
export class run_listener
{
public:
    virtual ~run_listener() = default;
    virtual void on_event(const run_event& event) = 0;
};

export class functional_run_listener final : public run_listener
{
public:
    explicit functional_run_listener(run_callback callback);
    void on_event(const run_event& event) override;

private:
    run_callback callback_;
};

export struct run_config
{
    std::string run_id;
    std::vector<std::string> tags;
    std::map<std::string, std::string> metadata;
    run_callback callback;
    std::vector<run_listener*> listeners;
    cancel_token* cancellation = nullptr;
    std::optional<http::tracing::trace_context> trace_parent;
    std::string parent_operation_id;

    [[nodiscard]] auto is_cancelled() const noexcept -> bool;
    /**
     * @brief Returns whether a callback or non-null listener observes this run.
     */
    [[nodiscard]] auto has_observers() const noexcept -> bool;
    /**
     * @brief Synchronously notifies observers, borrowing fully populated events.
     *
     * A copy is made only when configuration supplies missing context or
     * metadata. Observers must not retain the borrowed reference. Recoverable
     * enrichment failures fall back to the original event, not a partial copy.
     */
    void notify(const run_event& event) const;

    /**
     * @brief Constructs an event only when a callback or listener is present.
     *
     * The factory executes synchronously and is not retained. Recoverable
     * preparation failures discard this optional notification, not the caller's
     * operation result. Prefer this API over constructing an event before notify.
     */
    template <typename Factory>
    void notify_lazy(Factory&& factory) const noexcept
    {
        if (!has_observers())
            return;
        try
        {
            notify(std::invoke(std::forward<Factory>(factory)));
        }
        catch (...)
        {
            // Optional event preparation must not interrupt model execution.
        }
    }
};

/**
 * @brief Owns lifecycle observation while preserving child invocation settings.
 *
 * Without observers, no operation identity or default event payload is created.
 * Observed scopes emit a terminal failure when abandoned without completion.
 */
export class run_scope
{
public:
    run_scope(const run_config& config, run_event_type start_type,
        run_event_type success_type, run_event_type error_type,
        std::string_view name, std::string_view detail = {},
        json attributes = {}) noexcept;
    ~run_scope();
    run_scope(const run_scope&) = delete;
    auto operator=(const run_scope&) -> run_scope& = delete;
    run_scope(run_scope&& other) noexcept;
    auto operator=(run_scope&& other) noexcept -> run_scope&;

    /**
     * @brief Creates start attributes only when the run has observers.
     *
     * Factories execute synchronously and are never retained. Optional metadata
     * failure falls back to an event without those attributes; initialization
     * failure disables this observation without changing invocation settings.
     */
    template <typename Factory>
    [[nodiscard]] static auto start_lazy(const run_config& config,
        run_event_type start_type, run_event_type success_type,
        run_event_type error_type, std::string_view name, Factory&& factory,
        std::string_view detail = {}) noexcept -> run_scope
    {
        if (config.has_observers())
        {
            try
            {
                return run_scope{config, start_type, success_type, error_type,
                    name, detail, std::invoke(std::forward<Factory>(factory))};
            }
            catch (...)
            {
                // Optional metadata must not prevent the underlying operation.
            }
        }
        return run_scope{config, start_type, success_type, error_type, name, detail};
    }

    void succeed(std::string_view detail = {}, std::size_t attempt = 0,
        json attributes = {});
    void fail(std::string_view detail = {}, std::size_t attempt = 0,
        json attributes = {});

    /**
     * @brief Builds success attributes only for an active observed operation.
     *
     * The factory runs synchronously at most once. Factory failures discard
     * optional attributes without changing the successful operation outcome.
     */
    template <typename Factory>
    void succeed_lazy(Factory&& factory, std::string_view detail = {},
        std::size_t attempt = 0) noexcept
    {
        finish_lazy(success_type_, std::forward<Factory>(factory), detail, attempt);
    }

    /**
     * @brief Builds failure attributes without evaluating disabled observation.
     */
    template <typename Factory>
    void fail_lazy(Factory&& factory, std::string_view detail = {},
        std::size_t attempt = 0) noexcept
    {
        finish_lazy(error_type_, std::forward<Factory>(factory), detail, attempt);
    }

    [[nodiscard]] auto operation_id() const noexcept -> std::string_view;
    /**
     * @brief Preserves cancellation and metadata for nested invocations.
     *
     * An active observed scope also supplies the child's parent operation ID.
     */
    [[nodiscard]] auto child_config() const -> run_config;

private:
    template <typename Factory>
    void finish_lazy(run_event_type type, Factory&& factory,
        std::string_view detail, std::size_t attempt) noexcept
    {
        if (!config_)
            return;
        try
        {
            finish(type, detail, attempt, std::invoke(std::forward<Factory>(factory)));
        }
        catch (...)
        {
            finish(type, detail, attempt, {});
        }
    }

    void finish(run_event_type type, std::string_view detail,
        std::size_t attempt, json attributes) noexcept;

    const run_config* config_ = nullptr;
    const run_config* source_config_ = nullptr;
    run_event_type success_type_ = run_event_type::model_end;
    run_event_type error_type_ = run_event_type::model_error;
    std::string name_;
    std::string operation_id_;
};

/// Strategy interface shared by chains and agents.
export class chat_model
{
public:
    using stream_handler = std::function<task<bool>(const chat_chunk&)>;

    virtual ~chat_model() = default;
    virtual auto invoke(chat_request request, const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> = 0;
    virtual auto stream(chat_request request, stream_handler handler,
        const run_config& config = {})
        -> task<std::expected<chat_response, std::string>>;
};

/// Adapter from the low-level OpenAI client to the provider-neutral strategy.
export class openai_chat_model final : public chat_model
{
public:
    explicit openai_chat_model(client& api) noexcept;
    auto invoke(chat_request request, const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;
    auto stream(chat_request request, stream_handler handler,
        const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;

private:
    client& api_;
};

export class chat_model_router
{
public:
    virtual ~chat_model_router() = default;
    virtual auto select(const chat_request& request,
        const run_config& config)
        -> task<std::expected<chat_model*, std::string>> = 0;
};

export using model_routing_handler = std::function<task<
    std::expected<chat_model*, std::string>>(
    const chat_request& request, const run_config& config)>;

/// Adapter for application-defined asynchronous model selection policies.
export class functional_chat_model_router final : public chat_model_router
{
public:
    explicit functional_chat_model_router(model_routing_handler handler);
    auto select(const chat_request& request, const run_config& config)
        -> task<std::expected<chat_model*, std::string>> override;

private:
    model_routing_handler handler_;
};

/// Strategy context delegating each invocation to a dynamically selected model.
export class routed_chat_model final : public chat_model
{
public:
    explicit routed_chat_model(chat_model_router& router,
        chat_model* fallback = nullptr) noexcept;

    auto invoke(chat_request request, const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;
    auto stream(chat_request request, stream_handler handler,
        const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;

private:
    auto route(const chat_request& request, const run_config& config)
        -> task<std::expected<chat_model*, std::string>>;

    chat_model_router& router_;
    chat_model* fallback_;
};

export struct resilient_model_options
{
    std::size_t max_attempts_per_model = 3;
    std::chrono::milliseconds initial_backoff{100};
    std::chrono::milliseconds max_backoff{2000};
};

/// Decorator adding exponential retry and ordered provider fallbacks.
export class resilient_chat_model final : public chat_model
{
public:
    resilient_chat_model(io_context& context, chat_model& primary,
        std::vector<chat_model*> fallbacks = {},
        resilient_model_options options = {});

    auto invoke(chat_request request, const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;
    auto stream(chat_request request, stream_handler handler,
        const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;

private:
    io_context& context_;
    chat_model& primary_;
    std::vector<chat_model*> fallbacks_;
    resilient_model_options options_;
};

export struct governed_model_options
{
    std::size_t max_concurrency = 32;
    circuit_breaker_options circuit_breaker{};
    rate_limit request_rate{};
};

/// Decorator applying bulkhead isolation and circuit-breaker protection.
export class governed_chat_model final : public chat_model
{
public:
    explicit governed_chat_model(chat_model& delegate,
        governed_model_options options = {});

    auto invoke(chat_request request, const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;
    auto stream(chat_request request, stream_handler handler,
        const run_config& config = {})
        -> task<std::expected<chat_response, std::string>> override;

    [[nodiscard]] auto circuit_state() const noexcept -> circuit_breaker_state;
    void reset_circuit() noexcept;

private:
    chat_model& delegate_;
    async_semaphore permits_;
    circuit_breaker breaker_;
    token_bucket request_rate_;
    bool rate_limit_enabled_ = false;
};

export class embedding_model
{
public:
    virtual ~embedding_model() = default;
    virtual auto embed_documents(std::vector<std::string> texts)
        -> task<std::expected<std::vector<std::vector<float>>, std::string>> = 0;
    virtual auto embed_query(std::string text)
        -> task<std::expected<std::vector<float>, std::string>> = 0;
};

export class openai_embedding_model final : public embedding_model
{
public:
    explicit openai_embedding_model(client& api,
        std::string model = "text-embedding-3-small",
        std::optional<int> dimensions = std::nullopt);

    auto embed_documents(std::vector<std::string> texts)
        -> task<std::expected<std::vector<std::vector<float>>, std::string>> override;
    auto embed_query(std::string text)
        -> task<std::expected<std::vector<float>, std::string>> override;

private:
    client& api_;
    std::string model_;
    std::optional<int> dimensions_;
};

export class image_model
{
public:
    virtual ~image_model() = default;
    virtual auto generate(image_generation_request request,
        const run_config& config = {})
        -> task<std::expected<image_response, std::string>> = 0;
};

export class openai_image_model final : public image_model
{
public:
    explicit openai_image_model(client& api) noexcept;
    auto generate(image_generation_request request,
        const run_config& config = {})
        -> task<std::expected<image_response, std::string>> override;

private:
    client& api_;
};

export class moderation_model
{
public:
    virtual ~moderation_model() = default;
    virtual auto moderate(moderation_request request,
        const run_config& config = {})
        -> task<std::expected<moderation_response, std::string>> = 0;
};

export class openai_moderation_model final : public moderation_model
{
public:
    explicit openai_moderation_model(client& api) noexcept;
    auto moderate(moderation_request request,
        const run_config& config = {})
        -> task<std::expected<moderation_response, std::string>> override;

private:
    client& api_;
};

} // namespace cnetmod::openai
