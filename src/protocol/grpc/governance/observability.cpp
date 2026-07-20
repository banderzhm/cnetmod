module cnetmod.protocol.grpc.governance.observability;

import std;

namespace cnetmod::grpc::governance {
namespace {

auto non_negative_nanoseconds(std::chrono::nanoseconds latency) noexcept
    -> std::uint64_t {
  return latency.count() > 0 ? static_cast<std::uint64_t>(latency.count()) : 0;
}

void update_minimum(std::atomic<std::uint64_t> &target,
                    std::uint64_t value) noexcept {
  auto current = target.load(std::memory_order_relaxed);
  while (value < current && !target.compare_exchange_weak(
                                current, value, std::memory_order_relaxed,
                                std::memory_order_relaxed)) {
  }
}

void update_maximum(std::atomic<std::uint64_t> &target,
                    std::uint64_t value) noexcept {
  auto current = target.load(std::memory_order_relaxed);
  while (value > current && !target.compare_exchange_weak(
                                current, value, std::memory_order_relaxed,
                                std::memory_order_relaxed)) {
  }
}

} // namespace

void call_statistics::record_started() noexcept {
  started_.fetch_add(1, std::memory_order_relaxed);
  in_flight_.fetch_add(1, std::memory_order_relaxed);
}

void call_statistics::record_completed(std::chrono::nanoseconds latency,
                                       bool failed) noexcept {
  const auto latency_ns = non_negative_nanoseconds(latency);
  completed_.fetch_add(1, std::memory_order_relaxed);
  if (failed)
    failed_.fetch_add(1, std::memory_order_relaxed);

  auto current = in_flight_.load(std::memory_order_relaxed);
  while (current != 0 && !in_flight_.compare_exchange_weak(
                             current, current - 1, std::memory_order_relaxed,
                             std::memory_order_relaxed)) {
  }

  latency_count_.fetch_add(1, std::memory_order_relaxed);
  latency_sum_ns_.fetch_add(latency_ns, std::memory_order_relaxed);
  update_minimum(latency_min_ns_, latency_ns);
  update_maximum(latency_max_ns_, latency_ns);
}

auto call_statistics::snapshot() const noexcept -> call_snapshot {
  const auto count = latency_count_.load(std::memory_order_relaxed);
  const auto minimum = latency_min_ns_.load(std::memory_order_relaxed);
  return {.started = started_.load(std::memory_order_relaxed),
          .completed = completed_.load(std::memory_order_relaxed),
          .failed = failed_.load(std::memory_order_relaxed),
          .in_flight = in_flight_.load(std::memory_order_relaxed),
          .latency_count = count,
          .latency_sum = std::chrono::nanoseconds(
              latency_sum_ns_.load(std::memory_order_relaxed)),
          .latency_min = std::chrono::nanoseconds(count == 0 ? 0 : minimum),
          .latency_max = std::chrono::nanoseconds(
              latency_max_ns_.load(std::memory_order_relaxed))};
}

auto call_statistics_registry::for_method(std::string service,
                                          std::string method)
    -> std::shared_ptr<call_statistics> {
  std::scoped_lock lock(mutex_);
  auto &statistics = statistics_[std::move(service)][std::move(method)];
  if (!statistics)
    statistics = std::make_shared<call_statistics>();
  return statistics;
}

auto call_statistics_registry::snapshot(std::string_view service,
                                        std::string_view method) const
    -> std::optional<call_snapshot> {
  std::shared_ptr<call_statistics> statistics;
  {
    std::scoped_lock lock(mutex_);
    const auto service_it = statistics_.find(service);
    if (service_it == statistics_.end())
      return std::nullopt;
    const auto method_it = service_it->second.find(method);
    if (method_it == service_it->second.end())
      return std::nullopt;
    statistics = method_it->second;
  }
  return statistics->snapshot();
}

} // namespace cnetmod::grpc::governance
