module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.retry;

import std;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc::governance {

export struct retry_policy {
  std::uint32_t max_attempts = 3;
  std::chrono::milliseconds initial_backoff{100};
  std::chrono::milliseconds max_backoff{10'000};
  double backoff_multiplier = 2.0;
  double jitter = 0.2;
  std::vector<status_code> retryable_status_codes{status_code::unavailable};

  [[nodiscard]] auto
  allows_retry(status_code status,
               std::uint32_t completed_attempts) const noexcept -> bool;
  [[nodiscard]] auto backoff_for(std::uint32_t retry_number) const
      -> std::chrono::milliseconds;
};

export struct retry_budget_config {
  std::uint32_t max_tokens = 10;
  std::uint32_t token_ratio = 1;
  std::uint32_t retry_threshold = 0;
};

export class retry_budget {
public:
  explicit retry_budget(retry_budget_config config = {}) noexcept;

  retry_budget(const retry_budget &) = delete;
  auto operator=(const retry_budget &) -> retry_budget & = delete;

  [[nodiscard]] auto try_acquire() noexcept -> bool;
  void record_success() noexcept;
  void reset() noexcept;
  [[nodiscard]] auto available_tokens() const noexcept -> std::uint32_t;
  [[nodiscard]] auto allows_retry() const noexcept -> bool;

private:
  retry_budget_config config_;
  mutable std::mutex mutex_;
  std::uint32_t tokens_ = 0;
};

} // namespace cnetmod::grpc::governance
