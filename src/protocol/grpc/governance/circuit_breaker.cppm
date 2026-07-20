module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.circuit_breaker;

import std;

namespace cnetmod::grpc::governance {

export enum class circuit_breaker_state : std::uint8_t {
  closed,
  open,
  half_open,
};

export struct circuit_breaker_config {
  std::uint32_t failure_threshold = 5;
  std::uint32_t success_threshold = 2;
  std::chrono::milliseconds open_duration{30'000};
  std::uint32_t half_open_max_requests = 1;
};

export class circuit_breaker {
public:
  explicit circuit_breaker(circuit_breaker_config config = {}) noexcept;

  circuit_breaker(const circuit_breaker &) = delete;
  auto operator=(const circuit_breaker &) -> circuit_breaker & = delete;

  [[nodiscard]] auto try_acquire() noexcept -> bool;
  void record_success() noexcept;
  void record_failure() noexcept;
  void reset() noexcept;
  [[nodiscard]] auto state() const noexcept -> circuit_breaker_state;

private:
  void
  transition_if_expired(std::chrono::steady_clock::time_point now) noexcept;
  void open(std::chrono::steady_clock::time_point now) noexcept;

  circuit_breaker_config config_;
  mutable std::mutex mutex_;
  circuit_breaker_state state_ = circuit_breaker_state::closed;
  std::uint32_t consecutive_failures_ = 0;
  std::uint32_t half_open_successes_ = 0;
  std::uint32_t half_open_in_flight_ = 0;
  std::chrono::steady_clock::time_point opened_at_{};
};

} // namespace cnetmod::grpc::governance
