module cnetmod.protocol.grpc.governance.admission;

import std;

namespace cnetmod::grpc::governance {

concurrency_limiter::guard::guard(concurrency_limiter* limiter) noexcept
    : limiter_(limiter) {}

concurrency_limiter::guard::~guard()
{
    reset();
}

concurrency_limiter::guard::guard(guard&& other) noexcept
    : limiter_(std::exchange(other.limiter_, nullptr)) {}

auto concurrency_limiter::guard::operator=(guard&& other) noexcept -> guard&
{
    if (this != &other)
    {
        reset();
        limiter_ = std::exchange(other.limiter_, nullptr);
    }
    return *this;
}

concurrency_limiter::guard::operator bool() const noexcept
{
    return limiter_ != nullptr;
}

void concurrency_limiter::guard::reset() noexcept
{
    if (auto* limiter = std::exchange(limiter_, nullptr))
        limiter->release();
}

concurrency_limiter::concurrency_limiter(std::size_t limit) noexcept
    : limit_(limit) {}

auto concurrency_limiter::try_acquire() noexcept -> std::optional<guard>
{
    auto current = in_flight_.load(std::memory_order_relaxed);
    while (current < limit_)
    {
        if (in_flight_.compare_exchange_weak(current, current + 1,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            return guard{this};
    }
    return std::nullopt;
}

auto concurrency_limiter::limit() const noexcept -> std::size_t
{
    return limit_;
}

auto concurrency_limiter::in_flight() const noexcept -> std::size_t
{
    return in_flight_.load(std::memory_order_relaxed);
}

void concurrency_limiter::release() noexcept
{
    in_flight_.fetch_sub(1, std::memory_order_release);
}

void rate_limit_registry::set_limit(std::string service, std::string method,
    rate_limit limit)
{
    auto bucket = std::make_shared<token_bucket>(limit);
    concurrent_containers::exclusive_latch_guard lock{latch_};
    limits_[std::move(service)][std::move(method)] = std::move(bucket);
}

void rate_limit_registry::clear_limit(std::string_view service,
    std::string_view method)
{
    concurrent_containers::exclusive_latch_guard lock{latch_};
    const auto service_it = limits_.find(service);
    if (service_it == limits_.end())
        return;
    const auto method_it = service_it->second.find(method);
    if (method_it != service_it->second.end())
        service_it->second.erase(method_it);
    if (service_it->second.empty())
        limits_.erase(service_it);
}

auto rate_limit_registry::try_consume(std::string_view service,
    std::string_view method,
    double tokens) noexcept -> bool
{
    std::shared_ptr<token_bucket> bucket;
    {
        concurrent_containers::shared_latch_guard lock{latch_};
        const auto service_it = limits_.find(service);
        if (service_it == limits_.end())
            return true;
        const auto method_it = service_it->second.find(method);
        if (method_it == service_it->second.end())
            return true;
        bucket = method_it->second;
    }
    return bucket->try_consume(tokens);
}

auto rate_limit_registry::limit(std::string_view service,
    std::string_view method) const
    -> std::optional<rate_limit>
{
    concurrent_containers::shared_latch_guard lock{latch_};
    const auto service_it = limits_.find(service);
    if (service_it == limits_.end())
        return std::nullopt;
    const auto method_it = service_it->second.find(method);
    if (method_it == service_it->second.end())
        return std::nullopt;
    return method_it->second->limit();
}

} // namespace cnetmod::grpc::governance
