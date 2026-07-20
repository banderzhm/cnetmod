module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.server_policy;

import std;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.governance.admission;
import cnetmod.protocol.grpc.governance.observability;

namespace cnetmod::grpc::governance {

export struct server_policy_options {
  std::size_t max_concurrent_calls = std::numeric_limits<std::size_t>::max();
};

export class call_guard {
public:
  call_guard() noexcept = default;
  ~call_guard();

  call_guard(const call_guard &) = delete;
  auto operator=(const call_guard &) -> call_guard & = delete;
  call_guard(call_guard &&other) noexcept;
  auto operator=(call_guard &&other) noexcept -> call_guard &;

  void complete(const status &result,
                std::chrono::steady_clock::time_point started) noexcept;
  [[nodiscard]] auto completed() const noexcept -> bool;

private:
  friend class server_policy;

  call_guard(concurrency_limiter::guard admission_guard,
             std::shared_ptr<call_statistics> statistics,
             std::chrono::steady_clock::time_point started) noexcept;

  concurrency_limiter::guard admission_guard_;
  std::shared_ptr<call_statistics> statistics_;
  std::chrono::steady_clock::time_point started_{};
  bool completed_ = true;
};

export class server_policy {
public:
  explicit server_policy(server_policy_options options = {}) noexcept;

  [[nodiscard]] auto begin(const call_context &context)
      -> std::expected<call_guard, status>;
  [[nodiscard]] auto concurrency() const noexcept
      -> const concurrency_limiter &;
  [[nodiscard]] auto rate_limits() noexcept -> rate_limit_registry &;
  [[nodiscard]] auto rate_limits() const noexcept
      -> const rate_limit_registry &;
  [[nodiscard]] auto statistics() noexcept -> call_statistics_registry &;
  [[nodiscard]] auto statistics() const noexcept
      -> const call_statistics_registry &;

private:
  concurrency_limiter concurrency_;
  rate_limit_registry rate_limits_;
  call_statistics_registry statistics_;
};

} // namespace cnetmod::grpc::governance
