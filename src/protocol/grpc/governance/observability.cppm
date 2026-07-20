module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.observability;

import std;

namespace cnetmod::grpc::governance {

export struct call_snapshot {
  std::uint64_t started = 0;
  std::uint64_t completed = 0;
  std::uint64_t failed = 0;
  std::uint64_t in_flight = 0;
  std::uint64_t latency_count = 0;
  std::chrono::nanoseconds latency_sum{};
  std::chrono::nanoseconds latency_min{};
  std::chrono::nanoseconds latency_max{};
};

export class call_statistics {
public:
  void record_started() noexcept;
  void record_completed(std::chrono::nanoseconds latency,
                        bool failed = false) noexcept;

  [[nodiscard]] auto snapshot() const noexcept -> call_snapshot;

private:
  std::atomic<std::uint64_t> started_{0};
  std::atomic<std::uint64_t> completed_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> in_flight_{0};
  std::atomic<std::uint64_t> latency_count_{0};
  std::atomic<std::uint64_t> latency_sum_ns_{0};
  std::atomic<std::uint64_t> latency_min_ns_{
      std::numeric_limits<std::uint64_t>::max()};
  std::atomic<std::uint64_t> latency_max_ns_{0};
};

/// Provides stable, independently synchronized statistics per service/method.
export class call_statistics_registry {
public:
  [[nodiscard]] auto for_method(std::string service, std::string method)
      -> std::shared_ptr<call_statistics>;
  [[nodiscard]] auto snapshot(std::string_view service,
                              std::string_view method) const
      -> std::optional<call_snapshot>;

private:
  using method_statistics =
      std::map<std::string, std::shared_ptr<call_statistics>, std::less<>>;

  mutable std::mutex mutex_;
  std::map<std::string, method_statistics, std::less<>> statistics_;
};

} // namespace cnetmod::grpc::governance
