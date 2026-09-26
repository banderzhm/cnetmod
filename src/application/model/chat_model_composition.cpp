module;

#include <cnetmod/config.hpp>

module cnetmod.application.chat_model_composition;

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

auto composed_chat_model::create(service_registry& services,
    execution_context& executor, chat_model_composition composition)
    -> std::expected<std::shared_ptr<composed_chat_model>, std::error_code>
{
    if (composition.instances.empty())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (composition.instances.size() > 1 && !composition.routing)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    if (composition.fallback_to_remaining_instances && !composition.resilience)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    std::shared_ptr<composed_chat_model> model{new composed_chat_model()};
    model->event_loop_ = &executor.event_loop();
    model->routing_ = composition.routing.value_or(chat_model_routing::ordered);
    model->instances_ = composition.instances;

    for (const auto& instance : composition.instances)
    {
        if (std::ranges::count(composition.instances, instance) != 1)
            return std::unexpected(std::make_error_code(std::errc::file_exists));
        auto* service = services.find<chat_model_service>(instance);
        if (service == nullptr)
            return std::unexpected(
                std::make_error_code(std::errc::no_such_file_or_directory));
        model->templates_.push_back(std::make_unique<chat_model_template>(
            service->make_template(composition.template_options)));
    }

    const auto count = model->templates_.size();
    if (composition.resilience)
    {
        const auto candidate_count = model->routing_ == chat_model_routing::ordered
            ? std::size_t{1}
            : count;
        for (std::size_t start = 0; start < candidate_count; ++start)
        {
            std::vector<ai::chat_model*> fallbacks;
            if (composition.fallback_to_remaining_instances)
            {
                fallbacks.reserve(count - 1);
                for (std::size_t offset = 1; offset < count; ++offset)
                    fallbacks.push_back(
                        model->templates_[(start + offset) % count].get());
            }
            model->candidates_.push_back(std::make_unique<ai::resilient_chat_model>(
                executor.event_loop(), *model->templates_[start],
                std::move(fallbacks), *composition.resilience));
        }
    }

    ai::chat_model* routed = nullptr;
    if (count == 1 && model->candidates_.size() <= 1)
    {
        routed = model->candidates_.empty()
            ? static_cast<ai::chat_model*>(model->templates_.front().get())
            : model->candidates_.front().get();
    }
    else
    {
        auto* self = model.get();
        model->router_ = std::make_unique<ai::functional_chat_model_router>(
            [self](const ai::chat_request&, const ai::run_config&)
                -> task<std::expected<ai::chat_model*, std::string>>
            {
                co_return self->select();
            });
        model->routed_ = std::make_unique<ai::routed_chat_model>(*model->router_);
        routed = model->routed_.get();
    }

    if (composition.governance)
    {
        model->governed_ = std::make_unique<ai::governed_chat_model>(
            *routed, *composition.governance);
        model->top_ = model->governed_.get();
    }
    else
        model->top_ = routed;
    return model;
}

auto composed_chat_model::select() -> ai::chat_model*
{
    const auto count = candidates_.empty() ? templates_.size() : candidates_.size();
    const auto index = routing_ == chat_model_routing::round_robin
        ? cursor_.fetch_add(1, std::memory_order_relaxed) % count
        : std::size_t{0};
    if (!candidates_.empty())
        return candidates_[index].get();
    return templates_[index].get();
}

auto composed_chat_model::invoke(ai::chat_request request,
    const ai::run_config& config)
    -> task<std::expected<ai::chat_response, std::string>>
{
    auto owned = ai::run_config{config};
    auto* caller = io_context::current();
    if (caller != nullptr && caller != event_loop_)
        co_return co_await resume_on(*caller, starts_on(*event_loop_,
            top_->invoke(std::move(request), owned)));
    co_return co_await top_->invoke(std::move(request), owned);
}

auto composed_chat_model::stream(ai::chat_request request,
    stream_handler handler, const ai::run_config& config)
    -> task<std::expected<ai::chat_response, std::string>>
{
    auto owned = ai::run_config{config};
    auto* caller = io_context::current();
    if (caller != nullptr && caller != event_loop_)
    {
        auto return_handler = [caller, owner = event_loop_,
                                  handler = std::move(handler)](
                                  const ai::chat_chunk& chunk) mutable
            -> task<bool>
        {
            co_return co_await resume_on(*owner,
                starts_on(*caller, handler(chunk)));
        };
        co_return co_await resume_on(*caller, starts_on(*event_loop_,
            top_->stream(std::move(request), std::move(return_handler),
                owned)));
    }
    co_return co_await top_->stream(std::move(request), std::move(handler), owned);
}

auto composed_chat_model::instances() const noexcept
    -> std::span<const std::string>
{
    return instances_;
}

auto add_chat_model(component_collection& components, std::string name,
    chat_model_composition composition) -> component_collection&
{
    return components.singleton<ai::chat_model>(std::move(name),
        [composition = std::move(composition)](component_resolver& resolver)
            -> std::shared_ptr<ai::chat_model>
        {
            auto created = composed_chat_model::create(
                resolver.get<service_registry>(),
                resolver.get<application_runtime>().executor(), composition);
            if (!created)
                throw std::system_error(created.error(),
                    "chat model composition cannot resolve its instances");
            return std::move(*created);
        });
}

} // namespace cnetmod::application
#endif
