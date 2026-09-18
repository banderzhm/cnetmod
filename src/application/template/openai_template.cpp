module cnetmod.application.openai_template;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
namespace cnetmod::application {

openai_template::openai_template(openai::chat_model& model,
    openai_template_options options, openai::run_listener* listener,
    std::shared_ptr<async_mutex> request_gate)
    : model_(model), options_(std::move(options)), listener_(listener), request_gate_(request_gate ? std::move(request_gate) : std::make_shared<async_mutex>())
{
}

auto openai_template::invoke(openai::chat_request request,
    openai::run_config configuration)
    -> task<std::expected<openai::chat_response, std::string>>
{
    configuration = observe(std::move(configuration));
    if (configuration.is_cancelled())
        co_return std::unexpected("model invocation cancelled");
    co_await request_gate_->lock();
    async_lock_guard lock{*request_gate_, std::adopt_lock};
    if (configuration.is_cancelled())
        co_return std::unexpected("model invocation cancelled");
    co_return co_await model_.invoke(std::move(request), configuration);
}

auto openai_template::invoke(std::string input,
    openai::run_config configuration)
    -> task<std::expected<openai::chat_response, std::string>>
{
    co_return co_await invoke(
        make_request(std::move(input)), std::move(configuration));
}

auto openai_template::stream(openai::chat_request request,
    openai::chat_model::stream_handler handler,
    openai::run_config configuration)
    -> task<std::expected<openai::chat_response, std::string>>
{
    configuration = observe(std::move(configuration));
    if (configuration.is_cancelled())
        co_return std::unexpected("model stream cancelled");
    co_await request_gate_->lock();
    async_lock_guard lock{*request_gate_, std::adopt_lock};
    if (configuration.is_cancelled())
        co_return std::unexpected("model stream cancelled");
    co_return co_await model_.stream(
        std::move(request), std::move(handler), configuration);
}

auto openai_template::stream(std::string input,
    openai::chat_model::stream_handler handler,
    openai::run_config configuration)
    -> task<std::expected<openai::chat_response, std::string>>
{
    co_return co_await stream(make_request(std::move(input)),
        std::move(handler), std::move(configuration));
}

auto openai_template::make_request(std::string input) const
    -> openai::chat_request
{
    auto request = options_.request;
    if (!options_.system_prompt.empty())
        request.messages.insert(request.messages.begin(),
            openai::message::system(options_.system_prompt));
    request.messages.push_back(openai::message::user(input));
    return request;
}

auto openai_template::observe(openai::run_config configuration) const
    -> openai::run_config
{
    if (listener_ && std::ranges::find(configuration.listeners, listener_) == configuration.listeners.end())
        configuration.listeners.push_back(listener_);
    return configuration;
}

} // namespace cnetmod::application
#endif
