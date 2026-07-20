module cnetmod.protocol.grpc.governance.retry;

import std;

namespace cnetmod::grpc::governance {
namespace {

auto normalized_max_attempts(std::uint32_t attempts) noexcept -> std::uint32_t {
  return std::max(attempts, 1u);
}

auto normalized_backoff(std::chrono::milliseconds backoff) noexcept
    -> std::chrono::milliseconds {
  return std::max(backoff, std::chrono::milliseconds::zero());
}

auto normalized_multiplier(double multiplier) noexcept -> double {
  return std::isfinite(multiplier) && multiplier >= 1.0 ? multiplier : 1.0;
}

auto normalized_jitter(double jitter) noexcept -> double {
  return std::clamp(std::isfinite(jitter) ? jitter : 0.0, 0.0, 1.0);
}

auto normalized_config(retry_budget_config config) noexcept
    -> retry_budget_config {
  config.max_tokens = std::max(config.max_tokens, 1u);
  config.token_ratio = std::max(config.token_ratio, 1u);
  config.retry_threshold =
      std::min(config.retry_threshold, config.max_tokens - 1);
  return config;
}

} // namespace

auto retry_policy::allows_retry(status_code status,
                                std::uint32_t completed_attempts) const noexcept
    -> bool {
  if (completed_attempts >= normalized_max_attempts(max_attempts))
    return false;
  return std::ranges::find(retryable_status_codes, status) !=
         retryable_status_codes.end();
}

auto retry_policy::backoff_for(std::uint32_t retry_number) const
    -> std::chrono::milliseconds {
  const auto initial = normalized_backoff(initial_backoff);
  const auto maximum = std::max(normalized_backoff(max_backoff), initial);
  const long double scaled =
      static_cast<long double>(initial.count()) *
      std::pow(normalized_multiplier(backoff_multiplier), retry_number);
  const auto capped = std::min<long double>(scaled, maximum.count());
  auto delay = std::chrono::milliseconds(
      static_cast<std::chrono::milliseconds::rep>(capped));

  const auto spread = normalized_jitter(jitter);
  if (delay == std::chrono::milliseconds::zero() || spread == 0.0)
    return delay;

  thread_local std::mt19937_64 engine{std::random_device{}()};
  std::uniform_real_distribution<double> distribution(1.0 - spread,
                                                      1.0 + spread);
  const long double jittered =
      static_cast<long double>(delay.count()) * distribution(engine);
  return std::chrono::milliseconds(
      static_cast<std::chrono::milliseconds::rep>(std::clamp<long double>(
          jittered, 0.0L, static_cast<long double>(maximum.count()))));
}

retry_budget::retry_budget(retry_budget_config config) noexcept
    : config_(normalized_config(config)), tokens_(config_.max_tokens) {}

auto retry_budget::try_acquire() noexcept -> bool {
  std::lock_guard lock(mutex_);
  if (tokens_ <= config_.retry_threshold)
    return false;
  --tokens_;
  return true;
}

void retry_budget::record_success() noexcept {
  std::lock_guard lock(mutex_);
  const auto remaining = config_.max_tokens - tokens_;
  tokens_ += std::min(config_.token_ratio, remaining);
}

void retry_budget::reset() noexcept {
  std::lock_guard lock(mutex_);
  tokens_ = config_.max_tokens;
}

auto retry_budget::available_tokens() const noexcept -> std::uint32_t {
  std::lock_guard lock(mutex_);
  return tokens_;
}

auto retry_budget::allows_retry() const noexcept -> bool {
  std::lock_guard lock(mutex_);
  return tokens_ > config_.retry_threshold;
}

} // namespace cnetmod::grpc::governance
