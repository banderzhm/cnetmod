module cnetmod.application.chat_model_template;

#ifdef CNETMOD_HAS_CHAT_MODEL
namespace cnetmod::application {

chat_model_template::chat_model_template(chat_model_pool& models,
    chat_model_template_options options,
    std::shared_ptr<striped_async_mutex<std::string>> session_gates)
    : models_(models), event_loop_(models.event_loop()),
      options_(std::move(options)), session_gates_(session_gates ? std::move(session_gates) : std::make_shared<striped_async_mutex<std::string>>())
{
}

auto chat_model_template::invoke(ai::chat_request request,
    const ai::run_config& borrowed)
    -> task<std::expected<ai::chat_response, std::string>>
{
    // Own the configuration before the first suspension point.
    auto configuration = ai::run_config{borrowed};
    auto* caller = io_context::current();
    if (caller != nullptr && caller != &event_loop_)
        co_return co_await resume_on(*caller, starts_on(event_loop_,
            invoke_local(std::move(request), std::move(configuration))));
    co_return co_await invoke_local(
        std::move(request), std::move(configuration));
}

auto chat_model_template::invoke_local(ai::chat_request request,
    ai::run_config configuration)
    -> task<std::expected<ai::chat_response, std::string>>
{
    if (configuration.is_cancelled())
        co_return std::unexpected("model invocation cancelled");
    auto lease = co_await models_.acquire(configuration.cancellation);
    if (!lease)
        co_return std::unexpected(lease.error().message());
    co_return co_await lease->get().invoke(
        std::move(request), configuration);
}

auto chat_model_template::invoke(std::string input,
    ai::run_config configuration)
    -> task<std::expected<ai::chat_response, std::string>>
{
    co_return co_await invoke(
        make_request(std::move(input)), std::move(configuration));
}

auto chat_model_template::stream(ai::chat_request request,
    ai::chat_model::stream_handler handler, const ai::run_config& borrowed)
    -> task<std::expected<ai::chat_response, std::string>>
{
    // Own the configuration before the first suspension point.
    auto configuration = ai::run_config{borrowed};
    auto* caller = io_context::current();
    if (caller != nullptr && caller != &event_loop_)
    {
        auto return_handler = [caller, owner = &event_loop_,
                                  handler = std::move(handler)](
                                  const ai::chat_chunk& chunk) mutable
            -> task<bool>
        {
            co_return co_await resume_on(*owner,
                starts_on(*caller, handler(chunk)));
        };
        co_return co_await resume_on(*caller, starts_on(event_loop_,
            stream_local(std::move(request), std::move(return_handler),
                std::move(configuration))));
    }
    co_return co_await stream_local(std::move(request), std::move(handler),
        std::move(configuration));
}

auto chat_model_template::stream_local(ai::chat_request request,
    ai::chat_model::stream_handler handler, ai::run_config configuration)
    -> task<std::expected<ai::chat_response, std::string>>
{
    if (configuration.is_cancelled())
        co_return std::unexpected("model stream cancelled");
    auto lease = co_await models_.acquire(configuration.cancellation);
    if (!lease)
        co_return std::unexpected(lease.error().message());
    co_return co_await lease->get().stream(
        std::move(request), std::move(handler), configuration);
}

auto chat_model_template::stream(std::string input,
    ai::chat_model::stream_handler handler, ai::run_config configuration)
    -> task<std::expected<ai::chat_response, std::string>>
{
    co_return co_await stream(make_request(std::move(input)),
        std::move(handler), std::move(configuration));
}

auto chat_model_template::conversation(std::string session_id,
    ai::conversation_store& store, chat_conversation_options options)
    -> chat_conversation
{
    return chat_conversation{*this, std::move(session_id), store, options};
}

auto chat_model_template::make_request(std::string input,
    std::vector<ai::message> history) const -> ai::chat_request
{
    auto request = options_.request;
    if (!options_.system_prompt.empty())
        request.messages.insert(request.messages.begin(),
            ai::message::system(options_.system_prompt));
    request.messages.insert(request.messages.end(),
        std::make_move_iterator(history.begin()),
        std::make_move_iterator(history.end()));
    request.messages.push_back(ai::message::user(input));
    return request;
}

chat_conversation::chat_conversation(chat_model_template& owner,
    std::string session_id, ai::conversation_store& store,
    chat_conversation_options options)
    : owner_(&owner), session_id_(std::move(session_id)), store_(&store), options_(options)
{
    if (session_id_.empty())
        throw std::invalid_argument("conversation session id cannot be empty");
}

auto chat_conversation::invoke(std::string input,
    ai::run_config configuration)
    -> task<std::expected<ai::chat_response, std::string>>
{
    auto session_lock = co_await owner_->session_gates_->lock(session_id_);
    auto history = co_await store_->load_recent(
        session_id_, options_.history_limit);
    if (!history)
        co_return std::unexpected(history.error());
    auto request = owner_->make_request(input, std::move(*history));
    auto response = co_await owner_->invoke(
        std::move(request), std::move(configuration));
    if (!response)
        co_return response;
    std::vector<ai::message> turn;
    turn.reserve(2);
    turn.push_back(ai::message::user(input));
    turn.push_back(ai::message::model_output(response->content()));
    auto stored = co_await store_->append_batch(session_id_, std::move(turn));
    if (!stored)
        co_return std::unexpected(stored.error());
    co_return response;
}

auto chat_conversation::stream(std::string input,
    ai::chat_model::stream_handler handler, ai::run_config configuration)
    -> task<std::expected<ai::chat_response, std::string>>
{
    auto session_lock = co_await owner_->session_gates_->lock(session_id_);
    auto history = co_await store_->load_recent(
        session_id_, options_.history_limit);
    if (!history)
        co_return std::unexpected(history.error());
    auto request = owner_->make_request(input, std::move(*history));
    auto response = co_await owner_->stream(std::move(request),
        std::move(handler), std::move(configuration));
    if (!response)
        co_return response;
    std::vector<ai::message> turn;
    turn.reserve(2);
    turn.push_back(ai::message::user(input));
    turn.push_back(ai::message::model_output(response->content()));
    auto stored = co_await store_->append_batch(session_id_, std::move(turn));
    if (!stored)
        co_return std::unexpected(stored.error());
    co_return response;
}

auto chat_conversation::clear()
    -> task<std::expected<void, std::string>>
{
    auto session_lock = co_await owner_->session_gates_->lock(session_id_);
    co_return co_await store_->erase(session_id_);
}

auto chat_conversation::session_id() const noexcept -> std::string_view
{
    return session_id_;
}

} // namespace cnetmod::application
#endif
