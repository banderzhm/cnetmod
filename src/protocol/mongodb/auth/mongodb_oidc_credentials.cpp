module cnetmod.protocol.mongodb;
import std;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import :error;
import :oidc_credentials;

namespace cnetmod::mongodb {
oidc_token_cache::oidc_token_cache(io_context& io, oidc_token_callback callback, oidc_callback_parameters parameters)
    : io_(io), callback_(std::move(callback)), parameters_(std::move(parameters)) {}

auto oidc_token_cache::get(std::chrono::milliseconds timeout) -> task<result<cached_oidc_token>>
{
    const auto budget = std::min({timeout, parameters_.timeout, std::chrono::milliseconds{60000}});
    if (budget.count() <= 0)
        co_return std::unexpected(make_error(error_code::operation_timed_out, "OIDC callback deadline expired"));
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (fetching_)
    {
        if (std::chrono::steady_clock::now() >= deadline)
            co_return std::unexpected(make_error(error_code::operation_timed_out, "OIDC cache wait exceeded deadline"));
        auto waiter = std::make_shared<fetch_waiter>();
        waiting_.push_back(waiter);
        (void)co_await async_timer_wait(io_, std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()), waiter->wake);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
        co_return std::unexpected(make_error(error_code::operation_timed_out, "OIDC cache wait exceeded deadline"));
    if (cached_ && (!expires_ || *expires_ > now))
        co_return cached_oidc_token{*cached_, true};
    fetching_ = true;

    struct fetch_guard
    {
        oidc_token_cache& owner;

        ~fetch_guard()
        {
            owner.release_fetch();
        }
    } guard{*this};

    if (!callback_ || parameters_.timeout.count() <= 0)
        co_return std::unexpected(make_error(error_code::authentication_failed, "missing OIDC callback or invalid timeout"));
    const auto not_before = last_callback_ + std::chrono::milliseconds{100};
    if (now < not_before)
    {
        if (not_before >= deadline)
            co_return std::unexpected(make_error(error_code::operation_timed_out, "OIDC callback throttle exceeds deadline"));
        auto waited = co_await async_timer_wait(io_, std::chrono::ceil<std::chrono::milliseconds>(not_before - now));
        if (!waited)
            co_return std::unexpected(make_error(error_code::operation_cancelled, "OIDC callback wait was cancelled"));
    }
    last_callback_ = std::chrono::steady_clock::now();
    auto parameters = parameters_;
    parameters.timeout = std::chrono::ceil<std::chrono::milliseconds>(deadline - last_callback_);
    auto token = co_await callback_(parameters);
    if (!token)
        co_return std::unexpected(token.error());
    if (std::chrono::steady_clock::now() > deadline)
        co_return std::unexpected(make_error(error_code::operation_timed_out, "OIDC callback exceeded its deadline"));
    if (token->access_token.empty() || token->access_token.size() > 1024U * 1024U ||
        token->access_token.find('\0') != std::string::npos ||
        (token->expires_in && token->expires_in->count() < 0))
        co_return std::unexpected(make_error(error_code::authentication_failed, "invalid OIDC token or expiry"));
    cached_ = std::move(token->access_token);
    expires_.reset();
    if (token->expires_in)
        expires_ = std::chrono::steady_clock::now() + std::min(*token->expires_in, std::chrono::seconds{86400});
    co_return cached_oidc_token{*cached_, false};
}

void oidc_token_cache::release_fetch() noexcept
{
    fetching_ = false;
    // Swap first: waking a waiter can immediately start another fetch.
    auto waiters = std::exchange(waiting_, {});
    for (const auto& weak : waiters)
        if (const auto waiter = weak.lock())
            waiter->wake.cancel();
}

auto oidc_token_cache::peek() -> task<std::optional<std::string>>
{
    if (cached_ && (!expires_ || *expires_ > std::chrono::steady_clock::now()))
        co_return cached_;
    co_return std::nullopt;
}

auto oidc_token_cache::invalidate(std::string_view token) -> task<void>
{
    if (cached_ && *cached_ == token)
    {
        cached_.reset();
        expires_.reset();
    }
    co_return;
}
} // namespace cnetmod::mongodb
