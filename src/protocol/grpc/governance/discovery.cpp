module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.governance.discovery;

import std;
import cnetmod.protocol.grpc.governance.endpoint;

namespace cnetmod::grpc::governance {

static_discovery::static_discovery() = default;

static_discovery::static_discovery(std::vector<endpoint> endpoints)
    : endpoints_(std::move(endpoints)) {}

void static_discovery::replace_snapshot(std::vector<endpoint> endpoints) {
  std::unique_lock lock(mutex_);
  endpoints_ = std::move(endpoints);
}

auto static_discovery::snapshot() const -> std::vector<endpoint> {
  std::shared_lock lock(mutex_);
  return endpoints_;
}

} // namespace cnetmod::grpc::governance
