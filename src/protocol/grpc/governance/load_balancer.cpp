module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.governance.load_balancer;

import std;
import cnetmod.protocol.grpc.governance.discovery;
import cnetmod.protocol.grpc.governance.endpoint;

namespace cnetmod::grpc::governance {

namespace {

auto endpoint_key(const endpoint &value) -> std::string {
  return value.id.empty() ? value.url : value.id;
}

} // namespace

auto load_balancer::pick(std::span<const endpoint> endpoints)
    -> std::optional<endpoint> {
  std::scoped_lock lock(mutex_);

  std::optional<std::uint32_t> selected_priority;
  for (const auto &value : endpoints) {
    if (!value.is_available() || value.weight == 0)
      continue;
    if (!selected_priority || value.priority < *selected_priority)
      selected_priority = value.priority;
  }
  if (!selected_priority)
    return std::nullopt;

  const endpoint *selected = nullptr;
  std::int64_t total_weight = 0;
  for (const auto &value : endpoints) {
    if (!value.is_available() || value.weight == 0 ||
        value.priority != *selected_priority) {
      continue;
    }

    const auto key = endpoint_key(value);
    auto &current_weight = current_weights_[key];
    current_weight += static_cast<std::int64_t>(value.weight);
    total_weight += static_cast<std::int64_t>(value.weight);
    if (!selected || current_weight > current_weights_[endpoint_key(*selected)])
      selected = &value;
  }

  if (!selected)
    return std::nullopt;

  current_weights_[endpoint_key(*selected)] -= total_weight;
  return *selected;
}

auto load_balancer::pick(const static_discovery &discovery)
    -> std::optional<endpoint> {
  auto endpoints = discovery.snapshot();
  return pick(endpoints);
}

} // namespace cnetmod::grpc::governance
