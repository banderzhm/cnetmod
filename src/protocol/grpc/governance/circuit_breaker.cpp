module cnetmod.protocol.grpc.governance.circuit_breaker;

import std;

namespace cnetmod::grpc::governance {
namespace {

auto normalized_config(circuit_breaker_config config) noexcept
    -> circuit_breaker_config {
  config.failure_threshold = std::max(config.failure_threshold, 1u);
  config.success_threshold = std::max(config.success_threshold, 1u);
  config.open_duration =
      std::max(config.open_duration, std::chrono::milliseconds::zero());
  config.half_open_max_requests = std::max(config.half_open_max_requests, 1u);
  return config;
}

} // namespace

circuit_breaker::circuit_breaker(circuit_breaker_config config) noexcept
    : config_(normalized_config(config)) {}

auto circuit_breaker::try_acquire() noexcept -> bool {
  std::lock_guard lock(mutex_);
  transition_if_expired(std::chrono::steady_clock::now());
  if (state_ == circuit_breaker_state::open)
    return false;
  if (state_ == circuit_breaker_state::half_open) {
    if (half_open_in_flight_ >= config_.half_open_max_requests)
      return false;
    ++half_open_in_flight_;
  }
  return true;
}

void circuit_breaker::record_success() noexcept {
  std::lock_guard lock(mutex_);
  if (state_ == circuit_breaker_state::half_open) {
    if (half_open_in_flight_ > 0)
      --half_open_in_flight_;
    if (++half_open_successes_ >= config_.success_threshold) {
      state_ = circuit_breaker_state::closed;
      consecutive_failures_ = 0;
      half_open_successes_ = 0;
      half_open_in_flight_ = 0;
    }
    return;
  }
  if (state_ == circuit_breaker_state::closed)
    consecutive_failures_ = 0;
}

void circuit_breaker::record_failure() noexcept {
  std::lock_guard lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  if (state_ == circuit_breaker_state::half_open) {
    if (half_open_in_flight_ > 0)
      --half_open_in_flight_;
    open(now);
    return;
  }
  if (state_ == circuit_breaker_state::closed &&
      ++consecutive_failures_ >= config_.failure_threshold)
    open(now);
}

void circuit_breaker::reset() noexcept {
  std::lock_guard lock(mutex_);
  state_ = circuit_breaker_state::closed;
  consecutive_failures_ = 0;
  half_open_successes_ = 0;
  half_open_in_flight_ = 0;
  opened_at_ = {};
}

auto circuit_breaker::state() const noexcept -> circuit_breaker_state {
  std::lock_guard lock(mutex_);
  const_cast<circuit_breaker *>(this)->transition_if_expired(
      std::chrono::steady_clock::now());
  return state_;
}

void circuit_breaker::transition_if_expired(
    std::chrono::steady_clock::time_point now) noexcept {
  if (state_ != circuit_breaker_state::open ||
      now - opened_at_ < config_.open_duration)
    return;
  state_ = circuit_breaker_state::half_open;
  half_open_successes_ = 0;
  half_open_in_flight_ = 0;
}

void circuit_breaker::open(std::chrono::steady_clock::time_point now) noexcept {
  state_ = circuit_breaker_state::open;
  opened_at_ = now;
  consecutive_failures_ = 0;
  half_open_successes_ = 0;
  half_open_in_flight_ = 0;
}

} // namespace cnetmod::grpc::governance
