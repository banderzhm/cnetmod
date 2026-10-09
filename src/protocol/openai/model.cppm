/// Provider-neutral model contracts and optional model decorators.
export module cnetmod.protocol.openai:model;

import std;
import cnetmod.coro.circuit_breaker;
import cnetmod.coro.rate_limiter;
import cnetmod.coro.semaphore;
import cnetmod.coro.task;
import cnetmod.io.io_context;
export import :run;
import :chat;
import :images;
import :moderation;
import :client;

namespace cnetmod::openai {

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

export class functional_chat_model_router final : public chat_model_router
{
public:
    explicit functional_chat_model_router(model_routing_handler handler);
    auto select(const chat_request& request, const run_config& config)
        -> task<std::expected<chat_model*, std::string>> override;

private:
    model_routing_handler handler_;
};

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
