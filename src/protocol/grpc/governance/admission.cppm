module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.admission;

import std;
import cnetmod.coro.rate_limiter;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::grpc::governance {

export class concurrency_limiter
{
public:
    class [[nodiscard]] guard
    {
    public:
        guard() noexcept = default;
        ~guard();

        guard(const guard&) = delete;
        auto operator=(const guard&) -> guard& = delete;
        guard(guard&& other) noexcept;
        auto operator=(guard&& other) noexcept -> guard&;

        [[nodiscard]] explicit operator bool() const noexcept;
        void reset() noexcept;

    private:
        friend class concurrency_limiter;

        explicit guard(concurrency_limiter* limiter) noexcept;

        concurrency_limiter* limiter_ = nullptr;
    };

    explicit concurrency_limiter(std::size_t limit) noexcept;

    [[nodiscard]] auto try_acquire() noexcept -> std::optional<guard>;
    [[nodiscard]] auto limit() const noexcept -> std::size_t;
    [[nodiscard]] auto in_flight() const noexcept -> std::size_t;

private:
    void release() noexcept;

    std::size_t limit_;
    std::atomic<std::size_t> in_flight_{0};
};

export using rate_limit = cnetmod::rate_limit;
export using token_bucket = cnetmod::token_bucket;

/// Stores independent token buckets for exact service/method pairs.
export class rate_limit_registry
{
public:
    void set_limit(std::string service, std::string method, rate_limit limit);
    void clear_limit(std::string_view service, std::string_view method);

    [[nodiscard]] auto try_consume(std::string_view service,
        std::string_view method,
        double tokens = 1.0) noexcept -> bool;
    [[nodiscard]] auto limit(std::string_view service,
        std::string_view method) const
        -> std::optional<rate_limit>;

private:
    using method_limits =
        std::map<std::string, std::shared_ptr<token_bucket>, std::less<>>;

    mutable concurrent_containers::atomic_rw_latch latch_;
    std::map<std::string, method_limits, std::less<>> limits_;
};

} // namespace cnetmod::grpc::governance
