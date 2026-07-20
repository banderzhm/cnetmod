module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.admission;

import std;

namespace cnetmod::grpc::governance {

export class concurrency_limiter {
public:
  class [[nodiscard]] guard {
  public:
    guard() noexcept = default;
    ~guard();

    guard(const guard &) = delete;
    auto operator=(const guard &) -> guard & = delete;
    guard(guard &&other) noexcept;
    auto operator=(guard &&other) noexcept -> guard &;

    [[nodiscard]] explicit operator bool() const noexcept;
    void reset() noexcept;

  private:
    friend class concurrency_limiter;

    explicit guard(concurrency_limiter *limiter) noexcept;

    concurrency_limiter *limiter_ = nullptr;
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

export struct rate_limit {
  double tokens_per_second = 0.0;
  double burst = 0.0;
};

export class token_bucket {
public:
  explicit token_bucket(rate_limit limit) noexcept;

  [[nodiscard]] auto try_consume(double tokens = 1.0) noexcept -> bool;
  [[nodiscard]] auto limit() const noexcept -> rate_limit;

private:
  mutable std::mutex mutex_;
  rate_limit limit_;
  double tokens_ = 0.0;
  std::chrono::steady_clock::time_point last_refill_;
};

/// Stores independent token buckets for exact service/method pairs.
export class rate_limit_registry {
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

  mutable std::mutex mutex_;
  std::map<std::string, method_limits, std::less<>> limits_;
};

} // namespace cnetmod::grpc::governance
