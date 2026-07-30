#pragma once

namespace postgresql_example {

struct service_health_snapshot
{
    bool live{};
    bool ready{};
    std::size_t active_endpoint{};
    std::size_t in_flight{};
    std::uint64_t succeeded{};
    std::uint64_t failed{};
};

class service_health
{
public:
    void set_live(bool value) noexcept
    {
        live_.store(value, std::memory_order_release);
    }

    void set_ready(bool value) noexcept
    {
        ready_.store(value, std::memory_order_release);
    }

    void set_active_endpoint(std::size_t value) noexcept
    {
        active_endpoint_.store(value, std::memory_order_release);
    }

    void begin_request() noexcept
    {
        in_flight_.fetch_add(1, std::memory_order_relaxed);
    }

    void complete_request(bool succeeded) noexcept
    {
        in_flight_.fetch_sub(1, std::memory_order_relaxed);
        (succeeded ? succeeded_ : failed_).fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] auto snapshot() const noexcept -> service_health_snapshot
    {
        return {
            live_.load(std::memory_order_acquire),
            ready_.load(std::memory_order_acquire),
            active_endpoint_.load(std::memory_order_acquire),
            in_flight_.load(std::memory_order_relaxed),
            succeeded_.load(std::memory_order_relaxed),
            failed_.load(std::memory_order_relaxed)};
    }

private:
    std::atomic_bool live_{true};
    std::atomic_bool ready_{false};
    std::atomic_size_t active_endpoint_{0};
    std::atomic_size_t in_flight_{0};
    std::atomic_uint64_t succeeded_{0};
    std::atomic_uint64_t failed_{0};
};

class request_health_guard
{
public:
    explicit request_health_guard(service_health& health) noexcept : health_(health)
    {
        health_.begin_request();
    }

    request_health_guard(const request_health_guard&) = delete;
    auto operator=(const request_health_guard&) -> request_health_guard& = delete;

    ~request_health_guard()
    {
        health_.complete_request(succeeded_);
    }

    void mark_succeeded() noexcept
    {
        succeeded_ = true;
    }

private:
    service_health& health_;
    bool succeeded_{};
};

} // namespace postgresql_example
