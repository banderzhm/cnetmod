module cnetmod.application.chat_model_pool;

#ifdef CNETMOD_HAS_CHAT_MODEL
import std;
import cnetmod.coro.channel;
import cnetmod.coro.mutex;
import cnetmod.executor.async_op;

namespace cnetmod::application {

class chat_model_pool_state
{
public:
    explicit chat_model_pool_state(
        std::vector<std::shared_ptr<ai::chat_model>> values)
        : idle(values.size())
    {
        for (auto& value : values)
            (void)idle.try_send(std::move(value));
    }

    ~chat_model_pool_state()
    {
        idle.close();
    }

    channel<std::shared_ptr<ai::chat_model>> idle;
};

namespace {

    [[nodiscard]] auto make_pool_state(
        std::vector<std::shared_ptr<ai::chat_model>> models)
        -> std::shared_ptr<chat_model_pool_state>
    {
        if (models.empty() || std::ranges::any_of(models, [](const auto& model)
                                  {
                                      return !model;
                                  }))
            throw std::invalid_argument(
                "chat model pool requires non-null models");
        return std::make_shared<chat_model_pool_state>(std::move(models));
    }

} // namespace

pooled_chat_model::pooled_chat_model(
    std::shared_ptr<chat_model_pool_state> state,
    std::shared_ptr<ai::chat_model> model) noexcept
    : state_(std::move(state)), model_(std::move(model))
{
}

pooled_chat_model::pooled_chat_model(pooled_chat_model&& other) noexcept
    : state_(std::move(other.state_)), model_(std::move(other.model_))
{
}

auto pooled_chat_model::operator=(pooled_chat_model&& other) noexcept
    -> pooled_chat_model&
{
    if (this != &other)
    {
        release();
        state_ = std::move(other.state_);
        model_ = std::move(other.model_);
    }
    return *this;
}

pooled_chat_model::~pooled_chat_model()
{
    release();
}

auto pooled_chat_model::get() const noexcept -> ai::chat_model&
{
    return *model_;
}

pooled_chat_model::operator bool() const noexcept
{
    return static_cast<bool>(model_);
}

void pooled_chat_model::release() noexcept
{
    if (!state_ || !model_)
        return;
    (void)state_->idle.try_send(std::move(model_));
    state_.reset();
}

chat_model_pool::chat_model_pool(io_context& io)
    : io_(io), state_(std::make_shared<chat_model_pool_state>(std::vector<std::shared_ptr<ai::chat_model>>{}))
{
    state_->idle.close();
}

chat_model_pool::chat_model_pool(io_context& io,
    std::vector<std::shared_ptr<ai::chat_model>> models)
    : io_(io), state_(make_pool_state(std::move(models)))
{
}

chat_model_pool::~chat_model_pool()
{
    state_->idle.close();
}

auto chat_model_pool::acquire(cancel_token* cancellation)
    -> task<std::expected<pooled_chat_model, std::error_code>>
{
    if (cancellation && cancellation->is_cancelled())
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));
    std::shared_ptr<chat_model_pool_state> state;
    co_await state_gate_.lock();
    {
        async_lock_guard guard{state_gate_, std::adopt_lock};
        state = state_;
    }
    std::optional<std::shared_ptr<ai::chat_model>> model;
    if (cancellation)
    {
        while (!model)
        {
            if (cancellation->is_cancelled())
                co_return std::unexpected(
                    std::make_error_code(std::errc::operation_canceled));
            model = state->idle.try_receive();
            if (model || state->idle.is_closed())
                break;
            auto waited = co_await async_timer_wait(
                io_, std::chrono::milliseconds{5}, *cancellation);
            if (!waited)
            {
                if (cancellation->is_cancelled())
                    co_return std::unexpected(
                        std::make_error_code(std::errc::operation_canceled));
                co_return std::unexpected(waited.error());
            }
        }
    }
    else
    {
        model = co_await state->idle.receive();
    }
    if (!model)
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));
    if (cancellation && cancellation->is_cancelled())
    {
        pooled_chat_model returned{state, std::move(*model)};
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_canceled));
    }
    co_return pooled_chat_model{std::move(state), std::move(*model)};
}

auto chat_model_pool::reset(
    std::vector<std::shared_ptr<ai::chat_model>> models)
    -> task<std::expected<void, std::error_code>>
{
    if (models.empty() || std::ranges::any_of(models, [](const auto& model)
                              {
                                  return !model;
                              }))
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    try
    {
        auto next = std::make_shared<chat_model_pool_state>(std::move(models));
        std::shared_ptr<chat_model_pool_state> previous;
        co_await state_gate_.lock();
        {
            async_lock_guard guard{state_gate_, std::adopt_lock};
            previous = std::exchange(state_, std::move(next));
        }
        previous->idle.close();
        co_return std::expected<void, std::error_code>{};
    }
    catch (const std::bad_alloc&)
    {
        co_return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    }
}

auto chat_model_pool::close() -> task<void>
{
    std::shared_ptr<chat_model_pool_state> state;
    co_await state_gate_.lock();
    {
        async_lock_guard guard{state_gate_, std::adopt_lock};
        state = state_;
    }
    state->idle.close();
}

} // namespace cnetmod::application
#endif
