module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.governance.endpoint;

import std;

namespace cnetmod::grpc::governance {

export enum class endpoint_state {
  healthy,
  unhealthy,
  available = healthy,
  unavailable = unhealthy,
};

export struct endpoint {
  endpoint();
  endpoint(std::string id, std::string url, std::uint32_t weight = 1,
           std::uint32_t priority = 0,
           endpoint_state state = endpoint_state::healthy);

  [[nodiscard]] auto is_available() const noexcept -> bool;

  std::string id;
  std::string url;
  std::uint32_t weight = 1;
  std::uint32_t priority = 0;
  endpoint_state state = endpoint_state::healthy;
};

} // namespace cnetmod::grpc::governance
