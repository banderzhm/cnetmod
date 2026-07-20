module cnetmod.protocol.grpc.governance.server_policy;

import std;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.governance.admission;
import cnetmod.protocol.grpc.governance.observability;

namespace cnetmod::grpc::governance {

call_guard::call_guard(concurrency_limiter::guard admission_guard,
                       std::shared_ptr<call_statistics> statistics,
                       std::chrono::steady_clock::time_point started) noexcept
    : admission_guard_(std::move(admission_guard)),
      statistics_(std::move(statistics)), started_(started), completed_(false) {
}

call_guard::~call_guard() {
  if (!completed_)
    complete(status{.code = status_code::unknown}, started_);
}

call_guard::call_guard(call_guard &&other) noexcept
    : admission_guard_(std::move(other.admission_guard_)),
      statistics_(std::move(other.statistics_)), started_(other.started_),
      completed_(std::exchange(other.completed_, true)) {}

auto call_guard::operator=(call_guard &&other) noexcept -> call_guard & {
  if (this != &other) {
    if (!completed_)
      complete(status{.code = status_code::unknown}, started_);
    admission_guard_ = std::move(other.admission_guard_);
    statistics_ = std::move(other.statistics_);
    started_ = other.started_;
    completed_ = std::exchange(other.completed_, true);
  }
  return *this;
}

void call_guard::complete(
    const status &result,
    std::chrono::steady_clock::time_point started) noexcept {
  if (completed_)
    return;
  completed_ = true;
  if (statistics_) {
    const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started);
    statistics_->record_completed(latency, !result.ok());
  }
  admission_guard_.reset();
}

auto call_guard::completed() const noexcept -> bool { return completed_; }

server_policy::server_policy(server_policy_options options) noexcept
    : concurrency_(options.max_concurrent_calls) {}

auto server_policy::begin(const call_context &context)
    -> std::expected<call_guard, status> {
  if (!rate_limits_.try_consume(context.service, context.method)) {
    return std::unexpected(status{.code = status_code::resource_exhausted,
                                  .message = "gRPC rate limit exceeded"});
  }

  auto admission_guard = concurrency_.try_acquire();
  if (!admission_guard) {
    return std::unexpected(
        status{.code = status_code::resource_exhausted,
               .message = "gRPC concurrent call limit exceeded"});
  }

  auto statistics = statistics_.for_method(context.service, context.method);
  statistics->record_started();
  return call_guard{std::move(*admission_guard), std::move(statistics),
                    context.started};
}

auto server_policy::concurrency() const noexcept
    -> const concurrency_limiter & {
  return concurrency_;
}

auto server_policy::rate_limits() noexcept -> rate_limit_registry & {
  return rate_limits_;
}

auto server_policy::rate_limits() const noexcept
    -> const rate_limit_registry & {
  return rate_limits_;
}

auto server_policy::statistics() noexcept -> call_statistics_registry & {
  return statistics_;
}

auto server_policy::statistics() const noexcept
    -> const call_statistics_registry & {
  return statistics_;
}

} // namespace cnetmod::grpc::governance
