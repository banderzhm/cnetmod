/// cnetmod.protocol.openai:model — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.coro.circuit_breaker;
import cnetmod.coro.semaphore;
import cnetmod.coro.rate_limiter;
import :foundation;
import :chat;
import :embeddings;
import :images;
import :moderation;
import :client;
import :model;

namespace cnetmod::openai {

auto run_config::is_cancelled() const noexcept -> bool
{
    return cancellation && cancellation->is_cancelled();
}

namespace {
    auto next_operation_id(std::string_view run_id) -> std::string
    {
        static std::atomic<std::uint64_t> sequence{0};
        return std::format("{}{}{}", run_id, run_id.empty() ? "" : "-",
            sequence.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    void report_observer_failure() noexcept
    {
        try
        {
            logger::warn{"OpenAI run observer failed; exception details omitted"};
        }
        catch (...)
        {
            // Diagnostic logging must not suppress subsequent observers.
        }
    }

    /**
     * @brief Delivers a borrowed event synchronously with per-observer isolation.
     */
    void deliver_event(const run_config& config, const run_event& event) noexcept
    {
        if (config.callback)
        {
            try
            {
                config.callback(event);
            }
            catch (...)
            {
                report_observer_failure();
            }
        }
        for (auto* listener : config.listeners)
        {
            if (!listener)
                continue;
            try
            {
                listener->on_event(event);
            }
            catch (...)
            {
                report_observer_failure();
            }
        }
    }

} // namespace

void run_config::notify(const run_event& event) const
{
    if (!has_observers())
        return;
    const bool add_run_id = event.run_id.empty() && !run_id.empty();
    const bool add_tags = !tags.empty() && !event.attributes.contains("tags");
    const bool add_metadata = !metadata.empty() && !event.attributes.contains("metadata");
    const bool add_trace = !event.trace_parent && trace_parent.has_value();
    const bool add_parent = event.parent_operation_id.empty() && !parent_operation_id.empty();
    if (!add_run_id && !add_tags && !add_metadata && !add_trace && !add_parent)
    {
        deliver_event(*this, event);
        return;
    }
    std::optional<run_event> observed;
    try
    {
        observed.emplace(event);
        if (add_run_id)
            observed->run_id = run_id;
        if ((add_tags || add_metadata) && !observed->attributes.is_object())
            observed->attributes = json::object();
        if (add_tags)
            observed->attributes["tags"] = tags;
        if (add_metadata)
            observed->attributes["metadata"] = metadata;
        if (add_trace)
            observed->trace_parent = trace_parent;
        if (add_parent)
            observed->parent_operation_id = parent_operation_id;
    }
    catch (...)
    {
        deliver_event(*this, event);
        return;
    }
    deliver_event(*this, *observed);
}

auto run_config::has_observers() const noexcept -> bool
{
    return static_cast<bool>(callback) || std::ranges::any_of(listeners, [](const run_listener* listener)
                                              {
                                                  return listener != nullptr;
                                              });
}

run_scope::run_scope(const run_config& config, run_event_type start_type,
    run_event_type success_type, run_event_type error_type, std::string_view name,
    std::string_view detail, json attributes) noexcept
    : source_config_(&config), success_type_(success_type), error_type_(error_type)
{
    if (!config.has_observers())
        return;
    try
    {
        name_ = name;
        operation_id_ = next_operation_id(config.run_id);
        config.notify({.type = start_type,
            .run_id = config.run_id,
            .name = name_,
            .detail = std::string{detail},
            .attributes = std::move(attributes),
            .operation_id = operation_id_});
        config_ = &config;
    }
    catch (...)
    {
        operation_id_.clear();
        name_.clear();
    }
}

run_scope::~run_scope()
{
    if (config_)
        finish(error_type_, "operation exited without a completion event", 0,
            {});
}

run_scope::run_scope(run_scope&& other) noexcept
    : config_(std::exchange(other.config_, nullptr)),
      source_config_(std::exchange(other.source_config_, nullptr)),
      success_type_(other.success_type_),
      error_type_(other.error_type_),
      name_(std::move(other.name_)),
      operation_id_(std::move(other.operation_id_))
{
}

auto run_scope::operator=(run_scope&& other) noexcept -> run_scope&
{
    if (this == &other)
        return *this;
    if (config_)
        finish(error_type_, "operation observation replaced before completion",
            0, {});
    config_ = std::exchange(other.config_, nullptr);
    source_config_ = std::exchange(other.source_config_, nullptr);
    success_type_ = other.success_type_;
    error_type_ = other.error_type_;
    name_ = std::move(other.name_);
    operation_id_ = std::move(other.operation_id_);
    return *this;
}

void run_scope::succeed(std::string_view detail, std::size_t attempt,
    json attributes)
{
    finish(success_type_, std::move(detail), attempt, std::move(attributes));
}

void run_scope::fail(std::string_view detail, std::size_t attempt,
    json attributes)
{
    finish(error_type_, std::move(detail), attempt, std::move(attributes));
}

auto run_scope::operation_id() const noexcept -> std::string_view
{
    return operation_id_;
}

auto run_scope::child_config() const -> run_config
{
    if (!source_config_)
        return {};
    auto child = *source_config_;
    if (config_)
        child.parent_operation_id = operation_id_;
    return child;
}

void run_scope::finish(run_event_type type, std::string_view detail,
    std::size_t attempt, json attributes) noexcept
{
    if (!config_)
        return;
    const auto* config = std::exchange(config_, nullptr);
    try
    {
        config->notify({.type = type,
            .run_id = config->run_id,
            .name = name_,
            .detail = std::string{detail},
            .attempt = attempt,
            .attributes = std::move(attributes),
            .operation_id = operation_id_});
    }
    catch (...)
    {
    }
}

functional_run_listener::functional_run_listener(run_callback callback)
    : callback_(std::move(callback))
{
    if (!callback_)
        throw std::invalid_argument("run listener callback cannot be empty");
}

void functional_run_listener::on_event(const run_event& event)
{
    callback_(event);
}

namespace {
    void emit(const run_config& config, run_event_type type, std::string_view name,
        std::string_view detail = {}, std::size_t attempt = 0,
        bool streaming = false) noexcept
    {
        config.notify_lazy([&]
            {
                return run_event{.type = type,
                    .run_id = config.run_id,
                    .name = std::string{name},
                    .detail = std::string{detail},
                    .attempt = attempt,
                    .attributes = streaming ? json{{"stream", true}} : json{}};
            });
    }
} // namespace

auto chat_model::stream(chat_request request, stream_handler handler,
    const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    auto result = co_await invoke(std::move(request), config);
    if (!result)
        co_return std::unexpected(result.error());
    if (handler)
    {
        chat_chunk chunk{
            .id = result->id,
            .model = result->model,
            .delta_role = "assistant",
            .delta_content = std::string(result->content()),
            .finish_reason = result->choices.empty()
                ? std::string{}
                : result->choices.front().finish_reason,
            .token_usage = result->token_usage};
        (void)co_await handler(chunk);
    }
    co_return result;
}

openai_chat_model::openai_chat_model(client& api) noexcept : api_(api) {}

auto openai_chat_model::invoke(chat_request request, const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("model invocation cancelled");
    run_scope model_run{config, run_event_type::model_start,
        run_event_type::model_end, run_event_type::model_error, request.model};
    auto result = co_await api_.chat(std::move(request));
    if (result)
        model_run.succeed_lazy([&]
            {
                return json{
                    {"input_tokens", result->token_usage.prompt_tokens},
                    {"output_tokens", result->token_usage.completion_tokens},
                    {"response_model", result->model},
                    {"total_tokens", result->token_usage.total_tokens}};
            });
    else
        model_run.fail(result.error());
    co_return result;
}

auto openai_chat_model::stream(chat_request request, stream_handler handler,
    const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("model stream cancelled");
    const auto requested_model = request.model;
    request.extra_body["stream_options"]["include_usage"] = true;
    auto model_run = run_scope::start_lazy(config, run_event_type::model_start,
        run_event_type::model_end, run_event_type::model_error,
        requested_model, []
        {
            return json{{"stream", true}};
        });

    chat_response aggregate;
    message output{.role = "assistant"};
    std::string finish_reason;
    auto streamed = co_await api_.chat_stream_async(std::move(request),
        [&](const chat_chunk& chunk) -> task<bool>
        {
            if (config.is_cancelled())
                co_return false;
            if (!chunk.id.empty())
                aggregate.id = chunk.id;
            if (!chunk.model.empty())
                aggregate.model = chunk.model;
            output.content += chunk.delta_content;
            if (!chunk.finish_reason.empty())
                finish_reason = chunk.finish_reason;
            if (chunk.token_usage)
                aggregate.token_usage = *chunk.token_usage;
            for (const auto& delta : chunk.delta_tool_calls)
            {
                if (output.tool_calls.size() <= delta.index)
                    output.tool_calls.resize(delta.index + 1);
                auto& target = output.tool_calls[delta.index];
                if (!delta.value.id.empty())
                    target.id = delta.value.id;
                if (!delta.value.type.empty())
                    target.type = delta.value.type;
                target.function.name += delta.value.function.name;
                target.function.arguments += delta.value.function.arguments;
            }
            co_return !handler || co_await handler(chunk);
        });
    if (!streamed)
    {
        model_run.fail_lazy([]
            {
                return json{{"stream", true}};
            },
            streamed.error());
        co_return std::unexpected(streamed.error());
    }
    if (config.is_cancelled())
    {
        model_run.fail_lazy([]
            {
                return json{{"stream", true}, {"cancelled", true}};
            },
            "model stream cancelled");
        co_return std::unexpected("model stream cancelled");
    }
    if (aggregate.model.empty())
        aggregate.model = requested_model;
    aggregate.choices.push_back({.index = 0,
        .msg = std::move(output),
        .finish_reason = std::move(finish_reason)});
    model_run.succeed_lazy([&]
        {
            return json{
                {"input_tokens", aggregate.token_usage.prompt_tokens},
                {"output_tokens", aggregate.token_usage.completion_tokens},
                {"response_model", aggregate.model},
                {"stream", true},
                {"total_tokens", aggregate.token_usage.total_tokens}};
        });
    co_return aggregate;
}

functional_chat_model_router::functional_chat_model_router(
    model_routing_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("model routing handler cannot be empty");
}

auto functional_chat_model_router::select(const chat_request& request,
    const run_config& config)
    -> task<std::expected<chat_model*, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("model routing cancelled");
    co_return co_await handler_(request, config);
}

routed_chat_model::routed_chat_model(chat_model_router& router,
    chat_model* fallback) noexcept
    : router_(router), fallback_(fallback)
{
}

auto routed_chat_model::route(const chat_request& request,
    const run_config& config)
    -> task<std::expected<chat_model*, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("model routing cancelled");
    auto selected = co_await router_.select(request, config);
    if (selected && *selected)
        co_return *selected;
    if (fallback_)
        co_return fallback_;
    if (!selected)
        co_return std::unexpected("model routing failed: " + selected.error());
    co_return std::unexpected("model router selected no model");
}

auto routed_chat_model::invoke(chat_request request,
    const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    auto selected = co_await route(request, config);
    if (!selected)
        co_return std::unexpected(selected.error());
    co_return co_await (*selected)->invoke(std::move(request), config);
}

auto routed_chat_model::stream(chat_request request, stream_handler handler,
    const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    auto selected = co_await route(request, config);
    if (!selected)
        co_return std::unexpected(selected.error());
    co_return co_await (*selected)->stream(
        std::move(request), std::move(handler), config);
}

resilient_chat_model::resilient_chat_model(io_context& context,
    chat_model& primary, std::vector<chat_model*> fallbacks,
    resilient_model_options options)
    : context_(context), primary_(primary), fallbacks_(std::move(fallbacks)), options_(options)
{
}

auto resilient_chat_model::invoke(chat_request request,
    const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    std::vector<chat_model*> models{&primary_};
    models.insert(models.end(), fallbacks_.begin(), fallbacks_.end());
    std::string errors;
    for (std::size_t model_index = 0; model_index < models.size(); ++model_index)
    {
        auto delay = options_.initial_backoff;
        const auto attempts = std::max<std::size_t>(1, options_.max_attempts_per_model);
        for (std::size_t attempt = 1; attempt <= attempts; ++attempt)
        {
            if (config.is_cancelled())
                co_return std::unexpected("model invocation cancelled");
            auto result = co_await models[model_index]->invoke(request, config);
            if (result)
                co_return result;
            if (!errors.empty())
                errors += "; ";
            errors += std::format("provider {} attempt {}: {}", model_index,
                attempt, result.error());
            if (attempt < attempts)
            {
                emit(config, run_event_type::model_retry, request.model,
                    result.error(), attempt);
                if (config.is_cancelled())
                    co_return std::unexpected("model invocation cancelled");
                co_await async_sleep(context_, delay);
                delay = std::min(options_.max_backoff, delay * 2);
            }
        }
    }
    co_return std::unexpected(std::move(errors));
}

auto resilient_chat_model::stream(chat_request request,
    stream_handler handler, const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    std::vector<chat_model*> models{&primary_};
    models.insert(models.end(), fallbacks_.begin(), fallbacks_.end());
    std::string errors;
    for (std::size_t model_index = 0; model_index < models.size(); ++model_index)
    {
        auto delay = options_.initial_backoff;
        const auto attempts =
            std::max<std::size_t>(1, options_.max_attempts_per_model);
        for (std::size_t attempt = 1; attempt <= attempts; ++attempt)
        {
            if (config.is_cancelled())
                co_return std::unexpected("model stream cancelled");
            bool emitted = false;
            auto result = co_await models[model_index]->stream(request, [&](const chat_chunk& chunk) -> task<bool>
                {
                    emitted = true;
                    co_return !handler || co_await handler(chunk);
                },
                config);
            if (result)
                co_return result;
            if (!errors.empty())
                errors += "; ";
            errors += std::format("provider {} attempt {}: {}", model_index,
                attempt, result.error());
            // Retrying after user-visible output would duplicate or splice the
            // stream. Surface the provider failure instead.
            if (emitted)
                co_return std::unexpected(std::move(errors));
            if (attempt < attempts)
            {
                emit(config, run_event_type::model_retry, request.model,
                    result.error(), attempt, true);
                if (config.is_cancelled())
                    co_return std::unexpected("model stream cancelled");
                co_await async_sleep(context_, delay);
                delay = std::min(options_.max_backoff, delay * 2);
            }
        }
    }
    co_return std::unexpected(std::move(errors));
}

governed_chat_model::governed_chat_model(chat_model& delegate,
    governed_model_options options)
    : delegate_(delegate),
      permits_(std::max<std::size_t>(1, options.max_concurrency)),
      breaker_(options.circuit_breaker),
      request_rate_(options.request_rate),
      rate_limit_enabled_(options.request_rate.tokens_per_second > 0.0 &&
          options.request_rate.burst > 0.0)
{
}

auto governed_chat_model::invoke(chat_request request,
    const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("model invocation cancelled");

    if (rate_limit_enabled_ && !request_rate_.try_consume())
    {
        constexpr auto message = "model request rate limit exceeded";
        emit(config, run_event_type::model_rejected, request.model, message);
        co_return std::unexpected(message);
    }

    co_await permits_.acquire();

    struct permit_guard
    {
        async_semaphore& semaphore;

        ~permit_guard()
        {
            semaphore.release();
        }
    } guard{permits_};

    if (config.is_cancelled())
        co_return std::unexpected("model invocation cancelled");

    auto result = co_await breaker_.execute<chat_response, std::string>(
        [&]() -> task<std::expected<chat_response, std::string>>
        {
            co_return co_await delegate_.invoke(std::move(request), config);
        });
    if (!result && result.error().empty())
    {
        constexpr auto message = "model circuit breaker is open";
        emit(config, run_event_type::model_rejected, request.model, message);
        co_return std::unexpected(message);
    }
    co_return result;
}

auto governed_chat_model::stream(chat_request request,
    stream_handler handler, const run_config& config)
    -> task<std::expected<chat_response, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("model stream cancelled");

    if (rate_limit_enabled_ && !request_rate_.try_consume())
    {
        constexpr auto message = "model request rate limit exceeded";
        emit(config, run_event_type::model_rejected, request.model, message,
            0, true);
        co_return std::unexpected(message);
    }

    co_await permits_.acquire();

    struct permit_guard
    {
        async_semaphore& semaphore;

        ~permit_guard()
        {
            semaphore.release();
        }
    } guard{permits_};

    if (config.is_cancelled())
        co_return std::unexpected("model stream cancelled");
    auto result = co_await breaker_.execute<chat_response, std::string>(
        [&]() -> task<std::expected<chat_response, std::string>>
        {
            co_return co_await delegate_.stream(
                std::move(request), std::move(handler), config);
        });
    if (!result && result.error().empty())
    {
        constexpr auto message = "model circuit breaker is open";
        emit(config, run_event_type::model_rejected, {}, message, 0,
            true);
        co_return std::unexpected(message);
    }
    co_return result;
}

auto governed_chat_model::circuit_state() const noexcept
    -> circuit_breaker_state
{
    return breaker_.state();
}

void governed_chat_model::reset_circuit() noexcept
{
    breaker_.reset();
}

openai_embedding_model::openai_embedding_model(client& api, std::string model,
    std::optional<int> dimensions)
    : api_(api), model_(std::move(model)), dimensions_(dimensions)
{
}

auto openai_embedding_model::embed_documents(std::vector<std::string> texts)
    -> task<std::expected<std::vector<std::vector<float>>, std::string>>
{
    embedding_request request{.model = model_, .input = std::move(texts), .dimensions = dimensions_};
    auto result = co_await api_.embeddings(std::move(request));
    if (!result)
        co_return std::unexpected(result.error());
    std::ranges::sort(result->data, {}, &embedding_data::index);
    std::vector<std::vector<float>> values;
    values.reserve(result->data.size());
    for (auto& item : result->data)
        values.push_back(std::move(item.embedding));
    co_return values;
}

auto openai_embedding_model::embed_query(std::string text)
    -> task<std::expected<std::vector<float>, std::string>>
{
    auto result = co_await embed_documents({std::move(text)});
    if (!result)
        co_return std::unexpected(result.error());
    if (result->empty())
        co_return std::unexpected("embedding provider returned no vector");
    co_return std::move(result->front());
}

openai_image_model::openai_image_model(client& api) noexcept : api_(api) {}

auto openai_image_model::generate(image_generation_request request,
    const run_config& config)
    -> task<std::expected<image_response, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("image generation cancelled");
    auto model_run = run_scope::start_lazy(config, run_event_type::model_start, run_event_type::model_end, run_event_type::model_error, request.model, []
        {
            return json{{"operation", "image_generation"}};
        },
        "image generation");
    auto result = co_await api_.create_image(std::move(request));
    if (result)
        model_run.succeed_lazy([]
            {
                return json{{"operation", "image_generation"}};
            },
            "image generation");
    else
        model_run.fail_lazy([]
            {
                return json{{"operation", "image_generation"}};
            },
            result.error());
    co_return result;
}

openai_moderation_model::openai_moderation_model(client& api) noexcept
    : api_(api)
{
}

auto openai_moderation_model::moderate(moderation_request request,
    const run_config& config)
    -> task<std::expected<moderation_response, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("content moderation cancelled");
    auto model_run = run_scope::start_lazy(config, run_event_type::model_start, run_event_type::model_end, run_event_type::model_error, request.model, []
        {
            return json{{"operation", "moderation"}};
        },
        "content moderation");
    auto result = co_await api_.moderate(std::move(request));
    if (result)
        model_run.succeed_lazy([&]
            {
                return json{{"operation", "moderation"}, {"response_model", result->model}};
            },
            "content moderation");
    else
        model_run.fail_lazy([]
            {
                return json{{"operation", "moderation"}};
            },
            result.error());
    co_return result;
}

} // namespace cnetmod::openai
